#ifndef DEX_CLOCK_SYNC_H
#define DEX_CLOCK_SYNC_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Lightweight local-clock to recorder/host-clock model.
 * Production target should feed this from CAN sync/PTP/PPS timestamps rather than
 * a software request/response pair only.
 */
typedef struct {
    int64_t offset_ns;
    int64_t drift_ppb;
    uint64_t last_local_ns;
    uint64_t last_host_ns;
    uint32_t sample_count;
    bool locked;
} DexClockSync;

void dex_clock_sync_init(DexClockSync *clock);
void dex_clock_sync_update(DexClockSync *clock, uint64_t local_ns, uint64_t host_ns);
uint64_t dex_clock_sync_to_host_ns(const DexClockSync *clock, uint64_t local_ns);

#endif
