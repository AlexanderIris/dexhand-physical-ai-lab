#ifndef DEX_SAFETY_H
#define DEX_SAFETY_H

#include "dexhand/dex_types.h"

typedef struct {
    int32_t joint_min_mrad[DEXHAND_MAX_JOINTS];
    int32_t joint_max_mrad[DEXHAND_MAX_JOINTS];
    int32_t max_current_ma[DEXHAND_MAX_JOINTS];
    int16_t max_temperature_cdeg;
    uint32_t command_timeout_ns; /* 0 disables external setpoint timeout */
} DexSafetyConfig;

typedef struct {
    DexSafetyLevel level;
    uint32_t fault_bits;
    uint64_t last_command_ns;
    bool command_seen;
} DexSafetyMonitor;

void dex_safety_init(DexSafetyMonitor *monitor, const DexSafetyConfig *config, uint8_t joint_count);
void dex_safety_note_command(DexSafetyMonitor *monitor, uint64_t now_ns);
void dex_safety_add_fault(DexSafetyMonitor *monitor, uint32_t fault_bit);
DexSafetyLevel dex_safety_evaluate(DexSafetyMonitor *monitor,
                                   const DexJointSample *joints,
                                   uint8_t joint_count,
                                   uint64_t now_ns,
                                   const DexSafetyConfig *config);

#endif
