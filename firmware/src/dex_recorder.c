#include "dexhand/dex_recorder.h"

#include <string.h>

static int enqueue_slot(DexRecorder *r, const DexRecorderSlot *slot)
{
    if (!dex_spsc_ring_push(&r->ring, slot)) {
        r->dropped_frames++;
        r->pending_dropped++;
        return -2;
    }
    r->next_seq++;
    r->accepted_frames++;
    r->bytes_accepted += slot->len;
    return 0;
}

static int try_enqueue_gap(DexRecorder *r, uint64_t device_ns, uint64_t host_ns, uint32_t cycle)
{
    uint8_t payload[16];
    DexRecorderSlot slot;
    size_t frame_len;
    size_t payload_len;

    if (r->pending_dropped == 0u) {
        return 0;
    }

    payload_len = dex_gap_payload_encode(r->dropped_frames, r->pending_dropped, cycle,
                                         payload, sizeof(payload));
    if (payload_len == 0u) {
        return -1;
    }

    frame_len = dex_frame_encode(DEX_FRAME_GAP_MARKER, 0u, r->next_seq,
                                 device_ns, host_ns, payload, (uint16_t)payload_len,
                                 slot.data, sizeof(slot.data));
    if (frame_len == 0u) {
        return -1;
    }
    slot.len = (uint16_t)frame_len;

    if (!dex_spsc_ring_push(&r->ring, &slot)) {
        return -2;
    }

    r->next_seq++;
    r->accepted_frames++;
    r->gap_markers++;
    r->bytes_accepted += slot.len;
    r->pending_dropped = 0u;
    return 0;
}

int dex_recorder_init(DexRecorder *recorder, DexRecorderSlot *storage, size_t capacity)
{
    if (recorder == NULL || storage == NULL || capacity == 0u) {
        return -1;
    }
    memset(recorder, 0, sizeof(*recorder));
    return dex_spsc_ring_init(&recorder->ring, storage, capacity, sizeof(DexRecorderSlot));
}

int dex_recorder_submit_frame(DexRecorder *recorder,
                              uint8_t type,
                              uint8_t flags,
                              uint64_t device_ns,
                              uint64_t host_ns,
                              const uint8_t *payload,
                              uint16_t payload_len)
{
    DexRecorderSlot slot;
    size_t frame_len;

    if (recorder == NULL) {
        return -1;
    }

    /* Generic frames do not carry a control-cycle number. */
    (void)try_enqueue_gap(recorder, device_ns, host_ns, 0u);

    frame_len = dex_frame_encode(type, flags, recorder->next_seq, device_ns, host_ns,
                                 payload, payload_len, slot.data, sizeof(slot.data));
    if (frame_len == 0u) {
        return -1;
    }
    slot.len = (uint16_t)frame_len;
    return enqueue_slot(recorder, &slot);
}

int dex_recorder_submit_control(DexRecorder *recorder, const DexControlSample *sample, uint64_t host_ns)
{
    uint8_t payload[DEX_FRAME_MAX_PAYLOAD];
    DexRecorderSlot slot;
    size_t payload_len;
    size_t frame_len;
    uint64_t device_ns;

    if (recorder == NULL || sample == NULL) {
        return -1;
    }

    device_ns = sample->device_ns;
    (void)try_enqueue_gap(recorder, device_ns, host_ns, sample->cycle);

    payload_len = dex_control_sample_encode(sample, payload, sizeof(payload));
    if (payload_len == 0u) {
        return -1;
    }

    frame_len = dex_frame_encode(DEX_FRAME_CONTROL_SAMPLE, 0u, recorder->next_seq,
                                 device_ns, host_ns, payload, (uint16_t)payload_len,
                                 slot.data, sizeof(slot.data));
    if (frame_len == 0u) {
        return -1;
    }
    slot.len = (uint16_t)frame_len;
    return enqueue_slot(recorder, &slot);
}

size_t dex_recorder_drain(DexRecorder *recorder,
                          DexRecordWriter writer,
                          void *user_ctx,
                          size_t max_frames)
{
    DexRecorderSlot slot;
    size_t popped = 0u;

    if (recorder == NULL || writer == NULL) {
        return 0u;
    }

    while ((max_frames == 0u || popped < max_frames) &&
           dex_spsc_ring_pop(&recorder->ring, &slot)) {
        int rc = writer(slot.data, slot.len, user_ctx);
        if (rc == 0) {
            recorder->bytes_written += slot.len;
        } else {
            recorder->backend_errors++;
        }
        popped++;
    }
    return popped;
}
