#ifndef DEX_CRC16_H
#define DEX_CRC16_H

#include <stddef.h>
#include <stdint.h>

/* CRC-16/CCITT-FALSE: poly=0x1021, init=0xFFFF, no reflection, xorout=0. */
uint16_t dex_crc16_ccitt_false(const uint8_t *data, size_t len);

/* Streaming helper for implementations that do not hold a complete frame. */
uint16_t dex_crc16_update(uint16_t crc, const uint8_t *data, size_t len);

#endif
