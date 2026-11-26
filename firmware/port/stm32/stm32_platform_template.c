/*
 * Non-build integration template for STM32/Cortex-M ports.
 * This file is intentionally not compiled by the host Makefile.
 *
 * Replace all TODO(port) blocks with the real HAL/bus/storage implementation.
 */

#include "dexhand/dex_control_loop.h"
#include "dexhand/dex_recorder.h"

/* #include "stm32h7xx_hal.h" */  /* TODO(port): use the exact MCU family header */
#include <string.h>

#define HAND_JOINT_COUNT 20u
#define RECORDER_SLOT_COUNT 16u  /* Size according to available RAM and writer stall time. */

static DexRecorderSlot g_recorder_slots[RECORDER_SLOT_COUNT];
static DexRecorder g_recorder;
static DexClockSync g_clock;
static DexControlLoop g_loop;
static uint64_t g_boot_tick_ns;

static uint64_t platform_now_ns(void *user_ctx)
{
    (void)user_ctx;
    /*
     * TODO(port): combine a 64-bit maintained tick with a high-resolution timer.
     * HAL_GetTick() alone has 1 ms resolution and is only a bring-up placeholder.
     */
    return g_boot_tick_ns;
}

static int platform_read_joints(DexJointSample *joints, uint8_t joint_count, void *user_ctx)
{
    (void)user_ctx;
    if (joints == NULL || joint_count == 0u || joint_count > DEXHAND_MAX_JOINTS) {
        return -1;
    }

    memset(joints, 0, sizeof(DexJointSample) * joint_count);

    /*
     * TODO(port):
     * 1. Copy the latest CAN-FD/UART/SPI motor-bus snapshot under a short lock.
     * 2. Fill position_mrad, velocity_mrad_s, current_ma, temperature_cdeg, flags.
     * 3. Mark stale/CRC-failed channels in flags and return non-zero if the whole
     *    sample is invalid. Do not turn a communication timeout into fake zeros.
     */
    return -1;
}

static int platform_apply_command(const DexControlCommand *command, void *user_ctx)
{
    (void)user_ctx;
    if (command == NULL || command->joint_count == 0u) {
        return -1;
    }

    /*
     * TODO(port): encode command->target_mrad / applied_current_ma into the motor
     * bus protocol or local FOC setpoints. Do not block for an ACK inside the 1 kHz
     * path unless the bus protocol and WCET budget explicitly allow it.
     */
    return 0;
}

static int flash_or_usb_writer(const uint8_t *frame, size_t len, void *user_ctx)
{
    (void)user_ctx;
    if (frame == NULL || len == 0u) {
        return -1;
    }

    /*
     * TODO(port): write a complete frame to an append-only file, USB bulk endpoint
     * or network socket. This callback runs in writer/task context, not in the
     * motor ISR. Batch writes where the storage allows it.
     */
    return 0;
}

void dexhand_app_init(void)
{
    DexHandPlatform platform;
    DexControlConfig config;
    int32_t initial_targets[HAND_JOINT_COUNT];
    uint8_t i;

    (void)dex_recorder_init(&g_recorder, g_recorder_slots, RECORDER_SLOT_COUNT);
    dex_clock_sync_init(&g_clock);

    memset(&config, 0, sizeof(config));
    config.joint_count = HAND_JOINT_COUNT;
    config.cycle_period_us = 1000u;
    config.p_gain_ma_per_mrad = 2;
    config.max_current_ma = 900;
    config.safety.max_temperature_cdeg = 8500;
    config.safety.command_timeout_ns = 50000000ull; /* 50 ms candidate timeout */

    for (i = 0; i < HAND_JOINT_COUNT; ++i) {
        config.safety.joint_min_mrad[i] = -3141;
        config.safety.joint_max_mrad[i] = 3141;
        config.safety.max_current_ma[i] = 1200;
        initial_targets[i] = 0;
    }

    platform.now_ns = platform_now_ns;
    platform.read_joints = platform_read_joints;
    platform.apply_command = platform_apply_command;
    platform.user_ctx = NULL;

    (void)dex_control_loop_init(&g_loop, &config, &platform, &g_recorder, &g_clock);
    (void)dex_control_loop_set_target(&g_loop, initial_targets, HAND_JOINT_COUNT);
}

/* Call from a fixed-period timer task or deferred control task. */
void dexhand_control_tick(void)
{
    (void)dex_control_loop_step(&g_loop);
}

/* Call from a low-priority writer task. */
void dexhand_writer_task(void)
{
    (void)dex_recorder_drain(&g_recorder, flash_or_usb_writer, NULL, 0u);
}
