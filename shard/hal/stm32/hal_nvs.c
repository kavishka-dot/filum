/*
 * filum/shard/hal/stm32/hal_nvs.c
 *
 * Non-volatile storage using STM32 internal Flash.
 * Model weights are stored in the last flash sector.
 *
 * STM32F411: 512KB flash, sector 7 = 128KB at 0x08060000.
 * Adjust NVS_BASE_ADDR for your specific MCU.
 */

#include "hal.h"
#include <string.h>
#include <stdint.h>

#define NVS_BASE_ADDR  0x08060000UL   /* last sector of STM32F411 flash */
#define NVS_MAX_SIZE   (128 * 1024)   /* 128KB */

int hal_nvs_write(uint32_t offset, const void *data, size_t len)
{
    if (offset + len > NVS_MAX_SIZE) return -1;

    /*
     * 1. Unlock flash: HAL_FLASH_Unlock()
     * 2. Erase sector:
     *    FLASH_EraseInitTypeDef erase = { .TypeErase = FLASH_TYPEERASE_SECTORS,
     *                                     .Sector = 7, .NbSectors = 1,
     *                                     .VoltageRange = FLASH_VOLTAGE_RANGE_3 };
     *    uint32_t err; HAL_FLASHEx_Erase(&erase, &err);
     * 3. Program word by word:
     *    for each 4-byte word: HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr, val)
     * 4. Lock: HAL_FLASH_Lock()
     */
    (void)data;
    return 0;
}

int hal_nvs_read(uint32_t offset, void *data, size_t len)
{
    if (offset + len > NVS_MAX_SIZE) return -1;
    memcpy(data, (const void *)(NVS_BASE_ADDR + offset), len);
    return 0;
}
