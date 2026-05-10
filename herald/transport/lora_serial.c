/*
 * filum/herald/transport/lora_serial.c
 *
 * Thin shim translating between Herald frames and the serial protocol
 * spoken by common LoRa gateways (RAK811, RFM95 AT-command mode,
 * or raw UART transparent mode).
 *
 * Transparent mode: length-prefixed raw frame.
 * AT mode: hex-encoded AT+SEND command.
 *
 * Set FILUM_LORA_AT_MODE=1 at compile time to enable AT mode.
 */

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

#ifndef FILUM_LORA_AT_MODE
#define FILUM_LORA_AT_MODE 0
#endif

static void bytes_to_hex(const uint8_t *in, size_t len, char *out)
{
    static const char hex[] = "0123456789ABCDEF";
    for (size_t i = 0; i < len; i++) {
        out[i * 2]     = hex[in[i] >> 4];
        out[i * 2 + 1] = hex[in[i] & 0x0F];
    }
    out[len * 2] = '\0';
}

int lora_serial_send(int fd, const uint8_t *frame, uint8_t frame_len)
{
#if FILUM_LORA_AT_MODE
    char hex[512];
    bytes_to_hex(frame, frame_len, hex);
    char cmd[600];
    int  n = snprintf(cmd, sizeof(cmd), "AT+SEND=1,%d,%s\r\n", frame_len, hex);
    return (write(fd, cmd, (size_t)n) == n) ? 0 : -1;
#else
    uint8_t hdr = frame_len;
    if (write(fd, &hdr, 1) != 1)             return -1;
    if (write(fd, frame, frame_len) != frame_len) return -1;
    return 0;
#endif
}
