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
 * the stream samples. Interrupt-driven rather than DMA-driven so that a hook
 * can process every output sample.
 *
 * The interrupt path runs from RAM and touches the registers directly: with
 * 224 kB of SRAM only the first 128 kB of Flash are zero-wait, and code above
 * them is too slow for a 48 kHz interrupt (AT32F403A reference manual 2.3).
 *
 * Timer clock: SYSCLK 120 MHz, APB1 /2, so the APB1 timers run at 120 MHz
 * (AT32F403A reference manual, clock tree).
 */

#define TIMER_CLOCK 120000000UL
#define DAC_MID 2048
#define TMR6_IRQ_PRIO 2
/*
 * Stream samples (int16) to DAC counts (12 bit). Speech uses a fraction of the
 * int16 range, so it is scaled up by 4 over a plain 16 to 12 bit conversion;
 * the rare peaks above the DAC range are clamped.
 */
#define SAMPLE_SHIFT 2

static struct streamCtx *volatile stream;
static volatile uint32_t pos;   /* Stream sample being output */
static volatile uint32_t sub;   /* Output sample within it */
static volatile uint32_t ratio; /* Output samples per stream sample */
static volatile int32_t prev;   /* Previous stream sample, DAC counts */
static volatile bool stopRequest;
static dacAt32_hook hooks[DAC_AT32_MAX_HOOKS];
static volatile size_t numHooks;
static volatile dacAt32_tick tickFn;
static K_SEM_DEFINE(syncSem, 0, 1);
/* Sync points reached before the previous one was consumed: late refills */
volatile uint32_t dacAt32_underruns;
/* Interrupt cost: sum of cycles and number of interrupts, and the maximum */
volatile uint32_t dacAt32_isrCycles;
volatile uint32_t dacAt32_isrCount;
volatile uint32_t dacAt32_isrMax;

/*
 * Advance in the stream; at the middle and at the end of the buffer, wake up
 * the thread waiting in sync() and stop if requested.
 */
static AUDIO_ISR_CODE void advance(struct streamCtx *ctx, int32_t cur)
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
    if (stopRequest || (end && (ctx->bufMode == BUF_LINEAR)))
        ctx->running = 0;

    if (k_sem_count_get(&syncSem) != 0)
        dacAt32_underruns++;
    k_sem_give(&syncSem);
}

static AUDIO_ISR_CODE void tmr6Isr(const void *arg)
{
    (void)arg;

    if ((TMR6->ists & TMR_OVF_FLAG) == 0)
        return;

    TMR6->ists = ~TMR_OVF_FLAG;
    uint32_t t0 = DWT->CYCCNT;

    dacAt32_tick tick = tickFn;
    if (tick != NULL)
        tick();

    int32_t out = 0;
    struct streamCtx *ctx = stream;
    if ((ctx != NULL) && (ctx->running != 0)) {
        int32_t cur = ctx->buffer[pos] >> SAMPLE_SHIFT;
        if (cur > DAC_AT32_MAX)
            cur = DAC_AT32_MAX;
        if (cur < -DAC_AT32_MAX)
            cur = -DAC_AT32_MAX;
        out = prev + (((cur - prev) * (int32_t)sub) / (int32_t)ratio);
        advance(ctx, cur);
    }

    for (size_t i = 0; i < numHooks; i++)
        out = hooks[i](out);

    if (out > DAC_AT32_MAX)
        out = DAC_AT32_MAX;
    if (out < -DAC_AT32_MAX)
        out = -DAC_AT32_MAX;

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
    dac_trigger_enable(DAC1_SELECT, FALSE);
    dac_1_data_set(DAC1_12BIT_RIGHT, DAC_MID);
    dac_enable(DAC1_SELECT, TRUE);

    /* Cycle counter, for the interrupt cost figures */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    tmr_base_init(TMR6, (TIMER_CLOCK / DAC_AT32_RATE) - 1, 0);
    tmr_cnt_dir_set(TMR6, TMR_COUNT_UP);
    tmr_interrupt_enable(TMR6, TMR_OVF_INT, TRUE);

    IRQ_CONNECT(TMR6_GLOBAL_IRQn, TMR6_IRQ_PRIO, tmr6Isr, NULL, 0);
    irq_enable(TMR6_GLOBAL_IRQn);

    tmr_counter_enable(TMR6, TRUE);
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

static int dacAt32_start(const uint8_t instance, const void *config,
                         struct streamCtx *ctx)
{
    (void)instance;
    (void)config;

    if ((ctx == NULL) || (ctx->running != 0) || (ctx->sampleRate == 0))
        return -EINVAL;
    if ((DAC_AT32_RATE % ctx->sampleRate) != 0)
        return -EINVAL;
    if ((stream != NULL) && (stream->running != 0))
        return -EBUSY;

    unsigned int key = irq_lock();
    pos = 0;
    sub = 0;
    prev = 0;
    ratio = DAC_AT32_RATE / ctx->sampleRate;
    stopRequest = false;
    k_sem_reset(&syncSem);
    ctx->running = 1;
    stream = ctx;
    irq_unlock(key);

    return 0;
}

static int dacAt32_data(struct streamCtx *ctx, stream_sample_t **buf)
{
    size_t half = ctx->bufSize / 2;

    /* The idle half is the one not being output */
    if (pos < half)
        *buf = &ctx->buffer[half];
    else
        *buf = &ctx->buffer[0];

    return half;
}

static int dacAt32_sync(struct streamCtx *ctx, uint8_t dirty)
{
    (void)dirty;

    if (ctx->running == 0)
        return -1;

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
    unsigned int key = irq_lock();
    ctx->running = 0;
    /* Only the stream being output is detached, not one being started */
    bool current = (ctx == stream);
    if (current)
        stream = NULL;
    irq_unlock(key);

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
