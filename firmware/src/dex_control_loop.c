#include "dexhand/dex_control_loop.h"

#include <string.h>

static uint16_t us_to_u16_saturate(uint64_t value_us)
{
    return value_us > 65535u ? 65535u : (uint16_t)value_us;
}

int dex_control_loop_init(DexControlLoop *loop,
                          const DexControlConfig *config,
                          const DexHandPlatform *platform,
                          DexRecorder *recorder,
                          DexClockSync *clock)
{
    if (loop == NULL || config == NULL || platform == NULL ||
        platform->now_ns == NULL || platform->read_joints == NULL ||
        platform->apply_command == NULL || config->joint_count == 0u ||
        config->joint_count > DEXHAND_MAX_JOINTS || config->cycle_period_us == 0u) {
        return -1;
    }

    memset(loop, 0, sizeof(*loop));
    loop->config = *config;
    loop->platform = *platform;
    loop->recorder = recorder;
    loop->clock = clock;
    dex_safety_init(&loop->safety, &config->safety, config->joint_count);

    loop->command.joint_count = config->joint_count;
    loop->command.mode = DEX_MODE_DISABLED;
    loop->initialized = true;
    return 0;
}

int dex_control_loop_set_target(DexControlLoop *loop, const int32_t *target_mrad, uint8_t count)
{
    uint8_t i;
    uint64_t now;

    if (loop == NULL || !loop->initialized || target_mrad == NULL ||
        count != loop->config.joint_count) {
        return -1;
    }

    for (i = 0; i < count; ++i) {
        loop->command.target_mrad[i] = target_mrad[i];
    }
    loop->command.joint_count = count;
    loop->command.mode = DEX_MODE_POSITION_P;

    now = loop->platform.now_ns(loop->platform.user_ctx);
    dex_safety_note_command(&loop->safety, now);
    return 0;
}

int dex_control_loop_step(DexControlLoop *loop)
{
    DexJointSample joints[DEXHAND_MAX_JOINTS];
    DexControlSample sample;
    uint64_t t0_ns;
    uint64_t t_control_end_ns;
    uint64_t elapsed_ns;
    uint64_t elapsed_us;
    uint64_t host_ns;
    DexSafetyLevel safety_level;
    int read_rc;
    int apply_rc;
    uint8_t i;

    if (loop == NULL || !loop->initialized) {
        return -1;
    }

    memset(joints, 0, sizeof(joints));
    memset(&sample, 0, sizeof(sample));
    t0_ns = loop->platform.now_ns(loop->platform.user_ctx);

    read_rc = loop->platform.read_joints(joints, loop->config.joint_count,
                                         loop->platform.user_ctx);

    safety_level = dex_safety_evaluate(&loop->safety, joints, loop->config.joint_count,
                                       t0_ns, &loop->config.safety);
    if (read_rc != 0) {
        dex_safety_add_fault(&loop->safety, DEX_FAULT_SENSOR_INVALID);
        safety_level = DEX_SAFETY_FAULT;
    }

    for (i = 0; i < loop->config.joint_count; ++i) {
        int32_t current_cmd = 0;
        if (safety_level == DEX_SAFETY_OK && loop->command.mode == DEX_MODE_POSITION_P) {
            int32_t error_mrad = loop->command.target_mrad[i] - joints[i].position_mrad;
            int64_t raw_ma = (int64_t)loop->config.p_gain_ma_per_mrad * error_mrad;
            if (raw_ma > loop->config.max_current_ma) {
                raw_ma = loop->config.max_current_ma;
            } else if (raw_ma < -loop->config.max_current_ma) {
                raw_ma = -loop->config.max_current_ma;
            }
            current_cmd = (int32_t)raw_ma;
            loop->command.mode = DEX_MODE_POSITION_P;
        } else {
            current_cmd = 0;
            loop->command.mode = DEX_MODE_SAFE_HOLD;
        }
        loop->command.applied_current_ma[i] = current_cmd;
    }

    apply_rc = loop->platform.apply_command(&loop->command, loop->platform.user_ctx);
    t_control_end_ns = loop->platform.now_ns(loop->platform.user_ctx);

    elapsed_ns = (t_control_end_ns >= t0_ns) ? (t_control_end_ns - t0_ns) : 0u;
    elapsed_us = elapsed_ns / 1000u;
    loop->total_cycle_time_ns += elapsed_ns;
    if (elapsed_us > loop->max_cycle_time_us) {
        loop->max_cycle_time_us = (uint32_t)elapsed_us;
    }
    if (elapsed_us > loop->config.cycle_period_us) {
        loop->deadline_miss_count++;
        dex_safety_add_fault(&loop->safety, DEX_FAULT_DEADLINE_MISS);
        safety_level = DEX_SAFETY_FAULT;
    }

    sample.cycle = loop->cycle;
    sample.device_ns = t0_ns;
    sample.joint_count = loop->config.joint_count;
    sample.safety_level = (uint8_t)safety_level;
    sample.control_mode = loop->command.mode;
    sample.cycle_budget_us = us_to_u16_saturate(loop->config.cycle_period_us);
    sample.cycle_time_us = us_to_u16_saturate(elapsed_us);
    sample.recorder_dropped = loop->recorder ? loop->recorder->dropped_frames : 0u;
    for (i = 0; i < loop->config.joint_count; ++i) {
        sample.target_mrad[i] = loop->command.target_mrad[i];
        sample.applied_current_ma[i] = loop->command.applied_current_ma[i];
        sample.joints[i] = joints[i];
    }

    host_ns = loop->clock ? dex_clock_sync_to_host_ns(loop->clock, t0_ns) : t0_ns;
    if (loop->recorder != NULL) {
        (void)dex_recorder_submit_control(loop->recorder, &sample, host_ns);
    }

    loop->cycle++;
    return apply_rc == 0 ? read_rc : apply_rc;
}
