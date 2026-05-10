/*
 * filum/examples/shard_stm32f4/main.c
 *
 * Filum Shard reference for STM32F411 Blackpill + RFM95W.
 *
 * Customize:
 *   1. Fill SPI/GPIO calls in hal_lora.c
 *   2. Fill hal_rtc.c / hal_power.c / hal_nvs.c
 *   3. Implement fl_user_data_cb() with real sensor reads
 *   4. Adjust MODEL_* defines for your task
 */

#include "fl_shard.h"
#include "fl_model.h"
#include "fl_train.h"
#include "hal.h"
#include <stdint.h>

#define SHARD_ID           0x0001
#define LORA_FREQ_HZ       868000000UL
#define LORA_SF            7
#define LORA_TX_POWER_DBM  17

#define MODEL_IN    8
#define MODEL_HID   16
#define MODEL_OUT   2

/* --- HAL forward declarations (implemented in hal_*.c) --- */
extern void     hal_lora_init(uint32_t freq_hz, uint8_t sf, uint8_t tx_dbm);
extern int      hal_lora_send(const uint8_t *buf, uint8_t len);
extern int      hal_lora_recv(uint8_t *buf, uint8_t *len, uint32_t timeout_ms);
extern void     hal_rtc_init(void);
extern void     hal_power_init(void);
extern void     hal_deep_sleep_rtc(uint32_t seconds);
extern uint32_t hal_get_tick_ms(void);
extern void     hal_sleep_ms(uint32_t ms);
extern int      hal_nvs_write(uint32_t offset, const void *data, size_t len);
extern int      hal_nvs_read(uint32_t offset, void *data, size_t len);

static const FLHal g_hal = {
    .lora_send      = hal_lora_send,
    .lora_recv      = hal_lora_recv,
    .lora_set_sf    = NULL,
    .sleep_ms       = hal_sleep_ms,
    .deep_sleep_rtc = hal_deep_sleep_rtc,
    .get_tick_ms    = hal_get_tick_ms,
    .nvs_write      = hal_nvs_write,
    .nvs_read       = hal_nvs_read,
    .log            = NULL,
};

static FLModel g_model;
static FLShard g_shard;

static const FLLayerDesc g_layers[] = {
    { .type=FL_LAYER_LINEAR, .activation=FL_ACT_RELU,
      .in_features=MODEL_IN,  .out_features=MODEL_HID,
      .param_count=MODEL_IN * MODEL_HID + MODEL_HID },
    { .type=FL_LAYER_LINEAR, .activation=FL_ACT_SIGMOID,
      .in_features=MODEL_HID, .out_features=MODEL_OUT,
      .param_count=MODEL_HID * MODEL_OUT + MODEL_OUT },
};

/* --- Sensor data callback: CUSTOMIZE THIS --- */
#define MAX_SAMPLES 50
static uint16_t g_sample_idx = 0;

/* Placeholder dataset - replace with ADC/I2C reads */
static const float g_data[MAX_SAMPLES][MODEL_IN + MODEL_OUT] = {
    { 0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f, 0.8f,  1.0f, 0.0f },
    { 0.9f, 0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f,  0.0f, 1.0f },
};

int fl_user_data_cb(void *ctx, FLSample *s)
{
    (void)ctx;
    if (g_sample_idx >= MAX_SAMPLES) { g_sample_idx = 0; return 0; }
    const float *row = g_data[g_sample_idx++];
    for (uint8_t i = 0; i < MODEL_IN;  i++) s->input[i] = row[i];
    for (uint8_t i = 0; i < MODEL_OUT; i++) s->label[i] = row[MODEL_IN + i];
    s->input_len = MODEL_IN;
    s->label_len = MODEL_OUT;
    return 1;
}

int main(void)
{
    /* HAL_Init(); SystemClock_Config(); GPIO/SPI init here */

    hal_rtc_init();
    hal_power_init();
    hal_lora_init(LORA_FREQ_HZ, LORA_SF, LORA_TX_POWER_DBM);

    fl_model_init(&g_model, g_layers, 2);
    fl_model_init_random(&g_model, SHARD_ID);

    fl_shard_init(&g_shard, &g_hal, SHARD_ID, &g_model);

    for (;;) {
        fl_shard_tick(&g_shard);
        fl_shard_sleep(&g_shard);
    }
}
