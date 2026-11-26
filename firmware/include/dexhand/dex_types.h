#ifndef DEXHAND_TYPES_H
#define DEXHAND_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DEXHAND_MAX_JOINTS 20u
#define DEXHAND_PROTOCOL_VERSION 1u

typedef enum {
    DEX_SAFETY_OK = 0,
    DEX_SAFETY_WARN = 1,
    DEX_SAFETY_FAULT = 2
} DexSafetyLevel;

typedef enum {
    DEX_MODE_DISABLED = 0,
    DEX_MODE_POSITION_P = 1,
    DEX_MODE_SAFE_HOLD = 2
} DexControlMode;

typedef enum {
    DEX_FAULT_COMMAND_TIMEOUT = 1u << 0,
    DEX_FAULT_POSITION_LIMIT = 1u << 1,
    DEX_FAULT_OVERCURRENT = 1u << 2,
    DEX_FAULT_OVERTEMP = 1u << 3,
    DEX_FAULT_SENSOR_INVALID = 1u << 4,
    DEX_FAULT_DEADLINE_MISS = 1u << 5
} DexFaultBit;

typedef struct {
    int32_t position_mrad;      /* joint angle, milli-radian */
    int32_t velocity_mrad_s;    /* joint velocity, milli-radian / second */
    int32_t current_ma;         /* q-axis or bus current estimate */
    int16_t temperature_cdeg;   /* degrees Celsius * 100 */
    uint16_t flags;             /* device/vendor status bits */
} DexJointSample;

typedef struct {
    uint8_t joint_count;
    uint8_t mode;
    int32_t target_mrad[DEXHAND_MAX_JOINTS];
    int32_t applied_current_ma[DEXHAND_MAX_JOINTS];
} DexControlCommand;

typedef struct {
    uint32_t cycle;
    uint64_t device_ns;
    uint8_t joint_count;
    uint8_t safety_level;
    uint8_t control_mode;
    uint8_t reserved0;
    uint16_t cycle_budget_us;
    uint16_t cycle_time_us;
    uint32_t recorder_dropped;
    int32_t target_mrad[DEXHAND_MAX_JOINTS];
    int32_t applied_current_ma[DEXHAND_MAX_JOINTS];
    DexJointSample joints[DEXHAND_MAX_JOINTS];
} DexControlSample;

#endif /* DEXHAND_TYPES_H */
