#include "dexhand/dex_spsc_ring.h"

#include <string.h>

int dex_spsc_ring_init(DexSpscRing *ring, void *storage, size_t capacity, size_t slot_size)
{
    if (ring == NULL || storage == NULL || capacity == 0u || slot_size == 0u) {
        return -1;
    }
    memset(ring, 0, sizeof(*ring));
    ring->storage = (uint8_t *)storage;
    ring->capacity = capacity;
    ring->slot_size = slot_size;
    atomic_init(&ring->head, 0u);
    atomic_init(&ring->tail, 0u);
    return 0;
}

void dex_spsc_ring_reset(DexSpscRing *ring)
{
    if (ring == NULL) {
        return;
    }
    atomic_store_explicit(&ring->head, 0u, memory_order_relaxed);
    atomic_store_explicit(&ring->tail, 0u, memory_order_relaxed);
}

bool dex_spsc_ring_push(DexSpscRing *ring, const void *slot)
{
    size_t head;
    size_t tail;
    size_t index;

    if (ring == NULL || slot == NULL) {
        return false;
    }

    head = atomic_load_explicit(&ring->head, memory_order_acquire);
    tail = atomic_load_explicit(&ring->tail, memory_order_relaxed);
    if (tail - head >= ring->capacity) {
        return false;
    }

    index = tail % ring->capacity;
    memcpy(ring->storage + (index * ring->slot_size), slot, ring->slot_size);
    atomic_store_explicit(&ring->tail, tail + 1u, memory_order_release);
    return true;
}

bool dex_spsc_ring_pop(DexSpscRing *ring, void *slot)
{
    size_t head;
    size_t tail;
    size_t index;

    if (ring == NULL || slot == NULL) {
        return false;
    }

    tail = atomic_load_explicit(&ring->tail, memory_order_acquire);
    head = atomic_load_explicit(&ring->head, memory_order_relaxed);
    if (head == tail) {
        return false;
    }

    index = head % ring->capacity;
    memcpy(slot, ring->storage + (index * ring->slot_size), ring->slot_size);
    atomic_store_explicit(&ring->head, head + 1u, memory_order_release);
    return true;
}

size_t dex_spsc_ring_count(const DexSpscRing *ring)
{
    size_t head;
    size_t tail;
    if (ring == NULL) {
        return 0u;
    }
    head = atomic_load_explicit(&ring->head, memory_order_acquire);
    tail = atomic_load_explicit(&ring->tail, memory_order_acquire);
    return tail - head;
}

bool dex_spsc_ring_is_empty(const DexSpscRing *ring)
{
    return dex_spsc_ring_count(ring) == 0u;
}

bool dex_spsc_ring_is_full(const DexSpscRing *ring)
{
    return dex_spsc_ring_count(ring) >= ring->capacity;
}
