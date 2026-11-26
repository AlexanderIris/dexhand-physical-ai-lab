#include "dexhand/dex_crc16.h"

uint16_t dex_crc16_update(uint16_t crc, const uint8_t *data, size_t len)
{
    size_t i;
    uint8_t bit;

    if (data == NULL) {
        return crc;
    }

    for (i = 0; i < len; ++i) {
        crc ^= (uint16_t)((uint16_t)data[i] << 8);
        for (bit = 0; bit < 8u; ++bit) {
            if ((crc & 0x8000u) != 0u) {
                crc = (uint16_t)((crc << 1) ^ 0x1021u);
            } else {
                crc = (uint16_t)(crc << 1);
            }
        }
    }
    return crc;
}

uint16_t dex_crc16_ccitt_false(const uint8_t *data, size_t len)
{
    return dex_crc16_update(0xFFFFu, data, len);
}
