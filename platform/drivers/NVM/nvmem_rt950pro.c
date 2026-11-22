/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stddef.h>
#include <zephyr/device.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/printk.h>
#include "interfaces/nvmem.h"
#include "core/crc.h"
#include "calibration/calibInfo_RT950Pro.h"

/*
 * Settings and VFO data are kept in separate erase sectors of the storage
 * partition, so that updating one record does not force an erase of the
 * other. Each record carries a magic value and a CRC16 to detect both erased
 * and corrupted storage. 2x 4kB of storage are reserved for settings and VFO data.
 */
#define SECTOR_SIZE 0x1000
#define SETTINGS_ADDR 0x0000
#define VFO_ADDR (SETTINGS_ADDR + SECTOR_SIZE)
#define NVM_MAGIC 0x4F525458U /* "ORTX" */
#define NVM_RECORD_MAX 256U
#define NVM_WRITE_ALIGN 4U

struct nvmRecord {
    uint32_t magic;
    uint16_t crc;
    uint16_t pad;
};

static const struct flash_area *storage;
static uint16_t settingsCrc;
static uint16_t vfoCrc;

/**
 * Read a record from the storage partition, validating magic and CRC.
 *
 * @param off: record offset inside the storage partition.
 * @param data: destination buffer for the record payload.
 * @param len: payload size in bytes.
 * @return zero on success, a negative error code otherwise.
 */
static int readRecord(uint32_t off, void *data, size_t len)
{
    struct nvmRecord rec;
    uint8_t buf[NVM_RECORD_MAX];
    size_t padded = (len + NVM_WRITE_ALIGN - 1U) & ~(NVM_WRITE_ALIGN - 1U);

    if ((storage == NULL) || (padded > sizeof(buf)))
        return -1;

    if (flash_area_read(storage, off, &rec, sizeof(rec)) < 0)
        return -1;

    if (rec.magic != NVM_MAGIC)
        return -1;

    if (flash_area_read(storage, off + sizeof(rec), buf, padded) < 0)
        return -1;

    memcpy(data, buf, len);

    if (rec.crc != crc_ccitt(data, len))
        return -1;

    return 0;
}

/**
 * Erase the sector holding a record and write its payload, storing the CRC
 * alongside the magic value. Payloads are padded to the flash write block
 * size, as the underlying flash driver rejects unaligned writes.
 *
 * @param off: record offset inside the storage partition.
 * @param data: payload to be written.
 * @param len: payload size in bytes.
 * @return zero on success, a negative error code otherwise.
 */
static int writeRecord(uint32_t off, const void *data, size_t len)
{
    struct nvmRecord rec = { .magic = NVM_MAGIC, .crc = crc_ccitt(data, len) };
    uint8_t buf[NVM_RECORD_MAX];
    size_t padded = (len + NVM_WRITE_ALIGN - 1U) & ~(NVM_WRITE_ALIGN - 1U);

    if ((storage == NULL) || (padded > sizeof(buf)))
        return -1;

    memcpy(buf, data, len);
    memset(buf + len, 0xFF, padded - len);

    if (flash_area_erase(storage, off, SECTOR_SIZE) < 0)
        return -1;

    if (flash_area_write(storage, off, &rec, sizeof(rec)) < 0)
        return -1;

    return flash_area_write(storage, off + sizeof(rec), buf, padded);
}

void nvm_init()
{
    int ret = flash_area_open(FIXED_PARTITION_ID(storage_partition), &storage);

    if (ret < 0) {
        printk("nvmem: cannot open storage partition (%d)\n", ret);
        storage = NULL;
        return;
    }
}

void nvm_terminate()
{
    if (storage != NULL) {
        flash_area_close(storage);
        storage = NULL;
    }
}

/* External SPI NOR, read only: it holds the vendor factory calibration */
static const struct device *const extFlash =
    DEVICE_DT_GET(DT_NODELABEL(extflash));

void nvm_readCalibData(void *buf)
{
    struct rt950proCalib *cal = (struct rt950proCalib *)buf;

    /* An unreadable calibration is all zeroes: no PA drive */
    if (!device_is_ready(extFlash)
        || (flash_read(extFlash, RT950PRO_CAL_NOR_ADDR, cal,
                       offsetof(struct rt950proCalib, ctcssGain))
            != 0)
        || (flash_read(extFlash, RT950PRO_CAL_NOR_CTCSS_ADDR, &cal->ctcssGain,
                       sizeof(cal->ctcssGain))
            != 0))
        memset(cal, 0x00, sizeof(*cal));
}

void nvm_readHwInfo(hwInfo_t *info)
{
    (void)info;
}

int nvm_readVfoChannelData(channel_t *channel)
{
    (void)channel;
    if (channel == NULL)
        return -1;

    memset(channel, 0x00, sizeof(channel_t));

    if (readRecord(VFO_ADDR, channel, sizeof(channel_t)) < 0)
        return -1;

    vfoCrc = crc_ccitt(channel, sizeof(channel_t));

    return 0;
}

int nvm_readSettings(settings_t *settings)
{
    if (settings == NULL)
        return -1;

    memset(settings, 0x00, sizeof(settings_t));

    if (readRecord(SETTINGS_ADDR, settings, sizeof(settings_t)) < 0)
        return -1;

    settingsCrc = crc_ccitt(settings, sizeof(settings_t));

    return 0;
}

int nvm_writeSettings(const settings_t *settings)
{
    return nvm_writeSettingsAndVfo(settings, NULL);
}

int nvm_writeSettingsAndVfo(const settings_t *settings, const channel_t *vfo)
{
    if (settings != NULL) {
        uint16_t crc = crc_ccitt(settings, sizeof(settings_t));

        if (crc != settingsCrc) {
            if (writeRecord(SETTINGS_ADDR, settings, sizeof(settings_t)) < 0)
                return -1;

            settingsCrc = crc;
        }
    }

    if (vfo != NULL) {
        uint16_t crc = crc_ccitt(vfo, sizeof(channel_t));

        if (crc != vfoCrc) {
            if (writeRecord(VFO_ADDR, vfo, sizeof(channel_t)) < 0)
                return -1;

            vfoCrc = crc;
        }
    }
    return 0;
}
