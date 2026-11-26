#ifndef DEX_RECORDER_H
#define DEX_RECORDER_H

#include "dexhand/dex_protocol.h"
#include "dexhand/dex_spsc_ring.h"

#define DEX_RECORDER_SLOT_CAP DEX_FRAME_MAX_SIZE

typedef struct {
    uint16_t len;
    uint8_t data[DEX_RECORDER_SLOT_CAP];
} DexRecorderSlot;

typedef int (*DexRecordWriter)(const uint8_t *frame, size_t len, void *user_ctx);

typedef struct {
    DexSpscRing ring;
    uint32_t next_seq;
    uint32_t accepted_frames;
    uint32_t dropped_frames;
    uint32_t gap_markers;
    uint32_t pending_dropped;
    uint32_t bytes_accepted;
    uint32_t bytes_written;
    uint32_t backend_errors;
} DexRecorder;

/*
 * storage must provide capacity * sizeof(DexRecorderSlot) bytes and outlive recorder.
 * A static array is preferred on MCU targets.
 */
int dex_recorder_init(DexRecorder *recorder, DexRecorderSlot *storage, size_t capacity);

/* Non-blocking from the control thread. A full buffer drops the new frame, never blocks control. */
int dex_recorder_submit_frame(DexRecorder *recorder,
                               uint8_t type,
                               uint8_t flags,
                               uint64_t device_ns,
                               uint64_t host_ns,
                               const uint8_t *payload,
                               uint16_t payload_len);

int dex_recorder_submit_control(DexRecorder *recorder, const DexControlSample *sample, uint64_t host_ns);

/* Consumer side: low-priority writer task, file flush, USB bulk transfer or network sender. */
size_t dex_recorder_drain(DexRecorder *recorder,
                          DexRecordWriter writer,
                          void *user_ctx,
                          size_t max_frames);

#endif
