#ifndef DEX_SPSC_RING_H
#define DEX_SPSC_RING_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>

/*
 * Single-producer / single-consumer fixed-slot ring.
 *
 * Host build uses C11 atomics. On a bare-metal MCU where stdatomic is unavailable
 * or undesirable, replace head/tail access with:
 *   - producer: enter/exit the shortest interrupt-safe critical section, or
 *   - producer/consumer located in one ISR priority domain with memory barriers.
 * The API and copy semantics remain unchanged.
 */
typedef struct {
    uint8_t *storage;
    size_t capacity;
    size_t slot_size;
    atomic_size_t head;
    atomic_size_t tail;
} DexSpscRing;

int dex_spsc_ring_init(DexSpscRing *ring, void *storage, size_t capacity, size_t slot_size);
void dex_spsc_ring_reset(DexSpscRing *ring);
bool dex_spsc_ring_push(DexSpscRing *ring, const void *slot);
bool dex_spsc_ring_pop(DexSpscRing *ring, void *slot);
size_t dex_spsc_ring_count(const DexSpscRing *ring);
bool dex_spsc_ring_is_empty(const DexSpscRing *ring);
bool dex_spsc_ring_is_full(const DexSpscRing *ring);

#endif
