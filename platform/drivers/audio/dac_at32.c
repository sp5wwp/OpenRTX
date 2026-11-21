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
 * Timer clock: SYSCLK 120 MHz, APB1 /2, so the APB1 timers run at 120 MHz
 * (AT32F403A reference manual, clock tree).
 */

#define TIMER_CLOCK 120000000UL
#define DAC_MID 2048
#define TMR6_IRQ_PRIO 2
/* Stream samples (int16) to DAC counts (12 bit) */
#define SAMPLE_SHIFT 4

static struct streamCtx *volatile stream;
static volatile uint32_t pos;     /* Stream sample being output */
static volatile uint32_t sub;     /* Output sample within it */
static volatile uint32_t ratio;     /* Output samples per stream sample */
static volatile int32_t prev;     /* Previous stream sample, DAC counts */
static volatile bool stopRequest;
static volatile dacAt32_hook hook;
static K_SEM_DEFINE(syncSem, 0, 1);

/*
 * Advance in the stream; at the middle and at the end of the buffer, wake up
 * the thread waiting in sync() and stop if requested.
 */
static void advance(struct streamCtx *ctx, int32_t cur)
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

    k_sem_give(&syncSem);
}

static void tmr6Isr(const void *arg)
{
    (void)arg;

    if (tmr_interrupt_flag_get(TMR6, TMR_OVF_FLAG) == RESET)
        return;

    tmr_flag_clear(TMR6, TMR_OVF_FLAG);

    int32_t out = 0;
    struct streamCtx *ctx = stream;
    if ((ctx != NULL) && (ctx->running != 0)) {
        int32_t cur = ctx->buffer[pos] >> SAMPLE_SHIFT;
        out = prev + (((cur - prev) * (int32_t)sub) / (int32_t)ratio);
        advance(ctx, cur);
    }

    dacAt32_hook h = hook;
    if (h != NULL)
        out = h(out);

    if (out > DAC_AT32_MAX)
        out = DAC_AT32_MAX;
    if (out < -DAC_AT32_MAX)
        out = -DAC_AT32_MAX;

    dac_1_data_set(DAC1_12BIT_RIGHT, (uint16_t)(DAC_MID + out));
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

    tmr_base_init(TMR6, (TIMER_CLOCK / DAC_AT32_RATE) - 1, 0);
    tmr_cnt_dir_set(TMR6, TMR_COUNT_UP);
    tmr_interrupt_enable(TMR6, TMR_OVF_INT, TRUE);

    IRQ_CONNECT(TMR6_GLOBAL_IRQn, TMR6_IRQ_PRIO, tmr6Isr, NULL, 0);
    irq_enable(TMR6_GLOBAL_IRQn);

    tmr_counter_enable(TMR6, TRUE);
}

void dacAt32_setHook(dacAt32_hook newHook)
{
    hook = newHook;
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
    if (ctx->running == 0)
        return;

    stopRequest = true;
}

static void dacAt32_terminate(struct streamCtx *ctx)
{
    unsigned int key = irq_lock();
    ctx->running = 0;
    stream = NULL;
    irq_unlock(key);

    k_sem_give(&syncSem);
}

const struct audioDriver dacAt32_driver = {
    .start = dacAt32_start,
    .data = dacAt32_data,
    .sync = dacAt32_sync,
    .stop = dacAt32_stop,
    .terminate = dacAt32_terminate,
};
