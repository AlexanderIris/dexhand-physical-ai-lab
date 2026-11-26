#ifndef DEX_CONTROL_LOOP_H
#define DEX_CONTROL_LOOP_H

#include "dexhand/dex_clock_sync.h"
#include "dexhand/dex_recorder.h"
#include "dexhand/dex_safety.h"
#include "dexhand/dex_types.h"

typedef struct {
    /* All callbacks run in control-loop context in this reference. */
    uint64_t (*now_ns)(void *user_ctx);
    int (*read_joints)(DexJointSample *joints, uint8_t joint_count, void *user_ctx);
    int (*apply_command)(const DexControlCommand *command, void *user_ctx);
    void *user_ctx;
} DexHandPlatform;

typedef struct {
    uint8_t joint_count;
    uint32_t cycle_period_us;
    int32_t p_gain_ma_per_mrad;
    int32_t max_current_ma;
    DexSafetyConfig safety;
} DexControlConfig;

typedef struct DexControlLoop {
    DexControlConfig config;
    DexHandPlatform platform;
    DexRecorder *recorder;
    DexClockSync *clock;
    DexSafetyMonitor safety;
    DexControlCommand command;
    uint32_t cycle;
    uint32_t deadline_miss_count;
    uint32_t max_cycle_time_us;
    uint64_t total_cycle_time_ns;
    bool initialized;
} DexControlLoop;

int dex_control_loop_init(DexControlLoop *loop,
                          const DexControlConfig *config,
                          const DexHandPlatform *platform,
                          DexRecorder *recorder,
                          DexClockSync *clock);

int dex_control_loop_set_target(DexControlLoop *loop, const int32_t *target_mrad, uint8_t count);

/* One fixed-period iteration. On hardware this is called from a timer task/ISR-deferred task. */
int dex_control_loop_step(DexControlLoop *loop);

#endif
