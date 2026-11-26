#include "dexhand/dex_protocol.h"

#include <string.h>

#include "dexhand/dex_crc16.h"

#define DEX_CONTROL_PAYLOAD_VERSION 1u
#define DEX_EVENT_FIXED_SIZE 8u
#define DEX_GAP_PAYLOAD_SIZE 16u

static void put_u16_le(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
}

static void put_u32_le(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static void put_u64_le(uint8_t *p, uint64_t v)
{
    unsigned i;
    for (i = 0; i < 8u; ++i) {
        p[i] = (uint8_t)((v >> (8u * i)) & 0xFFu);
    }
}

static uint16_t get_u16_le(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t get_u32_le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t get_u64_le(const uint8_t *p)
{
    uint64_t v = 0;
    unsigned i;
    for (i = 0; i < 8u; ++i) {
        v |= ((uint64_t)p[i]) << (8u * i);
    }
    return v;
}

size_t dex_frame_encode(uint8_t type,
                        uint8_t flags,
                        uint32_t seq,
                        uint64_t device_ns,
                        uint64_t host_ns,
                        const uint8_t *payload,
                        uint16_t payload_len,
                        uint8_t *out,
                        size_t out_cap)
{
    size_t total;
    uint16_t crc;

    if (out == NULL || payload_len > DEX_FRAME_MAX_PAYLOAD) {
        return 0u;
    }
    if (payload_len > 0u && payload == NULL) {
        return 0u;
    }

    total = DEX_FRAME_HEADER_SIZE + payload_len + DEX_FRAME_CRC_SIZE;
    if (out_cap < total) {
        return 0u;
    }

    out[0] = DEX_FRAME_MAGIC0;
    out[1] = DEX_FRAME_MAGIC1;
    out[2] = DEX_FRAME_VERSION;
    out[3] = type;
    out[4] = flags;
    out[5] = 0u;
    put_u32_le(out + 6, seq);
    put_u64_le(out + 10, device_ns);
    put_u64_le(out + 18, host_ns);
    put_u16_le(out + 26, payload_len);

    if (payload_len > 0u) {
        memcpy(out + DEX_FRAME_HEADER_SIZE, payload, payload_len);
    }

    crc = dex_crc16_ccitt_false(out, DEX_FRAME_HEADER_SIZE + payload_len);
    put_u16_le(out + DEX_FRAME_HEADER_SIZE + payload_len, crc);
    return total;
}

int dex_frame_peek_total_length(const uint8_t *buffer, size_t available, size_t *total_len)
{
    uint16_t payload_len;

    if (total_len == NULL) {
        return -1;
    }
    *total_len = 0u;
    if (buffer == NULL) {
        return -1;
    }
    if (available < DEX_FRAME_HEADER_SIZE) {
        return 0;
    }
    if (buffer[0] != DEX_FRAME_MAGIC0 || buffer[1] != DEX_FRAME_MAGIC1 ||
        buffer[2] != DEX_FRAME_VERSION) {
        *total_len = SIZE_MAX;
        return -1;
    }

    payload_len = get_u16_le(buffer + 26);
    if (payload_len > DEX_FRAME_MAX_PAYLOAD) {
        *total_len = SIZE_MAX;
        return -1;
    }

    *total_len = DEX_FRAME_HEADER_SIZE + payload_len + DEX_FRAME_CRC_SIZE;
    return 0;
}

int dex_frame_decode(const uint8_t *frame, size_t frame_len, DexFrameView *view)
{
    size_t expected;
    uint16_t stored_crc;
    uint16_t calc_crc;

    if (frame == NULL || view == NULL || frame_len < DEX_FRAME_HEADER_SIZE) {
        return -1;
    }
    if (dex_frame_peek_total_length(frame, frame_len, &expected) != 0 ||
        expected == SIZE_MAX || frame_len != expected) {
        return -2;
    }

    stored_crc = get_u16_le(frame + frame_len - DEX_FRAME_CRC_SIZE);
    calc_crc = dex_crc16_ccitt_false(frame, frame_len - DEX_FRAME_CRC_SIZE);
    if (stored_crc != calc_crc) {
        return -3;
    }

    memset(view, 0, sizeof(*view));
    view->type = frame[3];
    view->flags = frame[4];
    view->seq = get_u32_le(frame + 6);
    view->device_ns = get_u64_le(frame + 10);
    view->host_ns = get_u64_le(frame + 18);
    view->payload_len = get_u16_le(frame + 26);
    view->payload = (view->payload_len > 0u) ? frame + DEX_FRAME_HEADER_SIZE : NULL;
    return 0;
}

size_t dex_control_sample_payload_size(uint8_t joint_count)
{
    if (joint_count > DEXHAND_MAX_JOINTS) {
        return 0u;
    }
    return 16u + ((size_t)joint_count * 20u);
}

size_t dex_control_sample_encode(const DexControlSample *sample, uint8_t *out, size_t out_cap)
{
    size_t expected;
    size_t off = 0;
    uint8_t i;

    if (sample == NULL || out == NULL || sample->joint_count > DEXHAND_MAX_JOINTS) {
        return 0u;
    }
    expected = dex_control_sample_payload_size(sample->joint_count);
    if (expected == 0u || out_cap < expected) {
        return 0u;
    }

    out[off++] = DEX_CONTROL_PAYLOAD_VERSION;
    out[off++] = sample->joint_count;
    out[off++] = sample->safety_level;
    out[off++] = sample->control_mode;
    put_u32_le(out + off, sample->cycle); off += 4;
    put_u16_le(out + off, sample->cycle_budget_us); off += 2;
    put_u16_le(out + off, sample->cycle_time_us); off += 2;
    put_u32_le(out + off, sample->recorder_dropped); off += 4;

    for (i = 0; i < sample->joint_count; ++i) {
        put_u32_le(out + off, (uint32_t)sample->target_mrad[i]); off += 4;
        put_u32_le(out + off, (uint32_t)sample->joints[i].position_mrad); off += 4;
        put_u32_le(out + off, (uint32_t)sample->joints[i].velocity_mrad_s); off += 4;
        put_u32_le(out + off, (uint32_t)sample->joints[i].current_ma); off += 4;
        put_u16_le(out + off, (uint16_t)sample->joints[i].temperature_cdeg); off += 2;
        put_u16_le(out + off, sample->joints[i].flags); off += 2;
    }

    if (off != expected) {
        return 0u;
    }
    return off;
}

int dex_control_sample_decode(const uint8_t *payload, size_t len, DexControlSample *out)
{
    uint8_t count;
    size_t expected;
    size_t off = 0;
    uint8_t i;

    if (payload == NULL || out == NULL || len < 16u) {
        return -1;
    }
    if (payload[0] != DEX_CONTROL_PAYLOAD_VERSION) {
        return -2;
    }
    count = payload[1];
    expected = dex_control_sample_payload_size(count);
    if (expected == 0u || len != expected) {
        return -3;
    }

    memset(out, 0, sizeof(*out));
    out->joint_count = count;
    out->safety_level = payload[2];
    out->control_mode = payload[3];
    out->cycle = get_u32_le(payload + 4);
    out->cycle_budget_us = get_u16_le(payload + 8);
    out->cycle_time_us = get_u16_le(payload + 10);
    out->recorder_dropped = get_u32_le(payload + 12);
    off = 16;

    for (i = 0; i < count; ++i) {
        out->target_mrad[i] = (int32_t)get_u32_le(payload + off); off += 4;
        out->joints[i].position_mrad = (int32_t)get_u32_le(payload + off); off += 4;
        out->joints[i].velocity_mrad_s = (int32_t)get_u32_le(payload + off); off += 4;
        out->joints[i].current_ma = (int32_t)get_u32_le(payload + off); off += 4;
        out->joints[i].temperature_cdeg = (int16_t)get_u16_le(payload + off); off += 2;
        out->joints[i].flags = get_u16_le(payload + off); off += 2;
        out->applied_current_ma[i] = out->joints[i].current_ma;
    }
    return 0;
}

size_t dex_event_payload_encode(uint16_t event_code,
                                uint8_t severity,
                                uint32_t cycle,
                                const char *message,
                                uint8_t *out,
                                size_t out_cap)
{
    size_t msg_len = 0;
    size_t total;

    if (out == NULL) {
        return 0u;
    }
    if (message != NULL) {
        msg_len = strnlen(message, DEX_FRAME_MAX_PAYLOAD - DEX_EVENT_FIXED_SIZE);
    }
    total = DEX_EVENT_FIXED_SIZE + msg_len;
    if (out_cap < total || total > DEX_FRAME_MAX_PAYLOAD) {
        return 0u;
    }

    put_u16_le(out, event_code);
    out[2] = severity;
    out[3] = 0u;
    put_u32_le(out + 4, cycle);
    if (msg_len > 0u) {
        memcpy(out + DEX_EVENT_FIXED_SIZE, message, msg_len);
    }
    return total;
}

size_t dex_gap_payload_encode(uint32_t dropped_frames,
                              uint32_t pending_dropped,
                              uint32_t cycle,
                              uint8_t *out,
                              size_t out_cap)
{
    if (out == NULL || out_cap < DEX_GAP_PAYLOAD_SIZE) {
        return 0u;
    }
    put_u32_le(out, dropped_frames);
    put_u32_le(out + 4, pending_dropped);
    put_u32_le(out + 8, cycle);
    put_u32_le(out + 12, 0u);
    return DEX_GAP_PAYLOAD_SIZE;
}
