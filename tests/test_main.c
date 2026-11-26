#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "dexhand/dex_control_loop.h"
#include "dexhand/dex_crc16.h"
#include "dexhand/dex_protocol.h"
#include "dexhand/dex_recorder.h"
#include "dexhand/dex_spsc_ring.h"

static int failures = 0;

#define CHECK(cond)                                                                      \
    do {                                                                                 \
        if (!(cond)) {                                                                   \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                       \
            failures++;                                                                  \
        }                                                                                \
    } while (0)

typedef struct {
    int writes;
    size_t bytes;
} MemoryWriter;

static int memory_writer(const uint8_t *frame, size_t len, void *ctx)
{
    MemoryWriter *w = (MemoryWriter *)ctx;
    if (frame == NULL || len == 0u || w == NULL) {
        return -1;
    }
    w->writes++;
    w->bytes += len;
    return 0;
}

static void test_crc16_known_vector(void)
{
    const char *ascii = "123456789";
    CHECK(dex_crc16_ccitt_false((const uint8_t *)ascii, strlen(ascii)) == 0x29B1u);
}

static void test_frame_roundtrip_and_crc_failure(void)
{
    uint8_t out[DEX_FRAME_MAX_SIZE];
    uint8_t bad[DEX_FRAME_MAX_SIZE];
    const uint8_t payload[] = {'h', 'e', 'l', 'l', 'o'};
    DexFrameView view;
    size_t encoded = dex_frame_encode(DEX_FRAME_EVENT, 0u, 7u, 100u, 200u,
                                      payload, sizeof(payload), out, sizeof(out));
    CHECK(encoded == DEX_FRAME_HEADER_SIZE + sizeof(payload) + DEX_FRAME_CRC_SIZE);
    CHECK(dex_frame_decode(out, encoded, &view) == 0);
    CHECK(view.type == DEX_FRAME_EVENT);
    CHECK(view.seq == 7u);
    CHECK(view.device_ns == 100u);
    CHECK(view.host_ns == 200u);
    CHECK(view.payload_len == sizeof(payload));
    CHECK(memcmp(view.payload, payload, sizeof(payload)) == 0);

    memcpy(bad, out, encoded);
    bad[DEX_FRAME_HEADER_SIZE] ^= 0x01u;
    CHECK(dex_frame_decode(bad, encoded, &view) == -3);
}

static void test_control_payload_codec(void)
{
    DexControlSample sample;
    DexControlSample decoded;
    uint8_t payload[DEX_FRAME_MAX_PAYLOAD];
    size_t len;

    memset(&sample, 0, sizeof(sample));
    sample.cycle = 42u;
    sample.device_ns = 1234u;
    sample.joint_count = 2u;
    sample.safety_level = DEX_SAFETY_OK;
    sample.control_mode = DEX_MODE_POSITION_P;
    sample.cycle_budget_us = 1000u;
    sample.cycle_time_us = 87u;
    sample.target_mrad[0] = -123;
    sample.target_mrad[1] = 456;
    sample.joints[0].position_mrad = -120;
    sample.joints[0].velocity_mrad_s = 30;
    sample.joints[0].current_ma = 210;
    sample.joints[0].temperature_cdeg = 3701;
    sample.joints[1].position_mrad = 450;
    sample.joints[1].current_ma = -190;

    len = dex_control_sample_encode(&sample, payload, sizeof(payload));
    CHECK(len == dex_control_sample_payload_size(2u));
    CHECK(dex_control_sample_decode(payload, len, &decoded) == 0);
    CHECK(decoded.cycle == 42u);
    CHECK(decoded.joint_count == 2u);
    CHECK(decoded.target_mrad[0] == -123);
    CHECK(decoded.joints[0].position_mrad == -120);
    CHECK(decoded.joints[0].temperature_cdeg == 3701);
    CHECK(decoded.joints[1].current_ma == -190);
}

static void test_spsc_ring_fifo(void)
{
    DexSpscRing ring;
    uint32_t storage[4];
    uint32_t value = 0;

    CHECK(dex_spsc_ring_init(&ring, storage, 4u, sizeof(uint32_t)) == 0);
    CHECK(dex_spsc_ring_is_empty(&ring));
    CHECK(dex_spsc_ring_push(&ring, &(uint32_t){11u}));
    CHECK(dex_spsc_ring_push(&ring, &(uint32_t){22u}));
    CHECK(dex_spsc_ring_count(&ring) == 2u);
    CHECK(dex_spsc_ring_pop(&ring, &value));
    CHECK(value == 11u);
    CHECK(dex_spsc_ring_pop(&ring, &value));
    CHECK(value == 22u);
    CHECK(dex_spsc_ring_is_empty(&ring));
    CHECK(!dex_spsc_ring_pop(&ring, &value));
}

static void test_recorder_drain_overflow_and_gap(void)
{
    DexRecorder recorder;
    DexRecorderSlot slots[2];
    MemoryWriter writer = {0, 0};
    DexControlSample sample;
    uint8_t payload[8] = {1, 2, 3, 4, 5, 6, 7, 8};

    CHECK(dex_recorder_init(&recorder, slots, 2u) == 0);
    CHECK(dex_recorder_submit_frame(&recorder, DEX_FRAME_EVENT, 0u, 1u, 2u, payload, sizeof(payload)) == 0);
    CHECK(dex_recorder_submit_frame(&recorder, DEX_FRAME_EVENT, 0u, 1u, 2u, payload, sizeof(payload)) == 0);
    CHECK(dex_recorder_submit_frame(&recorder, DEX_FRAME_EVENT, 0u, 1u, 2u, payload, sizeof(payload)) == -2);
    CHECK(recorder.dropped_frames == 1u);
    CHECK(dex_recorder_drain(&recorder, memory_writer, &writer, 0u) == 2u);
    CHECK(writer.writes == 2);
    CHECK(writer.bytes > 0u);

    memset(&recorder, 0, sizeof(recorder));
    memset(slots, 0, sizeof(slots));
    CHECK(dex_recorder_init(&recorder, slots, 2u) == 0);
    memset(&sample, 0, sizeof(sample));
    sample.joint_count = 1u;
    sample.device_ns = 10u;
    CHECK(dex_recorder_submit_control(&recorder, &sample, 11u) == 0);
    CHECK(dex_recorder_submit_control(&recorder, &sample, 11u) == 0);
    CHECK(dex_recorder_submit_control(&recorder, &sample, 11u) == -2);
    CHECK(recorder.pending_dropped == 1u);
    CHECK(dex_recorder_drain(&recorder, memory_writer, &writer, 0u) == 2u);

    sample.cycle = 3u;
    CHECK(dex_recorder_submit_control(&recorder, &sample, 11u) == 0);
    CHECK(recorder.gap_markers == 1u);
    CHECK(dex_recorder_drain(&recorder, memory_writer, &writer, 0u) == 2u);
    CHECK(writer.writes == 6); /* first recorder: 2; second: 2 + gap + control */
}

static void test_clock_sync_first_sample(void)
{
    DexClockSync clock;
    dex_clock_sync_init(&clock);
    dex_clock_sync_update(&clock, 0u, 1000u);
    CHECK(clock.locked);
    CHECK(dex_clock_sync_to_host_ns(&clock, 123u) == 1123u);
}

static void test_safety_limits_and_timeout(void)
{
    DexSafetyMonitor monitor;
    DexSafetyConfig config;
    DexJointSample joint;
    uint64_t now = 2000u;

    memset(&config, 0, sizeof(config));
    config.joint_min_mrad[0] = -3141;
    config.joint_max_mrad[0] = 3141;
    config.max_current_ma[0] = 1000;
    config.max_temperature_cdeg = 8500;
    config.command_timeout_ns = 0u;
    memset(&joint, 0, sizeof(joint));
    joint.current_ma = 1500;

    dex_safety_init(&monitor, &config, 1u);
    CHECK(dex_safety_evaluate(&monitor, &joint, 1u, now, &config) == DEX_SAFETY_FAULT);
    CHECK((monitor.fault_bits & DEX_FAULT_OVERCURRENT) != 0u);

    config.command_timeout_ns = 1000u;
    joint.current_ma = 10;
    dex_safety_init(&monitor, &config, 1u);
    dex_safety_note_command(&monitor, 0u);
    CHECK(dex_safety_evaluate(&monitor, &joint, 1u, 2000u, &config) == DEX_SAFETY_FAULT);
    CHECK((monitor.fault_bits & DEX_FAULT_COMMAND_TIMEOUT) != 0u);
}

typedef struct {
    int reads;
    int applies;
    DexControlCommand last_command;
} FakePlatformCtx;

static uint64_t fake_now_ns(void *user_ctx)
{
    FakePlatformCtx *ctx = (FakePlatformCtx *)user_ctx;
    return 1000000000ull + (uint64_t)ctx->reads * 1000000ull;
}

static int fake_read_joints(DexJointSample *joints, uint8_t count, void *user_ctx)
{
    FakePlatformCtx *ctx = (FakePlatformCtx *)user_ctx;
    uint8_t i;
    ctx->reads++;
    for (i = 0; i < count; ++i) {
        memset(&joints[i], 0, sizeof(joints[i]));
        joints[i].temperature_cdeg = 3650;
    }
    return 0;
}

static int fake_apply_command(const DexControlCommand *command, void *user_ctx)
{
    FakePlatformCtx *ctx = (FakePlatformCtx *)user_ctx;
    ctx->applies++;
    ctx->last_command = *command;
    return 0;
}

static void test_control_loop_runs_and_applies_current(void)
{
    DexControlLoop loop;
    DexControlConfig config;
    DexHandPlatform platform;
    FakePlatformCtx ctx;
    int32_t targets[2] = {1000, -500};

    memset(&ctx, 0, sizeof(ctx));
    memset(&config, 0, sizeof(config));
    config.joint_count = 2u;
    config.cycle_period_us = 1000u;
    config.p_gain_ma_per_mrad = 2;
    config.max_current_ma = 900;
    config.safety.max_temperature_cdeg = 8500;
    config.safety.command_timeout_ns = 0u;
    for (uint8_t i = 0; i < 2u; ++i) {
        config.safety.joint_min_mrad[i] = -3141;
        config.safety.joint_max_mrad[i] = 3141;
        config.safety.max_current_ma[i] = 1200;
    }

    platform.now_ns = fake_now_ns;
    platform.read_joints = fake_read_joints;
    platform.apply_command = fake_apply_command;
    platform.user_ctx = &ctx;

    CHECK(dex_control_loop_init(&loop, &config, &platform, NULL, NULL) == 0);
    CHECK(dex_control_loop_set_target(&loop, targets, 2u) == 0);
    CHECK(dex_control_loop_step(&loop) == 0);
    CHECK(loop.cycle == 1u);
    CHECK(ctx.reads == 1);
    CHECK(ctx.applies == 1);
    CHECK(ctx.last_command.applied_current_ma[0] == 900); /* clamped from 2000 mA */
    CHECK(ctx.last_command.applied_current_ma[1] == -900);
}

int main(void)
{
    test_crc16_known_vector();
    test_frame_roundtrip_and_crc_failure();
    test_control_payload_codec();
    test_spsc_ring_fifo();
    test_recorder_drain_overflow_and_gap();
    test_clock_sync_first_sample();
    test_safety_limits_and_timeout();
    test_control_loop_runs_and_applies_current();

    if (failures == 0) {
        printf("dexhand tests: ALL PASSED\n");
        return 0;
    }
    printf("dexhand tests: %d FAILURE(S)\n", failures);
    return 1;
}
