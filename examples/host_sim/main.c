#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <math.h>

#include "dexhand/dex_control_loop.h"
#include "dexhand/dex_recorder.h"

#define DEFAULT_CYCLES 1200u
#define DEFAULT_JOINTS 20u
#define DEFAULT_RING_CAPACITY 128u

typedef struct {
    int32_t position_mrad[DEXHAND_MAX_JOINTS];
    int32_t velocity_mrad_s[DEXHAND_MAX_JOINTS];
    int32_t target_mrad[DEXHAND_MAX_JOINTS];
    int32_t current_ma[DEXHAND_MAX_JOINTS];
    uint16_t temperature_cdeg[DEXHAND_MAX_JOINTS];
    uint64_t rng_state;
} SimHand;

static uint64_t sim_now_ns(void *user_ctx)
{
    struct timespec ts;
    (void)user_ctx;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static uint32_t sim_rand(SimHand *hand)
{
    uint64_t x = hand->rng_state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    hand->rng_state = x;
    return (uint32_t)(x >> 32);
}

static int sim_read_joints(DexJointSample *joints, uint8_t count, void *user_ctx)
{
    SimHand *hand = (SimHand *)user_ctx;
    uint8_t i;

    if (joints == NULL || hand == NULL || count > DEXHAND_MAX_JOINTS) {
        return -1;
    }

    /* Fixed 1 ms virtual plant step; this is a data-flow simulator, not a dynamics model. */
    for (i = 0; i < count; ++i) {
        int32_t previous = hand->position_mrad[i];
        int32_t error = hand->target_mrad[i] - hand->position_mrad[i];
        int32_t noise = (int32_t)(sim_rand(hand) % 5u) - 2;

        hand->position_mrad[i] += (error * 8) / 100 + noise;
        hand->velocity_mrad_s[i] = (hand->position_mrad[i] - previous) * 1000;
        hand->temperature_cdeg[i] = (uint16_t)(3650u + i * 12u);

        joints[i].position_mrad = hand->position_mrad[i];
        joints[i].velocity_mrad_s = hand->velocity_mrad_s[i];
        joints[i].current_ma = hand->current_ma[i];
        joints[i].temperature_cdeg = (int16_t)hand->temperature_cdeg[i];
        joints[i].flags = 0u;
    }
    return 0;
}

static int sim_apply_command(const DexControlCommand *command, void *user_ctx)
{
    SimHand *hand = (SimHand *)user_ctx;
    uint8_t i;

    if (command == NULL || hand == NULL) {
        return -1;
    }
    for (i = 0; i < command->joint_count; ++i) {
        hand->target_mrad[i] = command->target_mrad[i];
        hand->current_ma[i] = command->applied_current_ma[i];
    }
    return 0;
}

static int file_writer(const uint8_t *frame, size_t len, void *user_ctx)
{
    FILE *fp = (FILE *)user_ctx;
    return fwrite(frame, 1u, len, fp) == len ? 0 : -1;
}

static void usage(const char *prog)
{
    fprintf(stderr,
            "Usage: %s [--cycles N] [--joints 1..20] [--ring N] [--stall N] --out FILE\n"
            "  --stall N  skip recorder drain for first N cycles to demonstrate backpressure/gap markers\n",
            prog);
}

int main(int argc, char **argv)
{
    const char *out_path = "dexhand-session.dexlog";
    uint32_t cycles = DEFAULT_CYCLES;
    uint32_t ring_capacity = DEFAULT_RING_CAPACITY;
    uint32_t stall_cycles = 0u;
    uint8_t joint_count = DEFAULT_JOINTS;
    int argi;
    FILE *fp = NULL;
    DexRecorderSlot *slots = NULL;
    DexRecorder recorder;
    DexClockSync clock_sync;
    DexControlLoop loop;
    DexControlConfig config;
    DexHandPlatform platform;
    SimHand hand;
    char metadata[512];
    uint8_t event_payload[160];
    size_t payload_len;
    int rc = 1;

    for (argi = 1; argi < argc; ++argi) {
        if ((strcmp(argv[argi], "--cycles") == 0 || strcmp(argv[argi], "-c") == 0) && argi + 1 < argc) {
            cycles = (uint32_t)strtoul(argv[++argi], NULL, 10);
        } else if ((strcmp(argv[argi], "--joints") == 0 || strcmp(argv[argi], "-j") == 0) && argi + 1 < argc) {
            joint_count = (uint8_t)strtoul(argv[++argi], NULL, 10);
        } else if (strcmp(argv[argi], "--ring") == 0 && argi + 1 < argc) {
            ring_capacity = (uint32_t)strtoul(argv[++argi], NULL, 10);
        } else if (strcmp(argv[argi], "--stall") == 0 && argi + 1 < argc) {
            stall_cycles = (uint32_t)strtoul(argv[++argi], NULL, 10);
        } else if ((strcmp(argv[argi], "--out") == 0 || strcmp(argv[argi], "-o") == 0) && argi + 1 < argc) {
            out_path = argv[++argi];
        } else if (strcmp(argv[argi], "-h") == 0 || strcmp(argv[argi], "--help") == 0) {
            usage(argv[0]);
            return 0;
        } else {
            usage(argv[0]);
            return 2;
        }
    }

    if (joint_count == 0u || joint_count > DEXHAND_MAX_JOINTS || ring_capacity == 0u || cycles == 0u) {
        usage(argv[0]);
        return 2;
    }

    memset(&hand, 0, sizeof(hand));
    hand.rng_state = 0x123456789abcdef0ull;

    fp = fopen(out_path, "wb");
    if (fp == NULL) {
        perror("fopen");
        return 1;
    }

    slots = (DexRecorderSlot *)calloc(ring_capacity, sizeof(DexRecorderSlot));
    if (slots == NULL) {
        fprintf(stderr, "calloc recorder slots failed\n");
        goto cleanup;
    }
    if (dex_recorder_init(&recorder, slots, ring_capacity) != 0) {
        fprintf(stderr, "recorder init failed\n");
        goto cleanup;
    }
    dex_clock_sync_init(&clock_sync);

    snprintf(metadata, sizeof(metadata),
             "{\"schema\":\"dexhand-reference-session/v1\",\"device\":\"host-simulated-dexhand\","
             "\"joint_count\":%u,\"control_period_us\":1000,\"note\":\"non-production architecture prototype\"}",
             joint_count);
    (void)dex_recorder_submit_frame(&recorder, DEX_FRAME_SESSION_METADATA, 0u,
                                    sim_now_ns(NULL), sim_now_ns(NULL),
                                    (const uint8_t *)metadata, (uint16_t)strlen(metadata));

    memset(&config, 0, sizeof(config));
    config.joint_count = joint_count;
    config.cycle_period_us = 1000u;
    config.p_gain_ma_per_mrad = 2;
    config.max_current_ma = 900;
    config.safety.max_temperature_cdeg = 8500;
    config.safety.command_timeout_ns = 0u;
    for (uint8_t i = 0; i < joint_count; ++i) {
        config.safety.joint_min_mrad[i] = -3141;
        config.safety.joint_max_mrad[i] = 3141;
        config.safety.max_current_ma[i] = 1200;
    }

    platform.now_ns = sim_now_ns;
    platform.read_joints = sim_read_joints;
    platform.apply_command = sim_apply_command;
    platform.user_ctx = &hand;

    if (dex_control_loop_init(&loop, &config, &platform, &recorder, &clock_sync) != 0) {
        fprintf(stderr, "control loop init failed\n");
        goto cleanup;
    }

    for (uint32_t cycle = 0; cycle < cycles; ++cycle) {
        int32_t targets[DEXHAND_MAX_JOINTS];
        uint64_t now = sim_now_ns(&hand);
        dex_clock_sync_update(&clock_sync, now, now);

        for (uint8_t j = 0; j < joint_count; ++j) {
            double phase = ((double)cycle + (double)j * 3.0) * 0.05;
            targets[j] = (int32_t)(360.0 * sin(phase)) + (j == 0 ? 250 : 0);
        }
        (void)dex_control_loop_set_target(&loop, targets, joint_count);
        (void)dex_control_loop_step(&loop);

        if (stall_cycles == 0u || cycle >= stall_cycles) {
            (void)dex_recorder_drain(&recorder, file_writer, fp, 0u);
        }

        /* Pace the host logic simulation near the nominal 1 kHz control period. */
        struct timespec pace = {0, 1000000L};
        (void)nanosleep(&pace, NULL);
    }

    payload_len = dex_event_payload_encode(1000u, 0u, loop.cycle, "host simulation stopped",
                                           event_payload, sizeof(event_payload));
    if (payload_len > 0u) {
        (void)dex_recorder_submit_frame(&recorder, DEX_FRAME_EVENT, 0u,
                                        sim_now_ns(NULL), sim_now_ns(NULL),
                                        event_payload, (uint16_t)payload_len);
    }
    (void)dex_recorder_drain(&recorder, file_writer, fp, 0u);

    if (fclose(fp) != 0) {
        perror("fclose");
        fp = NULL;
        goto cleanup;
    }
    fp = NULL;

    printf("session=%s\n", out_path);
    printf("cycles=%u joints=%u ring=%u stall=%u\n", cycles, joint_count, ring_capacity, stall_cycles);
    printf("recorder accepted=%u dropped=%u gaps=%u pending=%u bytes=%u backend_errors=%u\n",
           recorder.accepted_frames, recorder.dropped_frames, recorder.gap_markers,
           recorder.pending_dropped, recorder.bytes_written, recorder.backend_errors);
    printf("control max_cycle_us=%u deadline_misses=%u avg_cycle_ns=%llu\n",
           loop.max_cycle_time_us, loop.deadline_miss_count,
           (unsigned long long)(loop.cycle ? loop.total_cycle_time_ns / loop.cycle : 0u));
    rc = 0;

cleanup:
    if (fp != NULL) {
        fclose(fp);
    }
    free(slots);
    return rc;
}
