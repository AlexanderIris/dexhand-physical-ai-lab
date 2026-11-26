#include "dexhand/dex_clock_sync.h"

#include <stddef.h>

#define DEX_CLOCK_ALPHA_DIV 8
#define NSEC_PER_SEC 1000000000LL

void dex_clock_sync_init(DexClockSync *clock)
{
    if (clock == NULL) {
        return;
    }
    clock->offset_ns = 0;
    clock->drift_ppb = 0;
    clock->last_local_ns = 0u;
    clock->last_host_ns = 0u;
    clock->sample_count = 0u;
    clock->locked = false;
}

void dex_clock_sync_update(DexClockSync *clock, uint64_t local_ns, uint64_t host_ns)
{
    int64_t instant_offset;
    int64_t d_local;
    int64_t d_host;
    int64_t instant_drift;

    if (clock == NULL) {
        return;
    }

    instant_offset = (int64_t)host_ns - (int64_t)local_ns;
    if (!clock->locked) {
        clock->offset_ns = instant_offset;
        clock->last_local_ns = local_ns;
        clock->last_host_ns = host_ns;
        clock->sample_count = 1u;
        clock->locked = true;
        return;
    }

    clock->offset_ns += (instant_offset - clock->offset_ns) / DEX_CLOCK_ALPHA_DIV;

    if (local_ns > clock->last_local_ns && host_ns >= clock->last_host_ns) {
        d_local = (int64_t)(local_ns - clock->last_local_ns);
        d_host = (int64_t)(host_ns - clock->last_host_ns);
        if (d_local > 0) {
            instant_drift = ((d_host - d_local) * NSEC_PER_SEC) / d_local;
            clock->drift_ppb += (instant_drift - clock->drift_ppb) / DEX_CLOCK_ALPHA_DIV;
        }
    }

    clock->last_local_ns = local_ns;
    clock->last_host_ns = host_ns;
    clock->sample_count++;
}

uint64_t dex_clock_sync_to_host_ns(const DexClockSync *clock, uint64_t local_ns)
{
    int64_t elapsed;
    int64_t correction;
    int64_t converted;

    if (clock == NULL || !clock->locked) {
        return local_ns;
    }

    elapsed = (local_ns >= clock->last_local_ns)
                  ? (int64_t)(local_ns - clock->last_local_ns)
                  : 0;
    correction = clock->offset_ns +
                 (clock->drift_ppb * elapsed) / NSEC_PER_SEC;
    converted = (int64_t)local_ns + correction;
    return converted > 0 ? (uint64_t)converted : 0u;
}
