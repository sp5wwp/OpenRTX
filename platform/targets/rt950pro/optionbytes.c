/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <errno.h>
#include <soc.h>
#include <stdint.h>
#include <zephyr/irq.h>
#include "interfaces/platform.h"
#include "optionbytes.h"

/*
 * MCU option bytes (user system data, USD): 24 halfwords at 0x1FFF_F800, each
 * a byte and its inverse (AT32F403A/407 reference manual v2.07, user system
 * data table, and section 5.5).
 *
 * The only option byte changed here is EOPB0 (bit 0 clear: 224 kB of SRAM,
 * section 2.3). Changing it means erasing the whole USD block and writing it
 * back. The radios come with Flash access protection enabled (FAP erased,
 * 0xFF), which leaves the code running but stops debuggers from reading the
 * Flash. That state is kept: the FAP halfword is never written, as writing
 * 0xA5 there would disable the protection and mass-erase the Flash, the
 * bootloader included. On a protected part flash_user_system_data_erase()
 * leaves FAP erased, and flash_user_system_data_program() refuses any FAP
 * value but 0xA5 (Artery HAL, at32f403a_407_flash.c).
 */

#define USD_ADDR 0x1FFFF800UL
#define USD_HALFWORDS 24
#define USD_FAP_IDX 0
#define USD_EOPB0_IDX 8
#define EOPB0_SRAM_224K 0xFE
/* Lowest battery voltage for the operation: an interruption is unwanted */
#define MIN_VBAT_MV 6800

static inline uint16_t usdRead(size_t idx)
{
    return ((const volatile uint16_t *)USD_ADDR)[idx];
}

static inline uint16_t withInverse(uint8_t value)
{
    return (uint16_t)value | ((uint16_t)(uint8_t)~value << 8);
}

bool optionbytes_sram224()
{
    return (usdRead(USD_EOPB0_IDX) & 0x01) == 0;
}

int optionbytes_setSram224()
{
    uint16_t usd[USD_HALFWORDS];

    if (optionbytes_sram224())
        return 0;
    if (platform_getVbat() < MIN_VBAT_MV)
        return -EAGAIN;

    for (size_t i = 0; i < USD_HALFWORDS; i++)
        usd[i] = usdRead(i);

    /* Expected content: the same, EOPB0 changed, FAP still erased */
    usd[USD_EOPB0_IDX] = withInverse(EOPB0_SRAM_224K);
    if (flash_fap_status_get() == RESET)
        return -EPERM; /* Unprotected part: not the case handled here */

    unsigned int key = irq_lock();
    flash_unlock();

    int ret = 0;
    if (flash_user_system_data_erase() != FLASH_OPERATE_DONE) {
        ret = -EIO;
        goto out;
    }

    for (size_t i = 0; i < USD_HALFWORDS; i++) {
        if (i == USD_FAP_IDX)
            continue;

        uint32_t addr = USD_ADDR + (2 * i);
        if (flash_user_system_data_program(addr, (uint8_t)usd[i])
            != FLASH_OPERATE_DONE) {
            ret = -EIO;
            goto out;
        }
    }

out:
    flash_lock();
    irq_unlock(key);

    if (ret < 0)
        return ret;

    /* FAP must read back erased (protected), everything else as expected */
    if (usdRead(USD_FAP_IDX) != 0xFFFF)
        return -EFAULT;
    for (size_t i = 1; i < USD_HALFWORDS; i++) {
        if (usdRead(i) != usd[i])
            return -EFAULT;
    }

    return 0;
}
