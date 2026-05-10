/*
 * filum/shard/hal/stm32/hal_lora.c
 *
 * SX1276 LoRa driver for STM32 via SPI.
 * Targets: RFM95W (SX1276 based, 868/915 MHz).
 *
 * Wiring (STM32F411 Blackpill):
 *   SPI1_SCK  -> PA5
 *   SPI1_MISO -> PA6
 *   SPI1_MOSI -> PA7
 *   NSS       -> PA4
 *   DIO0      -> PB0  (TxDone/RxDone interrupt)
 *   RESET     -> PC13
 *
 * This file is a reference implementation stub.
 * Replace SPI_Transfer, GPIO_Set etc. with your HAL or LL calls.
 */

#include "hal.h"
#include <stdint.h>
#include <string.h>

/* SX1276 register map (partial) */
#define REG_FIFO                 0x00
#define REG_OP_MODE              0x01
#define REG_FRF_MSB              0x06
#define REG_FRF_MID             0x07
#define REG_FRF_LSB              0x08
#define REG_PA_CONFIG            0x09
#define REG_LNA                  0x0C
#define REG_FIFO_ADDR_PTR        0x0D
#define REG_FIFO_TX_BASE_ADDR    0x0E
#define REG_FIFO_RX_BASE_ADDR    0x0F
#define REG_FIFO_RX_CURRENT_ADDR 0x10
#define REG_IRQ_FLAGS            0x12
#define REG_RX_NB_BYTES          0x13
#define REG_MODEM_CONFIG_1       0x1D
#define REG_MODEM_CONFIG_2       0x1E
#define REG_PREAMBLE_MSB         0x20
#define REG_PREAMBLE_LSB         0x21
#define REG_PAYLOAD_LENGTH       0x22
#define REG_MODEM_CONFIG_3       0x26
#define REG_FREQ_ERROR_MSB       0x28
#define REG_DETECTION_OPTIMIZE   0x31
#define REG_DETECTION_THRESHOLD  0x37
#define REG_SYNC_WORD            0x39
#define REG_DIO_MAPPING_1        0x40
#define REG_VERSION              0x42

#define MODE_LONG_RANGE_MODE     0x80
#define MODE_SLEEP               0x00
#define MODE_STDBY               0x01
#define MODE_TX                  0x03
#define MODE_RX_CONTINUOUS       0x05
#define MODE_RX_SINGLE           0x06

#define IRQ_TX_DONE              0x08
#define IRQ_RX_DONE              0x40
#define IRQ_PAYLOAD_CRC_ERROR    0x20

/* ----- Platform-specific primitives (fill with STM32 HAL/LL calls) ----- */

static void spi_nss_low(void)  { /* HAL_GPIO_WritePin(NSS_GPIO, NSS_PIN, 0); */ }
static void spi_nss_high(void) { /* HAL_GPIO_WritePin(NSS_GPIO, NSS_PIN, 1); */ }

static uint8_t spi_transfer(uint8_t byte)
{
    /* uint8_t rx; HAL_SPI_TransmitReceive(&hspi1, &byte, &rx, 1, 100); return rx; */
    (void)byte;
    return 0;
}

static void delay_ms(uint32_t ms)
{
    /* HAL_Delay(ms); */
    (void)ms;
}

/* ----- SX1276 register access ------------------------------------------ */

static uint8_t sx1276_read(uint8_t reg)
{
    spi_nss_low();
    spi_transfer(reg & 0x7F);
    uint8_t val = spi_transfer(0x00);
    spi_nss_high();
    return val;
}

static void sx1276_write(uint8_t reg, uint8_t val)
{
    spi_nss_low();
    spi_transfer(reg | 0x80);
    spi_transfer(val);
    spi_nss_high();
}

static void sx1276_write_fifo(const uint8_t *buf, uint8_t len)
{
    spi_nss_low();
    spi_transfer(REG_FIFO | 0x80);
    for (uint8_t i = 0; i < len; i++) spi_transfer(buf[i]);
    spi_nss_high();
}

static void sx1276_read_fifo(uint8_t *buf, uint8_t len)
{
    spi_nss_low();
    spi_transfer(REG_FIFO & 0x7F);
    for (uint8_t i = 0; i < len; i++) buf[i] = spi_transfer(0x00);
    spi_nss_high();
}

/* ----- Init ---------------------------------------------------------------- */

static void sx1276_set_frequency(uint32_t freq_hz)
{
    uint64_t frf = ((uint64_t)freq_hz << 19) / 32000000ULL;
    sx1276_write(REG_FRF_MSB, (uint8_t)(frf >> 16));
    sx1276_write(REG_FRF_MID, (uint8_t)(frf >>  8));
    sx1276_write(REG_FRF_LSB, (uint8_t)(frf >>  0));
}

void hal_lora_init(uint32_t freq_hz, uint8_t sf, uint8_t tx_power_dbm)
{
    /* Reset */
    /* HAL_GPIO_WritePin(RESET_GPIO, RESET_PIN, 0); */
    delay_ms(10);
    /* HAL_GPIO_WritePin(RESET_GPIO, RESET_PIN, 1); */
    delay_ms(10);

    /* Confirm chip version */
    /* uint8_t ver = sx1276_read(REG_VERSION); assert(ver == 0x12); */

    /* Enter sleep to configure LoRa mode */
    sx1276_write(REG_OP_MODE, MODE_LONG_RANGE_MODE | MODE_SLEEP);
    delay_ms(10);

    /* FIFO base addresses */
    sx1276_write(REG_FIFO_TX_BASE_ADDR, 0x00);
    sx1276_write(REG_FIFO_RX_BASE_ADDR, 0x00);

    /* LNA max gain, AGC on */
    sx1276_write(REG_LNA, 0x23);
    sx1276_write(REG_MODEM_CONFIG_3, 0x04);

    /* Modem config: BW=125kHz, CR=4/5, explicit header */
    sx1276_write(REG_MODEM_CONFIG_1, 0x72);

    /* SF and CRC */
    sx1276_write(REG_MODEM_CONFIG_2, (uint8_t)((sf << 4) | 0x04));

    /* Sync word (0x12 = LoRaWAN private, 0x34 = public) */
    sx1276_write(REG_SYNC_WORD, 0x12);

    /* Preamble length = 8 */
    sx1276_write(REG_PREAMBLE_MSB, 0x00);
    sx1276_write(REG_PREAMBLE_LSB, 0x08);

    /* TX power */
    if (tx_power_dbm > 17) tx_power_dbm = 17;
    sx1276_write(REG_PA_CONFIG, (uint8_t)(0x80 | (tx_power_dbm - 2)));

    sx1276_set_frequency(freq_hz);

    /* Standby */
    sx1276_write(REG_OP_MODE, MODE_LONG_RANGE_MODE | MODE_STDBY);
}

/* ----- HAL functions ------------------------------------------------------- */

int hal_lora_send(const uint8_t *buf, uint8_t len)
{
    sx1276_write(REG_OP_MODE, MODE_LONG_RANGE_MODE | MODE_STDBY);
    sx1276_write(REG_FIFO_ADDR_PTR, 0x00);
    sx1276_write_fifo(buf, len);
    sx1276_write(REG_PAYLOAD_LENGTH, len);

    /* DIO0 = TxDone */
    sx1276_write(REG_DIO_MAPPING_1, 0x40);
    sx1276_write(REG_OP_MODE, MODE_LONG_RANGE_MODE | MODE_TX);

    /* Poll for TxDone (blocking; replace with interrupt for production) */
    uint32_t timeout = 5000;
    while (timeout--) {
        delay_ms(1);
        if (sx1276_read(REG_IRQ_FLAGS) & IRQ_TX_DONE) break;
    }
    sx1276_write(REG_IRQ_FLAGS, IRQ_TX_DONE);
    return (timeout > 0) ? 0 : -1;
}

int hal_lora_recv(uint8_t *buf, uint8_t *len_out, uint32_t timeout_ms)
{
    sx1276_write(REG_FIFO_ADDR_PTR, sx1276_read(REG_FIFO_RX_BASE_ADDR));
    sx1276_write(REG_OP_MODE, MODE_LONG_RANGE_MODE | MODE_RX_SINGLE);

    while (timeout_ms--) {
        delay_ms(1);
        uint8_t flags = sx1276_read(REG_IRQ_FLAGS);
        if (flags & IRQ_RX_DONE) {
            if (flags & IRQ_PAYLOAD_CRC_ERROR) {
                sx1276_write(REG_IRQ_FLAGS, 0xFF);
                return -1;
            }
            *len_out = sx1276_read(REG_RX_NB_BYTES);
            sx1276_write(REG_FIFO_ADDR_PTR,
                         sx1276_read(REG_FIFO_RX_CURRENT_ADDR));
            sx1276_read_fifo(buf, *len_out);
            sx1276_write(REG_IRQ_FLAGS, 0xFF);
            return 0;
        }
    }
    return 1; /* timeout */
}

int hal_lora_set_sf(uint8_t sf)
{
    if (sf < 7 || sf > 12) return -1;
    uint8_t cfg2 = sx1276_read(REG_MODEM_CONFIG_2);
    cfg2 = (uint8_t)((cfg2 & 0x0F) | (sf << 4));
    sx1276_write(REG_MODEM_CONFIG_2, cfg2);
    return 0;
}
