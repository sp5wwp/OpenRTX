/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <interfaces/nvmem.h>
#include <hwconfig.h>
#include <string.h>
#include "core/cps.h"
#include "core/crc.h"

/*
 * Settings and VFO storage in the MCU internal flash.
 *
 * The last two 8kB pages of the 2MB flash are used, 0x081FC000-0x081FFFFF.
 * The stock firmware does not use them: it caches data at 0x081E4000 and
 * 0x081EE000, and keeps 16 bytes in the last bootloader page at 0x0800A000.
 *
 * The flash is programmed in 16-byte quadwords and a quadword can be written
 * only once after an erase (ECC), so the bit-flag scheme used on other
 * targets cannot be used here. Each save appends a record to the active page.
 * Records carry a sequence number and a CRC: the valid record with the
 * highest sequence number is the current one. When the active page is full,
 * the other page is erased and becomes the active one, so the last good
 * record survives an interrupted save.
 */

#define NVM_BASE 0x081FC000UL
#define PAGE_SIZE 0x2000UL
#define NUM_PAGES 2
#define FLASH_BASE_A 0x08000000UL
#define BANK_SIZE 0x100000UL

static const uint32_t REC_MAGIC = 0x584E504F; // "OPNX"

typedef struct {
    uint32_t magic;
    uint32_t seq;
    uint16_t crc;  // CRC of settings and vfo
    uint16_t size; // sizeof(settings_t) + sizeof(channel_t)
    settings_t settings;
    channel_t vfo;
} __attribute__((packed)) record_t;

#define SLOT_SIZE ((sizeof(record_t) + 15) & ~15UL)
#define SLOTS_PER_PAGE (PAGE_SIZE / SLOT_SIZE)

static const uint16_t DATA_SIZE = sizeof(settings_t) + sizeof(channel_t);

static inline const record_t *slotPtr(uint32_t page, uint32_t slot)
{
    return (const record_t *)(NVM_BASE + page * PAGE_SIZE + slot * SLOT_SIZE);
}

static inline bool slotEmpty(uint32_t page, uint32_t slot)
{
    return *((const volatile uint32_t *)slotPtr(page, slot)) == 0xFFFFFFFF;
}

static bool slotValid(const record_t *rec)
{
    if ((rec->magic != REC_MAGIC) || (rec->size != DATA_SIZE))
        return false;

    return crc_ccitt(&rec->settings, DATA_SIZE) == rec->crc;
}

/**
 * Find the most recent valid record.
 *
 * @param page: page holding the record.
 * @return pointer to the record or NULL if none is valid.
 */
static const record_t *findLatest(uint32_t *page)
{
    const record_t *best = NULL;

    for (uint32_t p = 0; p < NUM_PAGES; p++) {
        for (uint32_t s = 0; s < SLOTS_PER_PAGE; s++) {
            if (slotEmpty(p, s))
                break;

            const record_t *rec = slotPtr(p, s);
            if (slotValid(rec) == false)
                continue;

            if ((best == NULL) || (rec->seq > best->seq)) {
                best = rec;
                if (page != NULL)
                    *page = p;
            }
        }
    }

    return best;
}

/**
 * First free slot of a page, after the last used one.
 *
 * @return slot index or SLOTS_PER_PAGE if the page is full.
 */
static uint32_t firstFreeSlot(uint32_t page)
{
    uint32_t free = 0;

    for (uint32_t s = 0; s < SLOTS_PER_PAGE; s++) {
        if (slotEmpty(page, s) == false)
            free = s + 1;
    }

    return free;
}

static void flashUnlock()
{
    if ((FLASH->NSCR & FLASH_NSCR_LOCK) != 0) {
        FLASH->NSKEYR = 0x45670123;
        FLASH->NSKEYR = 0xCDEF89AB;
    }
}

static void flashLock()
{
    FLASH->NSCR |= FLASH_NSCR_LOCK;
}

static const uint32_t FLASH_ERRORS = FLASH_NSSR_OPERR | FLASH_NSSR_PROGERR
                                   | FLASH_NSSR_WRPERR | FLASH_NSSR_PGAERR
                                   | FLASH_NSSR_SIZERR | FLASH_NSSR_PGSERR;

static bool flashWait()
{
    while ((FLASH->NSSR & (FLASH_NSSR_BSY | FLASH_NSSR_WDW)) != 0)
        ;

    uint32_t sr = FLASH->NSSR;
    FLASH->NSSR = sr & (FLASH_ERRORS | FLASH_NSSR_EOP);

    return (sr & FLASH_ERRORS) == 0;
}

/*
 * The instruction cache also caches data reads from flash: invalidate it
 * after a change, if the bootloader left it enabled.
 */
static void cacheInvalidate()
{
    if ((ICACHE->CR & ICACHE_CR_EN) == 0)
        return;

    ICACHE->CR |= ICACHE_CR_CACHEINV;
    while ((ICACHE->SR & ICACHE_SR_BUSYF) != 0)
        ;
}

static bool flashErasePage(uint32_t page)
{
    uint32_t offset = NVM_BASE + page * PAGE_SIZE - FLASH_BASE_A;
    uint32_t bank = offset / BANK_SIZE;
    uint32_t pnb = (offset % BANK_SIZE) / PAGE_SIZE;

    // With swapped banks the bank selection bit refers to the physical bank
    if ((FLASH->OPTR & FLASH_OPTR_SWAP_BANK) != 0)
        bank ^= 1;

    flashWait();
    FLASH->NSCR = FLASH_NSCR_PER | (pnb << FLASH_NSCR_PNB_Pos)
                | (bank << FLASH_NSCR_BKER_Pos);
    FLASH->NSCR |= FLASH_NSCR_STRT;
    bool ok = flashWait();
    FLASH->NSCR &= ~(FLASH_NSCR_PER | FLASH_NSCR_PNB | FLASH_NSCR_BKER);

    return ok;
}

static bool flashProgram(uint32_t addr, const void *data, size_t len)
{
    const uint8_t *src = (const uint8_t *)data;
    bool ok = true;

    flashWait();
    FLASH->NSCR |= FLASH_NSCR_PG;

    for (size_t pos = 0; (pos < len) && ok; pos += 16) {
        uint32_t quad[4];
        memset(quad, 0xFF, sizeof(quad));

        size_t n = len - pos;
        if (n > 16)
            n = 16;
        memcpy(quad, src + pos, n);

        volatile uint32_t *dst = (volatile uint32_t *)(addr + pos);
        for (int i = 0; i < 4; i++)
            dst[i] = quad[i];

        ok = flashWait();
    }

    FLASH->NSCR &= ~FLASH_NSCR_PG;

    return ok;
}

static int writeRecord(const settings_t *settings, const channel_t *vfo)
{
    uint32_t page = 0;
    const record_t *last = findLatest(&page);

    record_t rec;
    rec.magic = REC_MAGIC;
    rec.seq = (last != NULL) ? (last->seq + 1) : 1;
    rec.size = DATA_SIZE;
    memcpy(&rec.settings, settings, sizeof(settings_t));
    memcpy(&rec.vfo, vfo, sizeof(channel_t));
    rec.crc = crc_ccitt(&rec.settings, DATA_SIZE);

    // Nothing changed since the last save
    if ((last != NULL) && (last->crc == rec.crc)
        && (memcmp(&last->settings, &rec.settings, DATA_SIZE) == 0))
        return 0;

    flashUnlock();

    bool ok = true;
    uint32_t slot = SLOTS_PER_PAGE;
    if (last != NULL)
        slot = firstFreeSlot(page);

    // No valid data or active page full: start over on the other page
    if (slot >= SLOTS_PER_PAGE) {
        page = (last != NULL) ? ((page + 1) % NUM_PAGES) : 0;
        slot = 0;
        ok = flashErasePage(page);
    }

    if (ok)
        ok = flashProgram((uint32_t)slotPtr(page, slot), &rec, sizeof(rec));

    flashLock();
    cacheInvalidate();

    return ok ? 0 : -1;
}

void nvm_init()
{
}

void nvm_terminate()
{
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
    const record_t *rec = findLatest(NULL);
    if (rec == NULL)
        return -1;

    memcpy(channel, &rec->vfo, sizeof(channel_t));

    return 0;
}

int nvm_readSettings(settings_t *settings)
{
    const record_t *rec = findLatest(NULL);
    if (rec == NULL)
        return -1;

    memcpy(settings, &rec->settings, sizeof(settings_t));

    return 0;
}

int nvm_writeSettings(const settings_t *settings)
{
    channel_t vfo;
    const record_t *rec = findLatest(NULL);

    if (rec != NULL)
        memcpy(&vfo, &rec->vfo, sizeof(channel_t));
    else
        vfo = cps_getDefaultChannel();

    return writeRecord(settings, &vfo);
}

int nvm_writeSettingsAndVfo(const settings_t *settings, const channel_t *vfo)
{
    return writeRecord(settings, vfo);
}
