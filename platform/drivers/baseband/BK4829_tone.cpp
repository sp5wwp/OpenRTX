/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <zephyr/kernel.h>
#include <zephyr/irq.h>
#include <zephyr/sys/sys_io.h>
#include <cmsis_core.h>
#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <numeric>
#include <hwconfig.h>
#include "interfaces/delays.h"
#include "protocols/M17/DSP.hpp"
#include "BK4829_tone.h"

// AT32F403A registers: TMR7 and DMA2 channel 4 clock GPIOE's set and clear
// register from a buffer of bus slots
#define CRM_AHBEN 0x40021014
#define CRM_APB1EN 0x4002101C
#define AHB_DMA2 BIT(1)
#define APB1_TMR7 BIT(5)
#define TMR7_CTRL1 0x40001400
#define TMR7_IDEN 0x4000140C
#define TMR7_SWEVT 0x40001414
#define TMR7_DIV 0x40001428
#define TMR7_PR 0x4000142C
#define TMR_IDEN_OVFDEN BIT(8)
#define DMA2_STS 0x40020400
#define DMA2_CLR 0x40020404
#define DMA2_C4_CTRL 0x40020444
#define DMA2_C4_DTCNT 0x40020448
#define DMA2_C4_PADDR 0x4002044C
#define DMA2_C4_MADDR 0x40020450
#define DMA_C4_FDT BIT(13)
#define DMA_C4_HDT BIT(14)
#define DMA_C4_ALL (0xF << 12)
// Enabled, full and half transfer interrupts, memory to peripheral,
// circular, memory increment, 32 bit each way, highest priority
#define DMA_CTRL_BUS                                                \
    (BIT(0) | BIT(1) | BIT(2) | BIT(4) | BIT(5) | BIT(7) | (2 << 8) \
     | (2 << 10) | (3 << 12))
#define GPIOE_SCR 0x40011810
#define GPIOE_CLR 0x40011814
#define BUS_SET(pin) BIT(pin)
#define BUS_CLR(pin) BIT((pin) + 16)

// The BK4829s' bus on port E
#define BK_SCK 10
#define BK_SDA 11

#define TIMER_HZ 120000000 // APB1 at 60MHz, its timers at twice that
#define WRITE_HZ 48000     // Gain writes, one a stream sample
#define SLOT_CYCLES 50     // 417ns a bus slot
#define SLOTS (TIMER_HZ / WRITE_HZ / SLOT_CYCLES) // 50 slots a gain write
#define HALF_WRITES 12                            // 0.25ms a buffer half

// Gain curve units (see toneGain) of a +3 symbol and of a zero baseband.
// The baseband is inverted, so the EOT frame, nearly all +3, pulls the level
// down: the zero sits well above the swing
#define UNITS_SYMBOL3 70
#define UNITS_ZERO 190
#define UNITS_MAX 472
// REG_40 for 2.4kHz of outer deviation at those units, in the 25kHz mode
#define DEVIATION 0x681
// The tone runs at 1kHz for this long before it is stopped: at its peak,
// where the level it stops at hardly depends on the timing
#define FREEZE_US 550
// Carrier at the zero level before the baseband starts: the tone path is a
// 4Hz high-pass and has to forget the step to that level
#define SETTLE_MS 200
// The same high-pass, undone on the baseband: 2 pi 4Hz / 24kHz in Q20
#define LOW_FREQ_GAIN 1098

#define SYMBOL_SAMPLES 10 // Stream samples a symbol
#define PULSE_SPS 5       // Pulse samples a symbol, 24kHz
#define PULSE_SYMBOLS 24

/**
 * A symbol's pulse at 24kHz, Q14 with 1.0 the level of a run of equal
 * symbols: the RRC convolved with the inverse of the tone path's measured
 * response (flat to 2.4kHz, 2dB down at 2.8kHz, 235us more delay at 2.4kHz
 * than at 500Hz).
 */
static const int16_t pulse[PULSE_SPS * PULSE_SYMBOLS] = {
    0,     0,     0,    0,     0,     0,     0,     1,    2,     1,     -1,
    -4,    -7,    -6,   -1,    7,     12,    10,    -1,   -12,   -10,   14,
    46,    53,    -12,  -171,  -392,  -581,  -613,  -400, 37,    543,   883,
    855,   421,   -235, -771,  -857,  -376,  469,   1229, 1437,  889,   -189,
    -1208, -1519, -788, 746,   2317,  2982,  2128,  -138, -2933, -4929, -4942,
    -2484, 1984,  7217, 11693, 14206, 14285, 12267, 9044, 5642,  2842,  994,
    50,    -254,  -210, -22,   208,   432,   609,   693,  658,   520,   338,
    190,   127,   150,  219,   281,   300,   281,   251,  240,   259,   297,
    327,   331,   309,  277,   255,   256,   282,   320,  354,   372,   367,
    341,   295,   235,  169,   104,   49,    11,    -10,  -15,   -10,   -3,
    1,     3,     2,    1,     0,     0,     1,     1,    0,     0
};

static const bk4829_reg_t regs[] = { BK4829_REG_2B, BK4829_REG_30,
                                     BK4829_REG_40, BK4829_REG_43,
                                     BK4829_REG_47, BK4829_REG_70,
                                     BK4829_REG_71, BK4829_REG_7E };

static struct {
    const struct BK4829 *chip;             ///< Keyed chip, NULL when none
    uint16_t saved[ARRAY_SIZE(regs)];      ///< Its registers before the key-up
    struct streamCtx *ctx;                 ///< Running stream, NULL when none
    volatile bool stopping;                ///< Stop at the stream's next half
    size_t pos;                            ///< Next stream sample to take
    struct k_sem sync;                     ///< Given at each half of the stream
    uint32_t bus[2 * HALF_WRITES * SLOTS]; ///< Slot words, two halves
    uint8_t gainSlot[7];                   ///< Slot of each gain bit, MSB first
    std::array<float, 2 * 81> filter;      ///< Last stream samples, twice
    size_t filterHead;
    int8_t symbols[PULSE_SYMBOLS];         ///< Last symbols, newest first
    uint8_t phase;                         ///< Pulse sample within the symbol
    int32_t from;    ///< Last 24kHz sample, 1/16 of a unit
    int32_t to;      ///< This one
    int32_t lowFreq; ///< Leaky sum of the baseband
    float unit;      ///< A symbol unit out of the filter
} tone;

/**
 * Spin for a few CPU cycles.
 *
 * @param turns: loop turns, about three cycles each.
 */
static inline void busDelay(uint32_t turns)
{
    while (turns--)
        __asm__ volatile("nop");
}

/**
 * Write a register of the keyed chip through GPIOE's set and clear
 * registers, in about ten microseconds: the tone is stopped by two writes a
 * set time apart. The bus must be idle.
 *
 * @param reg: register.
 * @param val: value.
 */
static void fastWrite(uint8_t reg, uint16_t val)
{
    const uint32_t scn = tone.chip->scn.pin;
    const uint32_t word = (static_cast<uint32_t>(reg) << 16) | val;

    sys_write32(BIT(scn), GPIOE_CLR);
    busDelay(1);
    for (int b = 23; b >= 0; b--) {
        sys_write32(BIT(BK_SDA), (word & BIT(b)) ? GPIOE_SCR : GPIOE_CLR);
        busDelay(1);
        sys_write32(BIT(BK_SCK), GPIOE_SCR);
        busDelay(1);
        sys_write32(BIT(BK_SCK), GPIOE_CLR);
        // The chip wants a microsecond after the address byte
        if (b == 16)
            busDelay(40);
    }

    busDelay(1);
    sys_write32(BIT(scn), GPIOE_SCR);
}

/**
 * Tone 1's gain code for a deviation in units of the gain curve: four
 * segments of 32 codes whose slope doubles, 1, 2, 4 then 8 units a code.
 *
 * @param units: 0 to UNITS_MAX.
 * @return the gain, 0 to 127.
 */
static uint8_t toneGain(int32_t units)
{
    if (units < 32)
        return static_cast<uint8_t>(units);
    if (units < 96)
        return static_cast<uint8_t>(32 + (units - 32) / 2);
    if (units < 224)
        return static_cast<uint8_t>(64 + (units - 96) / 4);
    return static_cast<uint8_t>(96 + (units - 224) / 8);
}

/**
 * Take a stream sample: every tenth one completes a symbol, read with the
 * matched filter and rounded.
 *
 * @param sample: baseband sample from the M17 modulator.
 */
static void takeSample(stream_sample_t sample)
{
    constexpr size_t TAPS = M17::rrc_taps_48k.size();

    // Each sample twice, TAPS apart, so the newest TAPS are contiguous
    tone.filter[tone.filterHead] = static_cast<float>(sample);
    tone.filter[tone.filterHead + TAPS] = tone.filter[tone.filterHead];
    tone.filterHead = (tone.filterHead + 1) % TAPS;

    if ((tone.pos % SYMBOL_SAMPLES) != 0)
        return;

    const float value =
        std::inner_product(M17::rrc_taps_48k.begin(), M17::rrc_taps_48k.end(),
                           tone.filter.begin() + tone.filterHead, 0.0f)
        / tone.unit;
    std::copy_backward(std::begin(tone.symbols), std::end(tone.symbols) - 1,
                       std::end(tone.symbols));
    tone.symbols[0] = static_cast<int8_t>(
        std::max(-3, std::min(3, static_cast<int>(lroundf(value)))));
    tone.phase = 0;
}

/**
 * The next 24kHz baseband sample: the symbols through the pulse, scaled to
 * the gain curve, the tone path's high-pass undone.
 *
 * @return the sample in sixteenths of a gain curve unit.
 */
static int32_t nextLevel(void)
{
    int32_t acc = 0;
    for (size_t i = 0; i < PULSE_SYMBOLS; i++)
        acc += tone.symbols[i] * pulse[tone.phase + PULSE_SPS * i];
    tone.phase = (tone.phase + 1) % PULSE_SPS;

    // Q14 of a symbol unit to sixteenths of a gain unit, inverted: the tone
    // path inverts
    int32_t v = -static_cast<int32_t>(
        (static_cast<int64_t>(acc) * UNITS_SYMBOL3 * 16) / (3 * 16384));
    // A run of one sign sags through the 4Hz high-pass, the EOT frame's most
    // of all. Its inverse is the baseband plus its integral, which leaks
    // here below 1Hz: lower, a small bias in the data walks the level away
    tone.lowFreq += v - (tone.lowFreq >> 12);
    v += static_cast<int32_t>(
        (static_cast<int64_t>(tone.lowFreq) * LOW_FREQ_GAIN) >> 20);

    return std::max<int32_t>(0, std::min<int32_t>(UNITS_MAX << 4,
                                                  (UNITS_ZERO << 4) + v));
}

/**
 * Fill the bus buffer with REG_70 writes at the zero level: SCN low with the
 * first data bit, data set as SCK falls, SCK up a slot later, SCN up a slot
 * after the last bit. A slot's word is for GPIOE's set and clear register;
 * zero changes nothing.
 */
static void busTemplate(void)
{
    const uint32_t scn = tone.chip->scn.pin;
    const uint32_t word = (0x70 << 16) | 0x8000 | (toneGain(UNITS_ZERO) << 8);

    for (size_t w = 0; w < (2 * HALF_WRITES); w++) {
        uint32_t *base = &tone.bus[w * SLOTS];

        memset(base, 0, SLOTS * sizeof(uint32_t));
        for (uint32_t k = 0; k < 24; k++) {
            base[2 * k] = BUS_CLR(BK_SCK)
                        | ((word & BIT(23 - k)) ? BUS_SET(BK_SDA) :
                                                  BUS_CLR(BK_SDA))
                        | ((k == 0) ? BUS_CLR(scn) : 0);
            base[2 * k + 1] = BUS_SET(BK_SCK);
            // Bits 14 to 8 are the gain
            if ((k >= 9) && (k <= 15))
                tone.gainSlot[k - 9] = static_cast<uint8_t>(2 * k);
        }
        base[48] = BUS_CLR(BK_SCK);
        base[49] = BUS_SET(scn);
    }
}

/**
 * Put the next gain codes in half of the bus buffer, a write a stream
 * sample: a 24kHz level every other sample and the midpoint in between,
 * since the chip reads the gain on its own clock and shorter steps leave
 * less timing error.
 *
 * @param half: 0 or 1.
 */
static void busFill(int half)
{
    struct streamCtx *ctx = tone.ctx;
    uint32_t *w = &tone.bus[half * HALF_WRITES * SLOTS];

    for (size_t i = 0; i < HALF_WRITES; i++, w += SLOTS) {
        int32_t level = tone.to;

        if (ctx != NULL) {
            takeSample(ctx->buffer[tone.pos]);
            if ((tone.pos % 2) == 0) {
                tone.from = tone.to;
                tone.to = nextLevel();
                level = (tone.from + tone.to) / 2;
            }

            tone.pos = (tone.pos + 1) % ctx->bufSize;
            if ((tone.pos % (ctx->bufSize / 2)) == 0) {
                k_sem_give(&tone.sync);
                if (tone.stopping) {
                    ctx->running = 0;
                    tone.ctx = NULL;
                    ctx = NULL;
                }
            }
        }

        const uint8_t gain = toneGain((level + 8) >> 4);
        for (size_t b = 0; b < ARRAY_SIZE(tone.gainSlot); b++)
            w[tone.gainSlot[b]] = BUS_CLR(BK_SCK)
                                | ((gain & BIT(6 - b)) ? BUS_SET(BK_SDA) :
                                                         BUS_CLR(BK_SDA));
    }
}

void bk4829Tone_isr(void)
{
    const uint32_t sts = sys_read32(DMA2_STS) & (DMA_C4_HDT | DMA_C4_FDT);
    if (sts == 0)
        return;

    sys_write32(sts, DMA2_CLR);
    if (sts & DMA_C4_HDT)
        busFill(0);
    if (sts & DMA_C4_FDT)
        busFill(1);
}

/**
 * Stop the bus DMA and leave the bus at rest.
 */
static void busStop(void)
{
    sys_write32(0, TMR7_CTRL1);
    sys_write32(0, TMR7_IDEN);
    sys_write32(0, DMA2_C4_CTRL);
    sys_write32(DMA_C4_ALL, DMA2_CLR);
    if (tone.chip != NULL) {
        sys_write32(BIT(BK_SCK), GPIOE_CLR);
        sys_write32(BIT(tone.chip->scn.pin), GPIOE_SCR);
    }
}

int bk4829Tone_keyUp(const struct BK4829 *chip, uint32_t freq)
{
    int ret = bk4829_set_freq(chip, freq);
    if (ret < 0)
        return ret;

    tone.chip = chip;
    for (size_t i = 0; i < ARRAY_SIZE(regs); i++)
        tone.saved[i] = bk4829_read_reg(chip, regs[i]);

    // Written whole and before keying: updating some of these once keyed
    // corrupts them
    bk4829_write_reg(chip, BK4829_REG_40,
                     (tone.saved[2] & ~0x0FFF) | DEVIATION);
    // The TX filter chain in the path with its filters off
    bk4829_write_reg(chip, BK4829_REG_2B, tone.saved[0] | 0x0007);
    // The 25kHz bandwidth mode: in the 12.5kHz one a gain code deviates half
    // as far
    bk4829_write_reg(chip, BK4829_REG_43, (tone.saved[3] & ~0x0030) | 0x0020);
    // The filter chain not bypassed, nothing on the AF output
    bk4829_write_reg(chip, BK4829_REG_47, tone.saved[4] & ~0x0F01);
    // No TX DC filter
    bk4829_write_reg(chip, BK4829_REG_7E, tone.saved[7] & ~0x0038);
    // Tone transmit, the mic ADC off
    bk4829_write_reg(chip, BK4829_REG_30, 0);
    bk4829_write_reg(chip, BK4829_REG_30, 0xC3FA);

    // Tone 1 at 1kHz, stopped at its peak: its gain is then a level. Locked:
    // an interrupt or a thread in here would move where the tone stops
    const unsigned int key = irq_lock();
    bk4829_write_reg(chip, BK4829_REG_71, 0x2854);
    fastWrite(0x70, 0x8000 | (toneGain(UNITS_ZERO) << 8));
    k_busy_wait(FREEZE_US);
    fastWrite(0x71, 0);
    irq_unlock(key);

    sleepFor(0, SETTLE_MS);
    return 0;
}

void bk4829Tone_keyDown(void)
{
    const struct BK4829 *chip = tone.chip;
    if (chip == NULL)
        return;

    busStop();
    bk4829_rtx_off(chip);
    for (size_t i = 0; i < ARRAY_SIZE(regs); i++)
        bk4829_write_reg(chip, regs[i], tone.saved[i]);
    tone.chip = NULL;
}

/**
 * Start the bus from the baseband stream: a chip must be keyed.
 */
static int tone_start(const uint8_t instance, const void *config,
                      struct streamCtx *ctx)
{
    (void)instance;
    (void)config;

    if ((tone.chip == NULL) || (ctx == NULL) || (ctx->running != 0))
        return -EINVAL;
    if ((ctx->sampleRate != WRITE_HZ) || (ctx->bufMode != BUF_CIRC_DOUBLE)
        || ((ctx->bufSize % (2 * SYMBOL_SAMPLES)) != 0))
        return -EINVAL;

    tone.stopping = false;
    tone.pos = 0;
    tone.filter.fill(0.0f);
    tone.filterHead = 0;
    memset(tone.symbols, 0, sizeof(tone.symbols));
    tone.phase = 0;
    tone.from = UNITS_ZERO << 4;
    tone.to = UNITS_ZERO << 4;
    tone.lowFreq = 0;
    // The modulator scales a symbol by 23000; a symbol through its filter
    // and the matched one is the sum of the squared taps
    tone.unit = 23000.0f
              * std::inner_product(M17::rrc_taps_48k.begin(),
                                   M17::rrc_taps_48k.end(),
                                   M17::rrc_taps_48k.begin(), 0.0f);
    k_sem_init(&tone.sync, 0, 1);
    ctx->running = 1;
    tone.ctx = ctx;

    busTemplate();
    sys_set_bits(CRM_APB1EN, APB1_TMR7);
    sys_set_bits(CRM_AHBEN, AHB_DMA2);
    sys_write32(0, TMR7_CTRL1);
    sys_write32(0, TMR7_IDEN);
    sys_write32(0, TMR7_DIV);
    sys_write32(SLOT_CYCLES - 1, TMR7_PR);
    sys_write32(1, TMR7_SWEVT);

    sys_write32(0, DMA2_C4_CTRL);
    sys_write32(DMA_C4_ALL, DMA2_CLR);
    sys_write32(GPIOE_SCR, DMA2_C4_PADDR);
    sys_write32(reinterpret_cast<uint32_t>(tone.bus), DMA2_C4_MADDR);
    sys_write32(ARRAY_SIZE(tone.bus), DMA2_C4_DTCNT);
    sys_write32(DMA_CTRL_BUS, DMA2_C4_CTRL);

    sys_write32(BIT(BK_SCK), GPIOE_CLR);
    sys_write32(TMR_IDEN_OVFDEN, TMR7_IDEN);
    sys_write32(1, TMR7_CTRL1);
    return 0;
}

/**
 * The half of the stream buffer that is not being played.
 */
static int tone_data(struct streamCtx *ctx, stream_sample_t **buf)
{
    const size_t half = ctx->bufSize / 2;

    *buf = (tone.pos < half) ? &ctx->buffer[half] : ctx->buffer;
    return static_cast<int>(half);
}

/**
 * Wait for the bus to pass a half of the stream buffer.
 */
static int tone_sync(struct streamCtx *ctx, uint8_t dirty)
{
    (void)dirty;

    if (ctx->running == 0)
        return -1;

    return (k_sem_take(&tone.sync, K_MSEC(200)) == 0) ? 0 : -1;
}

/**
 * Stop at the stream's next half: the carrier then stays at the zero level
 * until the key-down.
 */
static void tone_stop(struct streamCtx *ctx)
{
    if (ctx->running != 0)
        tone.stopping = true;
}

/**
 * Stop at once.
 */
static void tone_terminate(struct streamCtx *ctx)
{
    const unsigned int key = irq_lock();
    tone.ctx = NULL;
    ctx->running = 0;
    irq_unlock(key);
    k_sem_give(&tone.sync);
}

const struct audioDriver bk4829_tone_audio_driver = {
    .start = tone_start,
    .data = tone_data,
    .sync = tone_sync,
    .stop = tone_stop,
    .terminate = tone_terminate,
};
