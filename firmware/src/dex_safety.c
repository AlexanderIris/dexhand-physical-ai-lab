#include "dexhand/dex_safety.h"

#include <stddef.h>

void dex_safety_init(DexSafetyMonitor *monitor, const DexSafetyConfig *config, uint8_t joint_count)
{
    (void)config;
    (void)joint_count;
    if (monitor == NULL) {
        return;
    }
    monitor->level = DEX_SAFETY_OK;
    monitor->fault_bits = 0u;
    monitor->last_command_ns = 0u;
    monitor->command_seen = false;
}

void dex_safety_note_command(DexSafetyMonitor *monitor, uint64_t now_ns)
{
    if (monitor == NULL) {
        return;
    }
    monitor->last_command_ns = now_ns;
    monitor->command_seen = true;
}

void dex_safety_add_fault(DexSafetyMonitor *monitor, uint32_t fault_bit)
{
    if (monitor == NULL) {
        return;
    }
    monitor->fault_bits |= fault_bit;
    monitor->level = DEX_SAFETY_FAULT;
}

DexSafetyLevel dex_safety_evaluate(DexSafetyMonitor *monitor,
                                   const DexJointSample *joints,
                                   uint8_t joint_count,
                                   uint64_t now_ns,
                                   const DexSafetyConfig *config)
{
    uint8_t i;

    if (monitor == NULL || joints == NULL || config == NULL ||
        joint_count > DEXHAND_MAX_JOINTS) {
        return DEX_SAFETY_FAULT;
    }

    /*
     * Reference monitor evaluates current-cycle physical limits. Product firmware
     * should split instantaneous faults from latched faults and define recovery.
     */
    monitor->fault_bits = 0u;
    monitor->level = DEX_SAFETY_OK;

    if (config->command_timeout_ns > 0u && monitor->command_seen &&
        (now_ns - monitor->last_command_ns) > config->command_timeout_ns) {
        monitor->fault_bits |= DEX_FAULT_COMMAND_TIMEOUT;
    }

    for (i = 0; i < joint_count; ++i) {
        if (joints[i].position_mrad < config->joint_min_mrad[i] ||
            joints[i].position_mrad > config->joint_max_mrad[i]) {
            monitor->fault_bits |= DEX_FAULT_POSITION_LIMIT;
        }
        if (joints[i].current_ma > config->max_current_ma[i] ||
            joints[i].current_ma < -config->max_current_ma[i]) {
            monitor->fault_bits |= DEX_FAULT_OVERCURRENT;
        }
        if (joints[i].temperature_cdeg > config->max_temperature_cdeg) {
            monitor->fault_bits |= DEX_FAULT_OVERTEMP;
        }
    }

    if (monitor->fault_bits != 0u) {
        monitor->level = DEX_SAFETY_FAULT;
    }
    return monitor->level;
}
