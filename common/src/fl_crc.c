/*
 * filum/common/src/fl_crc.c
 * CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF, no reflection)
 */

#include <stdint.h>
#include <stddef.h>

uint16_t fl_crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int b = 0; b < 8; b++) {
            if (crc & 0x8000)
                crc = (uint16_t)((crc << 1) ^ 0x1021);
            else
                crc <<= 1;
        }
    }
    return crc;
}
