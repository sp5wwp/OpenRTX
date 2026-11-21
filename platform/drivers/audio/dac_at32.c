/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <errno.h>
#include <soc.h>
#include <stdbool.h>
#include <zephyr/irq.h>
#include <zephyr/kernel.h>
#include "dac_at32.h"

/*
 * Audio output on the AT32F403A DAC1 (PA4). TMR6 interrupts at DAC_AT32_RATE
 * and one 12-bit sample is written per interrupt, linearly interpolated from
 * the stream samples. In linear mode the DAC converts it at the next TMR6
 * overflow (hardware trigger, see setHwTrigger()). Interrupt-driven rather than DMA-driven so that a hook
 * can process every output sample.
 *
 * The interrupt path runs from RAM and touches the registers directly: with
 * 224 kB of SRAM only the first 128 kB of Flash are zero-wait, and code above
 * them is too slow for a 48 kHz interrupt (AT32F403A reference manual 2.3).
 *
 * TMR6 is on APB1; its clock is twice the APB1 clock when APB1 is divided
 * (AT32F403A reference manual, clock tree).
 */

#define DAC_MID 2048
/* DAC1 trigger enable bit, DAC_CTRL */
#define DAC_D1TRGEN BIT(2)
/*
 * The DAC interrupt has the highest priority, with the debug serial port (a
 * short handler, which must not be preempted by it: the commands it received
 * were corrupted). The DAC update and the ADC sample of the receive audio
 * canceller (canceller.c) must be on time, or the timing error on the played
 * audio is fed back to the speaker as clicks. Its kernel calls are deferred to
 * DEFER_IRQ, an unused timer interrupt line triggered by software, so that it
 * stays short and regular.
 */
#define DEFER_IRQ TMR7_GLOBAL_IRQn
#define DEFER_IRQ_PRIO 2
/*
 * Stream samples (int16) to DAC counts (12 bit), through a look-ahead peak
 * limiter. Speech uses a fraction of the int16 range: it is scaled up by 8 over
 * a plain 16 to 12 bit conversion, and the limiter lowers the gain where the
 * peaks would exceed LIMIT_LEVEL, instead of clipping them.
 *
 * The samples are delayed by LIMIT_LOOKAHEAD stream samples, and the envelope
 * follows the peak of the delay line: it rises over the look-ahead time,
 * reaching a peak before the peak is output, so that the gain never steps
 * (a step is heard as a click). Attack coefficient: 1 - exp(-5 / 64), within
 * 1% of the peak in 64 samples. Exponential release.
 *
 * The margin below full scale is left to the hooks (e.g. the receive audio
 * canceller correction, up to about 400 counts) and to the residual attack
 * error: at the maximum volume, peaks above about 1800 counts overdrive the
 * speaker amplifier.
 */
#define STREAM_GAIN (8.0f / 16.0f)
#define LIMIT_LEVEL 1500.0f
#define LIMIT_LOOKAHEAD 64
#define LIMIT_ATTACK 0.075f
#define LIMIT_RELEASE_S 0.15f

static struct streamCtx *volatile stream;
static volatile uint32_t pos;   /* Stream sample being output */
static volatile uint32_t sub;   /* Output sample within it */
static volatile uint32_t ratio; /* Output samples per stream sample */
static volatile int32_t prev;   /* Previous stream sample, DAC counts */
static volatile int32_t cur;    /* Current stream sample, DAC counts */
static volatile bool linear;    /* Linear mode: no limiter */
static volatile int32_t linearGain = 256; /* Linear mode gain, 1/256 */
static float delayLine[LIMIT_LOOKAHEAD];  /* Limiter delay line, DAC counts */
static uint32_t delayPos;                 /* Oldest sample in the delay line */
static float envelope; /* Limiter peak envelope, DAC counts */
static float release;  /* Limiter release factor per sample */
static volatile bool stopRequest;
static dacAt32_hook hooks[DAC_AT32_MAX_HOOKS];
static volatile size_t numHooks;
static volatile dacAt32_tick tickFn;
static K_SEM_DEFINE(syncSem, 0, 1);
/* Sync points reached before the previous one was consumed: late refills */
volatile uint32_t dacAt32_underruns;
/*
 * Halves output before their refill was complete (a sync after it): the
 * output splices stale samples with new ones.
 */
volatile uint32_t dacAt32_lateRefills;
/* Interrupts delayed past the next sample: samples lost, the output slips */
volatile uint32_t dacAt32_lostTicks;
/* Output samples clipped to the DAC range */
volatile uint32_t dacAt32_clipped;
/* Longest delay from a TMR6 overflow to its interrupt, CPU cycles */
volatile uint32_t dacAt32_latencyMax;
static volatile bool halfReady[2];
static volatile unsigned int idleHalf;
/* Interrupt cost: sum of cycles and number of interrupts, and the maximum */
volatile uint32_t dacAt32_isrCycles;
volatile uint32_t dacAt32_isrCount;
volatile uint32_t dacAt32_isrMax;

/*
 * Advance in the stream; at the middle and at the end of the buffer, wake up
 * the thread waiting in sync() and stop if requested.
 */
static AUDIO_ISR_CODE int32_t limit(stream_sample_t sample)
{
    float x = (float)sample * STREAM_GAIN;
    float out = delayLine[delayPos];

    delayLine[delayPos] = x;
    delayPos = (delayPos + 1) % LIMIT_LOOKAHEAD;

    float peak = LIMIT_LEVEL;
    for (uint32_t i = 0; i < LIMIT_LOOKAHEAD; i++) {
        float a = (delayLine[i] < 0.0f) ? -delayLine[i] : delayLine[i];
        if (a > peak)
            peak = a;
    }

    if (peak > envelope)
        envelope += (peak - envelope) * LIMIT_ATTACK;
    else
        envelope = LIMIT_LEVEL + ((envelope - LIMIT_LEVEL) * release);

    return (int32_t)(out * (LIMIT_LEVEL / envelope));
}

static volatile uint32_t deferredPending;
static dacAt32_deferred deferredFns[DAC_AT32_MAX_DEFERRED];
static volatile int numDeferred;
static int syncDeferred;

AUDIO_ISR_CODE void dacAt32_defer(int id)
{
    __atomic_fetch_or(&deferredPending, 1U << id, __ATOMIC_RELAXED);
    NVIC_SetPendingIRQ(DEFER_IRQ);
}

static void deferIsr(const void *arg)
{
    (void)arg;

    uint32_t pending = __atomic_exchange_n(&deferredPending, 0,
                                           __ATOMIC_RELAXED);
    for (int i = 0; i < numDeferred; i++) {
        if ((pending & (1U << i)) != 0)
            deferredFns[i]();
    }
}

/* Half of the stream buffer output: wake up the thread waiting in sync() */
static void syncEvent(void)
{
    dacAt32_defer(syncDeferred);
}

/*
 * Linear mode: cascaded first order equaliser sections,
 * y = b0 x + b1 x[-1] - a1 y[-1], then the gain, in DAC counts.
 */
struct eqSection {
    float b0, b1, a1;
    float x, y;
};

static struct eqSection eq[DAC_AT32_EQ_SECTIONS] = {
    { .b0 = 1.0f },
    { .b0 = 1.0f },
};

static AUDIO_ISR_CODE int32_t equalise(stream_sample_t sample)
{
    float v = (float)sample;

    for (int i = 0; i < DAC_AT32_EQ_SECTIONS; i++) {
        struct eqSection *s = &eq[i];
        float y = (s->b0 * v) + (s->b1 * s->x) - (s->a1 * s->y);
        s->x = v;
        s->y = y;
        v = y;
    }

    return (int32_t)((v * (float)linearGain) * (1.0f / (16.0f * 256.0f)));
}

static void resetEqualiser(void)
{
    for (int i = 0; i < DAC_AT32_EQ_SECTIONS; i++) {
        eq[i].x = 0.0f;
        eq[i].y = 0.0f;
    }
}

/*
 * DAC1 conversion trigger. Linear mode (modulation): at each TMR6 overflow
 * (TRGO on update), from the value the interrupt wrote one period earlier,
 * so that the output timing is exact, not moved by the interrupt latency
 * (jitter that adds noise to the modulation, M17 symbol timing). Otherwise
 * (speaker, receive canceller): as the interrupt writes it, at the same delay
 * from the ADC sample taken in the same interrupt, as the canceller model
 * requires.
 */
static AUDIO_ISR_CODE void setHwTrigger(bool on)
{
    if (on)
        DAC->ctrl |= DAC_D1TRGEN;
    else
        DAC->ctrl &= ~DAC_D1TRGEN;
}

static AUDIO_ISR_CODE void advance(struct streamCtx *ctx)
{
    if (++sub < ratio)
        return;

    sub = 0;
    prev = cur;
    pos++;

    bool half = (pos == (ctx->bufSize / 2));
    bool end = (pos >= ctx->bufSize);
    if (!half && !end)
        return;

    if (end)
        pos = 0;

    /* The half starting now must have been refilled since it was output */
    unsigned int next = end ? 0 : 1;
    if (!halfReady[next])
        dacAt32_lateRefills++;
    halfReady[next] = false;

    if (stopRequest || (end && (ctx->bufMode == BUF_LINEAR))) {
        ctx->running = 0;
        setHwTrigger(false);
    }

    if (k_sem_count_get(&syncSem) != 0)
        dacAt32_underruns++;
    k_sem_give(&syncSem);
}

/*
 * Sample period in timer ticks, 16.16 fixed point: the nominal one, corrected
 * for the error of the MCU clock (dacAt32_setClockTrim()).
 */
static uint32_t nominalPeriod;
static volatile uint32_t periodInt;
static volatile uint32_t periodFrac;
static uint32_t periodAcc;
static int32_t clockTrim;
static uint32_t lastOverflow;

static AUDIO_ISR_CODE void tmr6Isr(const void *arg)
{
    (void)arg;

    /* Entry time and latency: the counter restarted at the overflow */
    uint32_t t0 = DWT->CYCCNT;
    uint32_t latency = TMR6->cval;

    if ((TMR6->ists & TMR_OVF_FLAG) == 0)
        return;

    TMR6->ists = ~TMR_OVF_FLAG;

    /* Next period, dithered for the trimmed average rate */
    periodAcc += periodFrac;
    TMR6->pr = periodInt + (periodAcc >> 16) - 1;
    periodAcc &= 0xFFFFU;

    /*
     * An overflow more than one and a half periods after the previous one
     * serviced: the interrupt was delayed past a whole period, and the
     * sample in between was lost (the output slips). The timer counts the
     * CPU clock (TMR6 clock = AHB clock, checked at init).
     */
    uint32_t overflow = t0 - latency;
    if ((overflow - lastOverflow) > (3 * nominalPeriod / 2))
        dacAt32_lostTicks++;
    lastOverflow = overflow;
    if (latency > dacAt32_latencyMax)
        dacAt32_latencyMax = latency;

    dacAt32_tick tick = tickFn;
    if (tick != NULL)
        tick();

    int32_t out = 0;
    struct streamCtx *ctx = stream;
    if ((ctx != NULL) && (ctx->running != 0)) {
        /* Each stream sample goes through the limiter once, as it starts */
        if (sub == 0)
            cur = linear ? equalise(ctx->buffer[pos]) : limit(ctx->buffer[pos]);
        out = prev + (((cur - prev) * (int32_t)sub) / (int32_t)ratio);
        advance(ctx);
    }

    for (size_t i = 0; i < numHooks; i++)
        out = hooks[i](out);

    if (out > DAC_AT32_MAX) {
        out = DAC_AT32_MAX;
        dacAt32_clipped++;
    }
    if (out < -DAC_AT32_MAX) {
        out = -DAC_AT32_MAX;
        dacAt32_clipped++;
    }

    *(volatile uint32_t *)DAC1_12BIT_RIGHT = (uint32_t)(DAC_MID + out);

    uint32_t dt = DWT->CYCCNT - t0;
    dacAt32_isrCycles += dt;
    dacAt32_isrCount++;
    if (dt > dacAt32_isrMax)
        dacAt32_isrMax = dt;
}

void dacAt32_init(void)
{
    gpio_init_type pin;

    crm_periph_clock_enable(CRM_GPIOA_PERIPH_CLOCK, TRUE);
    crm_periph_clock_enable(CRM_DAC_PERIPH_CLOCK, TRUE);
    crm_periph_clock_enable(CRM_TMR6_PERIPH_CLOCK, TRUE);

    gpio_default_para_init(&pin);
    pin.gpio_pins = GPIO_PINS_4;
    pin.gpio_mode = GPIO_MODE_ANALOG;
    gpio_init(GPIOA, &pin);

    /* Output buffer on: the bench measurements of the paths were made so */
    dac_output_buffer_enable(DAC1_SELECT, TRUE);
    /* Hardware trigger source, enabled for linear mode streams only */
    dac_trigger_select(DAC1_SELECT, DAC_TMR6_TRGOUT_EVENT);
    dac_trigger_enable(DAC1_SELECT, FALSE);
    dac_1_data_set(DAC1_12BIT_RIGHT, DAC_MID);
    dac_enable(DAC1_SELECT, TRUE);

    /* Cycle counter, for the interrupt cost figures */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    crm_clocks_freq_type clocks;
    crm_clocks_freq_get(&clocks);
    uint32_t timerClock = clocks.apb1_freq;
    if (clocks.apb1_freq != clocks.ahb_freq)
        timerClock *= 2;

    /* The lost sample detection compares timer and CPU cycle counts */
    __ASSERT(timerClock == clocks.ahb_freq, "TMR6 clock differs from AHB");

    nominalPeriod = (timerClock + (DAC_AT32_RATE / 2)) / DAC_AT32_RATE;
    periodInt = nominalPeriod;
    periodFrac = 0;
    tmr_base_init(TMR6, nominalPeriod - 1, 0);
    tmr_period_buffer_enable(TMR6, TRUE);
    tmr_primary_mode_select(TMR6, TMR_PRIMARY_SEL_OVERFLOW);
    tmr_cnt_dir_set(TMR6, TMR_COUNT_UP);
    tmr_interrupt_enable(TMR6, TMR_OVF_INT, TRUE);

    syncDeferred = dacAt32_addDeferred(syncEvent);
    IRQ_CONNECT(DEFER_IRQ, DEFER_IRQ_PRIO, deferIsr, NULL, 0);
    irq_enable(DEFER_IRQ);

    IRQ_CONNECT(TMR6_GLOBAL_IRQn, 0, tmr6Isr, NULL, 0);

    /*
     * The kernel puts its timer at the highest priority too: move it below,
     * so that it never delays the audio interrupt.
     */
    NVIC_SetPriority(SysTick_IRQn, NVIC_GetPriority(SysTick_IRQn) + 2);
    irq_enable(TMR6_GLOBAL_IRQn);

    tmr_counter_enable(TMR6, TRUE);
}

void dacAt32_setClockTrim(int32_t error)
{
    /* Ticks of a clock off by error / 10^7 in one nominal sample period */
    int64_t p = ((int64_t)nominalPeriod << 16);
    p += (p * error) / 10000000;

    unsigned int key = dacAt32_lock();
    periodInt = (uint32_t)(p >> 16);
    periodFrac = (uint32_t)(p & 0xFFFF);
    clockTrim = error;
    dacAt32_unlock(key);
}

int32_t dacAt32_getClockTrim(void)
{
    return clockTrim;
}

void dacAt32_setLinearGain(uint16_t gain)
{
    linearGain = gain;
}

int dacAt32_setLinearFilter(unsigned int section, float b0, float b1, float a1)
{
    if (section >= DAC_AT32_EQ_SECTIONS)
        return -EINVAL;

    unsigned int key = dacAt32_lock();
    eq[section].b0 = b0;
    eq[section].b1 = b1;
    eq[section].a1 = a1;
    resetEqualiser();
    dacAt32_unlock(key);

    return 0;
}

int dacAt32_addHook(dacAt32_hook hook)
{
    if (numHooks >= DAC_AT32_MAX_HOOKS)
        return -ENOMEM;

    /* The slot is filled before it is counted, so the interrupt never sees
     * an empty one */
    hooks[numHooks] = hook;
    numHooks++;

    return 0;
}

void dacAt32_setTick(dacAt32_tick tick)
{
    tickFn = tick;
}

int dacAt32_addDeferred(dacAt32_deferred fn)
{
    if (numDeferred >= DAC_AT32_MAX_DEFERRED)
        return -ENOMEM;

    deferredFns[numDeferred] = fn;
    return numDeferred++;
}

unsigned int dacAt32_lock(void)
{
    unsigned int key = irq_lock();

    irq_disable(TMR6_GLOBAL_IRQn);
    __DSB();
    __ISB();

    return key;
}

void dacAt32_unlock(unsigned int key)
{
    irq_enable(TMR6_GLOBAL_IRQn);
    irq_unlock(key);
}

static int dacAt32_start(const uint8_t instance, const void *config,
                         struct streamCtx *ctx)
{
    (void)instance;

    if ((ctx == NULL) || (ctx->running != 0) || (ctx->sampleRate == 0))
        return -EINVAL;
    if ((DAC_AT32_RATE % ctx->sampleRate) != 0)
        return -EINVAL;
    if ((stream != NULL) && (stream->running != 0))
        return -EBUSY;

    unsigned int key = dacAt32_lock();
    linear = ((enum dacAt32Mode)(uintptr_t)config == DAC_AT32_LINEAR);
    setHwTrigger(linear);
    resetEqualiser();
    pos = 0;
    sub = 0;
    prev = 0;
    cur = 0;
    for (size_t i = 0; i < LIMIT_LOOKAHEAD; i++)
        delayLine[i] = 0.0f;
    delayPos = 0;
    envelope = LIMIT_LEVEL;
    release = 1.0f - (1.0f / (LIMIT_RELEASE_S * (float)ctx->sampleRate));
    ratio = DAC_AT32_RATE / ctx->sampleRate;
    stopRequest = false;
    k_sem_reset(&syncSem);
    /* The stream starts with the whole buffer filled by the caller */
    halfReady[0] = true;
    halfReady[1] = true;
    idleHalf = 1;
    ctx->running = 1;
    stream = ctx;
    dacAt32_unlock(key);

    return 0;
}

static int dacAt32_data(struct streamCtx *ctx, stream_sample_t **buf)
{
    size_t half = ctx->bufSize / 2;

    /* The idle half is the one not being output */
    idleHalf = (pos < half) ? 1 : 0;
    *buf = &ctx->buffer[idleHalf * half];

    return half;
}

static int dacAt32_sync(struct streamCtx *ctx, uint8_t dirty)
{
    (void)dirty;

    if (ctx->running == 0)
        return -1;

    halfReady[idleHalf] = true;
    k_sem_take(&syncSem, K_FOREVER);

    return 0;
}

static void dacAt32_stop(struct streamCtx *ctx)
{
    if ((ctx != stream) || (ctx->running == 0))
        return;

    stopRequest = true;
}

static void dacAt32_terminate(struct streamCtx *ctx)
{
    unsigned int key = dacAt32_lock();
    ctx->running = 0;
    /* Only the stream being output is detached, not one being started */
    bool current = (ctx == stream);
    if (current) {
        stream = NULL;
        setHwTrigger(false);
    }
    dacAt32_unlock(key);

    if (current)
        k_sem_give(&syncSem);
}

const struct audioDriver dacAt32_driver = {
    .start = dacAt32_start,
    .data = dacAt32_data,
    .sync = dacAt32_sync,
    .stop = dacAt32_stop,
    .terminate = dacAt32_terminate,
};
