/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <drivers/baseband/WX4818N.h>
#include <drivers/NVM/spiNand_D890UV.h>
#include <drivers/USB/usb.h>
#include <interfaces/delays.h>
#include <hwconfig.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "monitor.h"
#include "bootloader.h"
#include "drivers/SPI/spi_stm32.h"
#include "peripherals/i2c.h"
#include "peripherals/adc.h"
#include <math.h>

/*
 * Debug monitor on the USB serial port. One command per line, numbers in
 * hexadecimal (with or without 0x). Every reply ends with a line "OK" or
 * "ERR <reason>".
 *
 *   ping                      -> D890UV
 *   rb <addr> <len>           read bytes, hex, 32 bytes per line
 *   rw <addr> [count]         read 32-bit words, one per line
 *   wb <addr> <value>         write a byte
 *   ww <addr> <value>         write a 32-bit word
 *   nr <addr> <len>           read the SPI NAND (addr = page << 12 | column)
 *   xr <a|b> <reg>            read a WX4818N register
 *   xw <a|b> <reg> <value>    write a WX4818N register
 *   xg <a|b> <pin> <0|1>      set a WX4818N GPIO
 *   update                    restart into the bootloader update mode
 *   si                        power up and reset the SCT3288, wait for boot
 *   sw <hexbytes>             send bytes to the SCT3288 (no spaces)
 *   sr                        read the bytes the SCT3288 has ready
 *   so                        power the SCT3288 down
 *   sa <0|1>                  read SCT3288 frames automatically, as stock
 *   sl                        print the automatically read frames
 *   sx <hex> [timeout]        exchange a packet with the SCT3288
 *   iw <addr> <hexbytes>      write bytes to an I2C device
 *   ar <count>                on the * key, record <count> sample pairs of
 *                             PC0 (received audio or band B output) and PA7
 *                             (microphone amplifier) at 8kHz, then print the
 *                             buffer address (read it with rb)
 *   tg <shape> [hz] [amp]     test signal on PB9 (TIM4 PWM, 48kHz samples
 *                             from TIM15):
 *                             0 off, 1 sine, 2 square, 3 PN9 4FSK (M17
 *                             mapping and RRC filter, 4800 symbols/s,
 *                             hz ignored); amp 0-127 (default 64)
 *
 * Memory accesses are not checked: an unmapped address faults the MCU.
 */

#define MAX_READ 4096
#define NAND_PAGE 0x1000

static void out(const char *fmt, ...)
{
    char buf[160];
    va_list ap;

    va_start(ap, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (len < 0)
        return;
    if (len >= (int)sizeof(buf))
        len = sizeof(buf) - 1;

    usb_write(buf, len);
}

static void hexLines(uint32_t addr, const uint8_t *data, size_t len)
{
    for (size_t pos = 0; pos < len; pos += 32) {
        char line[8 + 2 + 32 * 2 + 2];
        int n = snprintf(line, sizeof(line), "%08lX ",
                         (unsigned long)(addr + pos));

        for (size_t i = pos; (i < len) && (i < pos + 32); i++)
            n += snprintf(line + n, sizeof(line) - n, "%02X", data[i]);

        line[n++] = '\n';
        usb_write(line, n);
    }
}

static bool parseHex(const char *str, uint32_t *value)
{
    if (str == NULL)
        return false;

    char *end;
    *value = strtoul(str, &end, 16);

    return (*end == '\0') && (end != str);
}

static const struct wx4818n *parseChip(const char *str)
{
    if (str == NULL)
        return NULL;
    if ((strcmp(str, "a") == 0) || (strcmp(str, "A") == 0))
        return &wx4818nA;
    if ((strcmp(str, "b") == 0) || (strcmp(str, "B") == 0))
        return &wx4818nB;

    return NULL;
}

static const char *cmdReadBytes(char **arg)
{
    uint32_t addr, len;
    if (!parseHex(arg[0], &addr) || !parseHex(arg[1], &len))
        return "usage: rb <addr> <len>";
    if (len > MAX_READ)
        return "length too large";

    hexLines(addr, (const uint8_t *)addr, len);
    return NULL;
}

static const char *cmdReadWords(char **arg)
{
    uint32_t addr, count = 1;
    if (!parseHex(arg[0], &addr)
        || ((arg[1] != NULL) && !parseHex(arg[1], &count)))
        return "usage: rw <addr> [count]";
    if ((addr & 3) != 0)
        return "address not aligned";
    if (count > MAX_READ / 4)
        return "count too large";

    for (uint32_t i = 0; i < count; i++) {
        uint32_t a = addr + 4 * i;
        out("%08lX %08lX\n", (unsigned long)a,
            (unsigned long)*((volatile uint32_t *)a));
    }

    return NULL;
}

static const char *cmdWriteByte(char **arg)
{
    uint32_t addr, value;
    if (!parseHex(arg[0], &addr) || !parseHex(arg[1], &value) || (value > 0xFF))
        return "usage: wb <addr> <value>";

    *((volatile uint8_t *)addr) = value;
    return NULL;
}

static const char *cmdWriteWord(char **arg)
{
    uint32_t addr, value;
    if (!parseHex(arg[0], &addr) || !parseHex(arg[1], &value))
        return "usage: ww <addr> <value>";
    if ((addr & 3) != 0)
        return "address not aligned";

    *((volatile uint32_t *)addr) = value;
    return NULL;
}

static const char *cmdNandRead(char **arg)
{
    static uint8_t buf[256];
    uint32_t addr, len;

    if (!parseHex(arg[0], &addr) || !parseHex(arg[1], &len))
        return "usage: nr <addr> <len>";
    if (len > MAX_READ)
        return "length too large";

    while (len > 0) {
        // Do not cross a page boundary in a single read
        uint32_t chunk = NAND_PAGE - (addr & (NAND_PAGE - 1));
        if (chunk > sizeof(buf))
            chunk = sizeof(buf);
        if (chunk > len)
            chunk = len;

        if (spiNand_read(addr, buf, chunk) < 0)
            return "NAND timeout";

        hexLines(addr, buf, chunk);
        addr += chunk;
        len -= chunk;
    }

    return NULL;
}

static const char *cmdWxRead(char **arg)
{
    const struct wx4818n *dev = parseChip(arg[0]);
    uint32_t reg;
    if ((dev == NULL) || !parseHex(arg[1], &reg) || (reg > 0x7F))
        return "usage: xr <a|b> <reg>";

    out("%04X\n", wx4818n_readReg(dev, reg));
    return NULL;
}

static const char *cmdWxWrite(char **arg)
{
    const struct wx4818n *dev = parseChip(arg[0]);
    uint32_t reg, value;
    if ((dev == NULL) || !parseHex(arg[1], &reg) || (reg > 0x7F)
        || !parseHex(arg[2], &value) || (value > 0xFFFF))
        return "usage: xw <a|b> <reg> <value>";

    wx4818n_writeReg(dev, reg, value);
    return NULL;
}

static const char *cmdWxGpio(char **arg)
{
    const struct wx4818n *dev = parseChip(arg[0]);
    uint32_t pin, level;
    if ((dev == NULL) || !parseHex(arg[1], &pin) || (pin > 7)
        || !parseHex(arg[2], &level) || (level > 1))
        return "usage: xg <a|b> <pin> <0|1>";

    wx4818n_setGpio(dev, pin, level);
    return NULL;
}

static const char *cmdUpdate(char **arg)
{
    (void)arg;

    if (bootloader_isKnown() == false)
        return "unknown bootloader version";

    // Let the reply reach the host before the USB device goes away
    out("OK\n");
    sleepFor(0, 100);
    bootloader_restartToUpdate();

    return NULL;
}

/* SCT3288 link speed for the bring-up, well below the stock SPI2 setting */
#define SCT_SPI_SPEED 5000000

static const char *cmdSctInit(char **arg)
{
    static bool spiReady = false;
    (void)arg;

    if (spiReady == false) {
        gpio_setMode(SCT_SCK, ALTERNATE | ALTERNATE_FUNC(5));
        gpio_setMode(SCT_MISO, ALTERNATE | ALTERNATE_FUNC(5));
        gpio_setMode(SCT_MOSI, ALTERNATE | ALTERNATE_FUNC(5));
        gpio_setOutputSpeed(SCT_SCK, HIGH);
        gpio_setOutputSpeed(SCT_MOSI, HIGH);
        if (spiStm32_init(&sct_spi, SCT_SPI_SPEED, 0) < 0)
            return "SPI init failed";
        spiReady = true;
    }

    // SPI2 state, for the bring-up
    out("SPI2 CR1=%08lX CFG1=%08lX CFG2=%08lX SR=%08lX\n",
        (unsigned long)SPI2->CR1, (unsigned long)SPI2->CFG1,
        (unsigned long)SPI2->CFG2, (unsigned long)SPI2->SR);

    if (sct3288_init(&sct3288) < 0)
        return "no boot status";

    return NULL;
}

static const char *cmdSctWrite(char **arg)
{
    uint8_t buf[64];
    size_t len = 0;
    const char *hex = arg[0];

    if ((hex == NULL) || ((strlen(hex) % 2) != 0)
        || (strlen(hex) / 2 > sizeof(buf)))
        return "usage: sw <hexbytes>";

    for (; hex[0] != '\0'; hex += 2) {
        char byte[3] = { hex[0], hex[1], '\0' };
        char *end;
        buf[len++] = strtoul(byte, &end, 16);
        if (*end != '\0')
            return "bad hex";
    }

    sct3288_write(&sct3288, buf, len);
    return NULL;
}

/*
 * Send one packet to the SCT3288 and wait for its answer: used to program
 * the chip's flash (re-tools, NOTES.md). Prints "RX <hex>" with every frame
 * received within 20ms of the first one, or "RX -" on timeout.
 */
#define SCT_EXCHANGE_MAX 1536
#define SCT_EXCHANGE_TAIL_MS 20

static const char *cmdSctExchange(char **arg)
{
    static uint8_t tx[SCT_EXCHANGE_MAX];
    static uint8_t rx[256];
    static char line[8 + 2 * sizeof(rx)];
    const char *hex = arg[0];
    uint32_t timeout = 1000;
    size_t len = 0;

    if ((hex == NULL) || ((strlen(hex) % 2) != 0)
        || ((strlen(hex) / 2) > sizeof(tx)))
        return "usage: sx <hexbytes> [timeout ms, hex]";

    if ((arg[1] != NULL) && !parseHex(arg[1], &timeout))
        return "bad timeout";

    for (; hex[0] != '\0'; hex += 2) {
        char byte[3] = { hex[0], hex[1], '\0' };
        char *end;
        tx[len++] = strtoul(byte, &end, 16);
        if (*end != '\0')
            return "bad hex";
    }

    sct3288_drain(&sct3288);
    sct3288_write(&sct3288, tx, len);

    size_t n = 0;
    uint32_t idle = 0;
    for (uint32_t t = 0; t < timeout; t++) {
        size_t got = sct3288_read(&sct3288, rx + n, sizeof(rx) - n);
        n += got;
        if (got > 0)
            idle = 0;
        else if ((n > 0) && (++idle > SCT_EXCHANGE_TAIL_MS))
            break;
        if (n == sizeof(rx))
            break;
        sleepFor(0, 1);
    }

    int pos = snprintf(line, sizeof(line), "RX ");
    if (n == 0)
        pos += snprintf(line + pos, sizeof(line) - pos, "-");
    for (size_t i = 0; i < n; i++)
        pos += snprintf(line + pos, sizeof(line) - pos, "%02X", rx[i]);
    line[pos++] = '\n';
    usb_write(line, pos);
    return NULL;
}

/* Write bytes to an I2C device on the bit-banged bus: iw <addr7> <hexbytes> */
static const char *cmdI2cWrite(char **arg)
{
    uint8_t buf[32];
    uint32_t addr;
    size_t len = 0;
    const char *hex = arg[1];

    if (!parseHex(arg[0], &addr) || (hex == NULL) || ((strlen(hex) % 2) != 0)
        || ((strlen(hex) / 2) > sizeof(buf)))
        return "usage: iw <addr> <hexbytes>";

    for (; hex[0] != '\0'; hex += 2) {
        char byte[3] = { hex[0], hex[1], '\0' };
        char *end;
        buf[len++] = strtoul(byte, &end, 16);
        if (*end != '\0')
            return "bad hex";
    }

    i2c_acquire(&i2c_bus);
    int ret = i2c_write(&i2c_bus, addr, buf, len, true);
    i2c_release(&i2c_bus);

    return (ret == 0) ? NULL : "no acknowledge";
}

/*
 * Audio recorder: PC0 and PA7 sampled alternately at 8kHz, timed by TIM6.
 * Blocking: meant to run with the USB cable unplugged. The DIGI-RX LED is
 * on while recording.
 */
#define REC_RATE_HZ 8000
#define REC_MAX 32000
#define REC_WAIT_MS 60000

/* In SRAM3, which the linker script leaves unused (it maps SRAM1 only) */
#define REC_BUF ((uint16_t(*)[2])0x20040000)

static const char *cmdAudioRecord(char **arg)
{
    uint32_t count;

    if (!parseHex(arg[0], &count) || (count > REC_MAX))
        return "usage: ar <count>";

    RCC->AHB2ENR1 |= RCC_AHB2ENR1_SRAM3EN;
    __DSB();

    gpio_setMode(AIN_AF, ANALOG);
    gpio_setMode(AIN_MIC, ANALOG);

    /* Start on the * key, within a minute */
    uint32_t waited = 0;
    while ((kbd_lastKeys() & KEY_STAR) == 0) {
        if (waited >= REC_WAIT_MS)
            return "no * key";
        sleepFor(0, 10);
        waited += 10;
    }

    gpio_setPin(LED_DIGI_RX);

    /* TIM6, unused elsewhere, overflows at the sample rate (APB1 = HCLK) */
    RCC->APB1ENR1 |= RCC_APB1ENR1_TIM6EN;
    __DSB();
    TIM6->CR1 = 0;
    TIM6->PSC = 0;
    TIM6->ARR = (SystemCoreClock / REC_RATE_HZ) - 1;
    TIM6->EGR = TIM_EGR_UG;
    TIM6->SR = 0;
    TIM6->CR1 = TIM_CR1_CEN;

    for (uint32_t i = 0; i < count; i++) {
        while ((TIM6->SR & TIM_SR_UIF) == 0)
            ;
        TIM6->SR = 0;
        REC_BUF[i][0] = adc_getRawSample(&adc1, ADC_AF_CH);
        REC_BUF[i][1] = adc_getRawSample(&adc1, ADC_MIC_CH);
    }

    TIM6->CR1 = 0;
    RCC->APB1ENR1 &= ~RCC_APB1ENR1_TIM6EN;
    gpio_clearPin(LED_DIGI_RX);

    out("buf %08lX %lu\n", (uint32_t)REC_BUF, count);
    return NULL;
}

/*
 * Test signal generator on PB9: TIM4 channel 4 runs an 8-bit PWM (625kHz
 * carrier, as stock: ARR 0xFF), set up by the audio driver (pwm_D890UV.c);
 * TIM15 interrupts at 48kHz and writes the next sample. The board's RC
 * filter on the TONE net removes the carrier. Not to be used while an audio
 * stream plays.
 */
#define TG_RATE_HZ 48000
#define TG_TABLE 256

enum tgShape {
    TG_OFF = 0,
    TG_SINE = 1,
    TG_SQUARE = 2,
    TG_PN9 = 3,
};

/*
 * PN9 4FSK: x^9 + x^5 + 1 bits taken in pairs, M17 mapping (01 +3, 00 +1,
 * 10 -1, 11 -3), shaped by a root raised cosine (alpha 0.5, 8 symbols span)
 * at 10 samples per symbol.
 */
#define TG_SPS 10
#define TG_SPAN 8
#define TG_TAPS (TG_SPS * TG_SPAN + 1)

static int16_t tgRrc[TG_TAPS];
static int8_t tgSym[TG_SPAN + 1];
static uint16_t tgLfsr;
static uint8_t tgSub;
static int32_t tgAmp;
static volatile uint8_t tgShape;

static int8_t tgNextSymbol()
{
    uint8_t bits = 0;
    for (int i = 0; i < 2; i++) {
        uint8_t b = ((tgLfsr >> 8) ^ (tgLfsr >> 4)) & 1;
        tgLfsr = ((tgLfsr << 1) | b) & 0x1FF;
        bits = (bits << 1) | b;
    }

    static const int8_t map[4] = { 1, 3, -1, -3 };
    return map[bits];
}

static void tgRrcInit()
{
    const float a = 0.5f;
    float peak = 0.0f;
    float h[TG_TAPS];

    for (int i = 0; i < TG_TAPS; i++) {
        float t = (float)(i - (TG_TAPS / 2)) / TG_SPS;
        float v;
        if (fabsf(t) < 1e-6f)
            v = 1.0f - a + (4.0f * a / 3.14159265f);
        else if (fabsf(fabsf(t) - (1.0f / (4.0f * a))) < 1e-4f)
            v = (a / sqrtf(2.0f))
              * ((1.0f + 2.0f / 3.14159265f) * sinf(3.14159265f / (4.0f * a))
                 + (1.0f - 2.0f / 3.14159265f)
                       * cosf(3.14159265f / (4.0f * a)));
        else
            v = (sinf(3.14159265f * t * (1.0f - a))
                 + 4.0f * a * t * cosf(3.14159265f * t * (1.0f + a)))
              / (3.14159265f * t * (1.0f - (4.0f * a * t) * (4.0f * a * t)));
        h[i] = v;
        if (fabsf(v) > peak)
            peak = fabsf(v);
    }

    /* Scale so that a +3 symbol train peaks near 3 * 4096 */
    float sum = 0.0f;
    for (int i = 0; i < TG_TAPS; i += TG_SPS)
        sum += h[i];
    for (int i = 0; i < TG_TAPS; i++)
        tgRrc[i] = (int16_t)(h[i] * 4096.0f / sum);
    (void)peak;
}

static uint8_t tgPn9Sample()
{
    if (tgSub == 0) {
        for (int i = TG_SPAN; i > 0; i--)
            tgSym[i] = tgSym[i - 1];
        tgSym[0] = tgNextSymbol();
    }

    int32_t acc = 0;
    for (int i = 0; i <= TG_SPAN; i++) {
        int k = tgSub + (i * TG_SPS);
        if (k < TG_TAPS)
            acc += tgSym[i] * tgRrc[k];
    }

    tgSub = (tgSub + 1) % TG_SPS;

    /* acc: +-3 symbols -> about +-3 * 4096; map +-3 to +-amp */
    int32_t v = 128 + ((acc * tgAmp) / (3 * 4096));
    if (v < 0)
        v = 0;
    if (v > 255)
        v = 255;
    return (uint8_t)v;
}

static uint8_t tgTable[TG_TABLE];
static volatile uint32_t tgPhase;
static volatile uint32_t tgStep;

void _Z16TIM15_IRQHandlerv()
{
    TIM15->SR = 0;
    if (tgShape == TG_PN9) {
        TIM4->CCR4 = tgPn9Sample();
        return;
    }
    tgPhase += tgStep;
    TIM4->CCR4 = tgTable[tgPhase >> 24];
}

static void tgStop()
{
    NVIC_DisableIRQ(TIM15_IRQn);
    TIM15->CR1 = 0;
    RCC->APB2ENR &= ~RCC_APB2ENR_TIM15EN;
    TIM4->CCR4 = 0x80;
}

static const char *cmdTestSignal(char **arg)
{
    uint32_t shape;
    uint32_t hz = 1000;
    uint32_t amp = 64;

    if (!parseHex(arg[0], &shape))
        return "usage: tg <0|1|2> [hz] [amp]";
    if ((arg[1] != NULL) && !parseHex(arg[1], &hz))
        return "bad hz";
    if ((arg[2] != NULL) && !parseHex(arg[2], &amp))
        return "bad amp";
    if (amp > 127)
        amp = 127;

    tgStop();
    if (shape == TG_OFF)
        return NULL;

    for (int i = 0; i < TG_TABLE; i++) {
        float v;
        if (shape == TG_SINE)
            v = sinf(2.0f * 3.14159265f * i / TG_TABLE);
        else
            v = (i < (TG_TABLE / 2)) ? 1.0f : -1.0f;
        tgTable[i] = (uint8_t)(128 + (int)(v * amp));
    }
    tgShape = shape;
    if (shape == TG_PN9) {
        tgRrcInit();
        tgLfsr = 0x1FF;
        tgSub = 0;
        tgAmp = amp;
        for (int i = 0; i <= TG_SPAN; i++)
            tgSym[i] = 0;
    }

    tgPhase = 0;
    tgStep = (uint32_t)(((uint64_t)hz << 32) / TG_RATE_HZ);

    RCC->APB2ENR |= RCC_APB2ENR_TIM15EN;
    __DSB();

    TIM15->CR1 = 0;
    TIM15->PSC = 0;
    TIM15->ARR = (SystemCoreClock / TG_RATE_HZ) - 1;
    TIM15->DIER = TIM_DIER_UIE;
    TIM15->EGR = TIM_EGR_UG;
    TIM15->SR = 0;
    NVIC_SetPriority(TIM15_IRQn, 10);
    NVIC_EnableIRQ(TIM15_IRQn);
    TIM15->CR1 = TIM_CR1_CEN;

    return NULL;
}

static const char *cmdSctRead(char **arg)
{
    uint8_t buf[256];
    (void)arg;

    size_t len = sct3288_read(&sct3288, buf, sizeof(buf));
    hexLines(0, buf, len);
    out("%u bytes\n", (unsigned)len);
    return NULL;
}

static const char *cmdSctOff(char **arg)
{
    (void)arg;
    sct3288_terminate(&sct3288);
    return NULL;
}

/*
 * Automatic reading of the SCT3288 frames, like the stock main loop
 * (0x0810A6DC). Frames are kept with a timestamp in a small log.
 */
#define SCT_LOG_SIZE 32
#define SCT_FRAME_MAX 32

struct sctFrame {
    uint32_t time;
    uint8_t len;
    uint8_t data[SCT_FRAME_MAX];
};

static struct sctFrame sctLog[SCT_LOG_SIZE];
static size_t sctLogCount;
static bool sctAuto;

void monitor_poll()
{
    if (sctAuto == false)
        return;

    uint8_t buf[SCT_FRAME_MAX];
    size_t len = sct3288_read(&sct3288, buf, sizeof(buf));
    if (len == 0)
        return;

    struct sctFrame *f = &sctLog[sctLogCount % SCT_LOG_SIZE];
    f->time = getTick();
    f->len = len;
    memcpy(f->data, buf, len);
    sctLogCount++;
}

static const char *cmdSctAuto(char **arg)
{
    uint32_t on;
    if (!parseHex(arg[0], &on))
        return "usage: sa <0|1>";

    sctAuto = (on != 0);
    return NULL;
}

static const char *cmdSctLog(char **arg)
{
    (void)arg;

    size_t first = (sctLogCount > SCT_LOG_SIZE) ? sctLogCount - SCT_LOG_SIZE :
                                                  0;
    for (size_t i = first; i < sctLogCount; i++) {
        const struct sctFrame *f = &sctLog[i % SCT_LOG_SIZE];
        char line[16 + 2 * SCT_FRAME_MAX];
        int n = snprintf(line, sizeof(line), "%8lu ", (unsigned long)f->time);
        for (uint8_t j = 0; j < f->len; j++)
            n += snprintf(line + n, sizeof(line) - n, "%02X", f->data[j]);
        line[n++] = '\n';
        usb_write(line, n);
    }

    sctLogCount = 0;
    return NULL;
}

static const char *cmdPing(char **arg)
{
    (void)arg;
    out("D890UV\n");
    return NULL;
}

struct command {
    const char *name;
    const char *(*func)(char **arg);
};

static const struct command commands[] = {
    { "ping", cmdPing },      { "rb", cmdReadBytes },   { "rw", cmdReadWords },
    { "wb", cmdWriteByte },   { "ww", cmdWriteWord },   { "nr", cmdNandRead },
    { "xr", cmdWxRead },      { "xw", cmdWxWrite },     { "xg", cmdWxGpio },
    { "update", cmdUpdate },  { "si", cmdSctInit },     { "sw", cmdSctWrite },
    { "sr", cmdSctRead },     { "so", cmdSctOff },      { "sa", cmdSctAuto },
    { "sl", cmdSctLog },      { "sx", cmdSctExchange }, { "iw", cmdI2cWrite },
    { "ar", cmdAudioRecord }, { "tg", cmdTestSignal },
};

void monitor_exec(char *line)
{
    char *arg[4] = { NULL };
    char *save;

    char *cmd = strtok_r(line, " \t", &save);
    if (cmd == NULL)
        return;

    for (int i = 0; i < 4; i++)
        arg[i] = strtok_r(NULL, " \t", &save);

    for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
        if (strcmp(cmd, commands[i].name) != 0)
            continue;

        const char *err = commands[i].func(arg);
        if (err == NULL)
            out("OK\n");
        else
            out("ERR %s\n", err);

        return;
    }

    out("ERR unknown command\n");
}
