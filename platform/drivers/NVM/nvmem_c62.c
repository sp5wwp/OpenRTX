/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * Retevis C62 nonvolatile memory: settings and VFO in an emulated EEPROM
 * (eeep) on the CSK6 SPI flash, as on the CS7000.
 *
 * The flash also holds the firmware (executed in place), the DSP image and
 * the OEM factory resources, calibration included. Only the first 16 KiB of
 * the devicetree's storage_partition (0x2B0000, erased in every factory
 * dump examined) is used: eeep erases its whole partition when it finds no
 * valid page, so the partition below must stay inside storage_partition.
 */

#include <string.h>
#include <zephyr/storage/flash_map.h>
#include "interfaces/nvmem.h"
#include "core/nvmem_access.h"
#include "core/crc.h"
#include "drivers/NVM/eeep.h"
#include "drivers/NVM/flash_zephyr.h"

#define C62_EEEP_OFFSET FIXED_PARTITION_OFFSET(storage_partition)
#define C62_EEEP_SIZE (16 * 1024)

BUILD_ASSERT(C62_EEEP_SIZE <= FIXED_PARTITION_SIZE(storage_partition),
             "eeep area larger than the storage partition");

/* eeep record ids, as on the CS7000 */
#define EEEP_VFO 0x0001
#define EEEP_SETTINGS 0x0002

/*
 * The flash controller that holds storage_partition (the "flash" alias
 * names the memory node, which is not a device)
 */
static struct nvmInfo eflashInfo;
static const struct nvmDevice eflash = {
    .priv = FIXED_PARTITION_DEVICE(storage_partition),
    .ops = &zephyr_flash_ops,
    .info = &eflashInfo,
};
EEEP_DEVICE_DEFINE(eeep)

static const struct nvmPartition memPartitions[] = {
    {
        .offset = C62_EEEP_OFFSET, // eeep storage
        .size = C62_EEEP_SIZE,
    },
};

static const struct nvmDescriptor memAreas[] = {
    {
        .name = "SPI flash",
        .dev = &eflash,
        .baseAddr = 0x00000000,
        .size = 0x400000, // 4 MiB
        .nbPart = ARRAY_SIZE(memPartitions),
        .partitions = memPartitions,
    },
    {
        .name = "Virtual EEPROM",
        .dev = &eeep,
        .baseAddr = 0x00000000,
        .size = 0xFFFFFFFF, // No physical size
        .nbPart = 0,
        .partitions = NULL,
    },
};

const struct nvmTable nvmTab = {
    .areas = memAreas,
    .nbAreas = ARRAY_SIZE(memAreas),
};

/* CRCs of what is stored, to skip writes of unchanged data */
static uint16_t settingsCrc;
static uint16_t vfoCrc;
static bool ready;

void nvm_init()
{
    if (zephirFlash_init(&eflash) < 0)
        return;
    /*
     * eeep writes records of any length at any address, and rewrites a
     * record header to mark it valid. The devicetree gives a 4-byte write
     * block, but the CSK6 driver programs any byte range and the NOR flash
     * clears bits of programmed bytes: allow single bytes, as on the CS7000.
     */
    eflashInfo.write_size = 1;
    ready = eeep_init(&eeep, 0, 0) == 0;
}

void nvm_terminate()
{
    eeep_terminate(&eeep);
}

void nvm_readCalibData(void *buf)
{
    (void)buf;
}

void nvm_readHwInfo(hwInfo_t *info)
{
    (void)info;
}

int nvm_readVfoChannelData(channel_t *channel)
{
    if (!ready)
        return -1;

    memset(channel, 0x00, sizeof(channel_t));
    if (nvm_read(1, 0, EEEP_VFO, channel, sizeof(channel_t)) < 0)
        return -1;

    vfoCrc = crc_ccitt(channel, sizeof(channel_t));
    return 0;
}

int nvm_readSettings(settings_t *settings)
{
    if (!ready)
        return -1;

    memset(settings, 0x00, sizeof(settings_t));
    if (nvm_read(1, 0, EEEP_SETTINGS, settings, sizeof(settings_t)) < 0)
        return -1;

    settingsCrc = crc_ccitt(settings, sizeof(settings_t));
    return 0;
}

int nvm_writeSettings(const settings_t *settings)
{
    if (!ready)
        return -1;

    uint16_t crc = crc_ccitt(settings, sizeof(settings_t));
    if (crc == settingsCrc)
        return 0;
    if (nvm_write(1, 0, EEEP_SETTINGS, settings, sizeof(settings_t)) < 0)
        return -1;

    settingsCrc = crc;
    return 0;
}

int nvm_writeSettingsAndVfo(const settings_t *settings, const channel_t *vfo)
{
    if (!ready)
        return -1;

    uint16_t crc = crc_ccitt(vfo, sizeof(channel_t));
    if (crc != vfoCrc) {
        if (nvm_write(1, 0, EEEP_VFO, vfo, sizeof(channel_t)) < 0)
            return -1;
        vfoCrc = crc;
    }

    return nvm_writeSettings(settings);
}
