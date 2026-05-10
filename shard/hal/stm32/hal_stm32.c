/*
 * filum - hal/stm32/hal_stm32.c
 * STM32F4 HAL implementation targeting STM32F411 Blackpill.
 *
 * Dependencies:
 *   - STM32 HAL (STM32CubeF4)
 *   - SX1276 driver (sx1276.c — include your LoRa driver here)
 *   - RTC configured with LSE 32.768kHz crystal
 *
 * Connections assumed (SPI1):
 *   PA5 → SX1276 SCK
 *   PA6 → SX1276 MISO
 *   PA7 → SX1276 MOSI
 *   PA4 → SX1276 NSS
 *   PB0 → SX1276 RESET
 *   PA1 → SX1276 DIO0 (TX done / RX done interrupt)
 *
 * NVS: uses last 8KB of flash (sector 7 on STM32F411).
 * Ensure your linker script excludes this region from code.
 */

#include "hal_stm32.h"
#include <string.h>
#include <stdio.h>

/* ── STM32 Cube includes (adjust path for your project) ─────────────────── */
/* #include "stm32f4xx_hal.h"  */
/* #include "sx1276.h"         */

/* ── LoRa configuration ────────────────────────────────────────────────── */

#define LORA_FREQUENCY      868000000UL /* EU868 MHz                        */
#define LORA_SF             7           /* spreading factor (7-12)          */
#define LORA_BW             0           /* 125 kHz                          */
#define LORA_CR             1           /* 4/5 coding rate                  */
#define LORA_PREAMBLE       8
#define LORA_TX_POWER       14          /* dBm                              */

/* ── NVS flash address ─────────────────────────────────────────────────── */
/* STM32F411: sector 7 starts at 0x08060000, size 128KB                     */
#define NVS_FLASH_ADDR      0x08060000UL
#define NVS_FLASH_SIZE      (8 * 1024)

/* ── Duty cycle tracking ───────────────────────────────────────────────── */

static uint32_t s_last_tx_time_ms = 0;
static uint32_t s_duty_cycle_gap_ms = 10000; /* 1% on a 1s airtime budget  */

/* ── HAL functions ─────────────────────────────────────────────────────── */

static int stm32_lora_send(const uint8_t *buf, uint8_t len)
{
    /* enforce duty cycle */
    uint32_t now = HAL_GetTick();
    uint32_t since_last = now - s_last_tx_time_ms;
    if (since_last < s_duty_cycle_gap_ms)
        HAL_Delay(s_duty_cycle_gap_ms - since_last);

    /*
     * SX1276 send — replace with your actual SX1276 driver call.
     * Example using a generic SX1276 API:
     *
     * SX1276SetTxConfig(MODEM_LORA, LORA_TX_POWER, 0,
     *                   LORA_BW, LORA_SF, LORA_CR,
     *                   LORA_PREAMBLE, false, true, 0, 0, false, 3000);
     * SX1276Send((uint8_t*)buf, len);
     * while (SX1276GetStatus() != RF_IDLE) { /* wait for TX done * / }
     */

    /* stub — replace with real driver call */
    (void)buf; (void)len;

    s_last_tx_time_ms = HAL_GetTick();
    return FL_HAL_OK;
}

static int stm32_lora_recv(uint8_t *buf, uint8_t *len, uint32_t timeout_ms)
{
    /*
     * SX1276 receive with timeout:
     *
     * SX1276SetRxConfig(MODEM_LORA, LORA_BW, LORA_SF, LORA_CR,
     *                   0, LORA_PREAMBLE, 5, false, 0, true, 0, 0, false, true);
     * SX1276SetRx(timeout_ms);
     * uint32_t deadline = HAL_GetTick() + timeout_ms;
     * while (HAL_GetTick() < deadline) {
     *     if (SX1276GetStatus() == RF_RX_RUNNING) {
     *         // packet received via DIO0 interrupt
     *         *len = SX1276ReadFifo(buf);
     *         return FL_HAL_OK;
     *     }
     *     HAL_Delay(10);
     * }
     */

    /* stub */
    (void)buf; (void)len; (void)timeout_ms;
    HAL_Delay(timeout_ms > 1000 ? 1000 : timeout_ms);
    return FL_HAL_TIMEOUT;
}

static void stm32_sleep_ms(uint32_t ms)
{
    HAL_Delay(ms);
}

static void stm32_deep_sleep_until_rtc(uint32_t seconds)
{
    /*
     * Configure RTC wakeup timer and enter STOP mode.
     * STM32F4 exits STOP mode on RTC wakeup interrupt.
     *
     * HAL_RTCEx_SetWakeUpTimer_IT(&hrtc, seconds, RTC_WAKEUPCLOCK_CK_SPRE_16BITS);
     * HAL_PWR_EnterSTOPMode(PWR_LOWPOWERREGULATOR_ON, PWR_STOPENTRY_WFI);
     * SystemClock_Config();  // reconfigure PLL after STOP exit
     */

    /* stub — just delay */
    (void)seconds;
    HAL_Delay(1000);
}

static uint32_t stm32_get_tick_ms(void)
{
    return HAL_GetTick();
}

static int stm32_nvs_write(uint32_t offset, const void *data, size_t len)
{
    if (offset + len > NVS_FLASH_SIZE) return FL_HAL_ERR;

    /*
     * STM32F4 flash write requires:
     * 1. Unlock flash
     * 2. Erase sector (if first write after erase)
     * 3. Program word by word
     * 4. Lock flash
     *
     * HAL_FLASH_Unlock();
     * FLASH_EraseInitTypeDef erase = {
     *     .TypeErase = FLASH_TYPEERASE_SECTORS,
     *     .Sector = FLASH_SECTOR_7,
     *     .NbSectors = 1,
     *     .VoltageRange = FLASH_VOLTAGE_RANGE_3
     * };
     * uint32_t error;
     * HAL_FLASHEx_Erase(&erase, &error);
     * for (size_t i = 0; i < len; i += 4) {
     *     uint32_t word;
     *     memcpy(&word, (uint8_t*)data + i, 4);
     *     HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,
     *                       NVS_FLASH_ADDR + offset + i, word);
     * }
     * HAL_FLASH_Lock();
     */

    (void)offset; (void)data; (void)len;
    return FL_HAL_OK;
}

static int stm32_nvs_read(uint32_t offset, void *data, size_t len)
{
    if (offset + len > NVS_FLASH_SIZE) return FL_HAL_ERR;
    memcpy(data, (const void *)(NVS_FLASH_ADDR + offset), len);
    return FL_HAL_OK;
}

static char s_log_buf[128];
static void stm32_debug_log(const char *msg)
{
    /* route to UART — assumes huart2 is your debug UART */
    /*
     * HAL_UART_Transmit(&huart2, (uint8_t*)msg,
     *                   strlen(msg), HAL_MAX_DELAY);
     * HAL_UART_Transmit(&huart2, (uint8_t*)"\r\n", 2, HAL_MAX_DELAY);
     */
    (void)msg;
    (void)s_log_buf;
}

/* ── HAL instance ──────────────────────────────────────────────────────── */

static const FLHal s_stm32_hal = {
    .lora_send           = stm32_lora_send,
    .lora_recv           = stm32_lora_recv,
    .sleep_ms            = stm32_sleep_ms,
    .deep_sleep_until_rtc = stm32_deep_sleep_until_rtc,
    .get_tick_ms         = stm32_get_tick_ms,
    .nvs_write           = stm32_nvs_write,
    .nvs_read            = stm32_nvs_read,
    .debug_log           = stm32_debug_log,
};

const FLHal *fl_hal_stm32_get(void)
{
    return &s_stm32_hal;
}

/* ── Duty cycle configuration ──────────────────────────────────────────── */

void fl_hal_stm32_set_duty_gap_ms(uint32_t gap_ms)
{
    s_duty_cycle_gap_ms = gap_ms;
}
