#ifndef DEX_PROTOCOL_H
#define DEX_PROTOCOL_H

#include "dexhand/dex_types.h"

#define DEX_FRAME_MAGIC0 0xA5u
#define DEX_FRAME_MAGIC1 0x5Au
#define DEX_FRAME_VERSION 1u
#define DEX_FRAME_HEADER_SIZE 28u
#define DEX_FRAME_CRC_SIZE 2u
#define DEX_FRAME_MAX_PAYLOAD 512u
#define DEX_FRAME_MAX_SIZE (DEX_FRAME_HEADER_SIZE + DEX_FRAME_MAX_PAYLOAD + DEX_FRAME_CRC_SIZE)

typedef enum {
    DEX_FRAME_SESSION_METADATA = 0x01,
    DEX_FRAME_CONTROL_SAMPLE = 0x10,
    DEX_FRAME_SENSOR_BATCH = 0x11,
    DEX_FRAME_EVENT = 0x20,
    DEX_FRAME_CLOCK_SYNC = 0x21,
    DEX_FRAME_GAP_MARKER = 0x30
} DexFrameType;

typedef struct {
    uint8_t type;
    uint8_t flags;
    uint32_t seq;
    uint64_t device_ns;
    uint64_t host_ns;
    uint16_t payload_len;
    const uint8_t *payload;
} DexFrameView;

/* Returns encoded length, or 0 on invalid arguments/insufficient capacity. */
size_t dex_frame_encode(uint8_t type,
                        uint8_t flags,
                        uint32_t seq,
                        uint64_t device_ns,
                        uint64_t host_ns,
                        const uint8_t *payload,
                        uint16_t payload_len,
                        uint8_t *out,
                        size_t out_cap);

/*
 * Returns 0 when at least a complete frame header is available.
 *   *total_len == 0     : need more bytes
 *   *total_len == SIZE_MAX: invalid header/magic/length
 *   otherwise           : expected complete frame length
 */
int dex_frame_peek_total_length(const uint8_t *buffer, size_t available, size_t *total_len);

/* Validates one complete frame and exposes a payload pointer inside buffer. */
int dex_frame_decode(const uint8_t *frame, size_t frame_len, DexFrameView *view);

/* Compact v1 control-sample payload codec. */
size_t dex_control_sample_payload_size(uint8_t joint_count);
size_t dex_control_sample_encode(const DexControlSample *sample, uint8_t *out, size_t out_cap);
int dex_control_sample_decode(const uint8_t *payload, size_t len, DexControlSample *out);

size_t dex_event_payload_encode(uint16_t event_code,
                                uint8_t severity,
                                uint32_t cycle,
                                const char *message,
                                uint8_t *out,
                                size_t out_cap);

size_t dex_gap_payload_encode(uint32_t dropped_frames,
                              uint32_t pending_dropped,
                              uint32_t cycle,
                              uint8_t *out,
                              size_t out_cap);

#endif
