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
#include "adc_at32.h"
#include "dac_at32.h"

/*
 * Audio input on the AT32F403A ADC3. One conversion is started at each tick
 * of the DAC driver sample clock (DAC_AT32_RATE) and read at the next one,
 * the conversion time being far shorter than the sample period. Sharing the
 * DAC clock keeps input and output sample-locked, for processing that uses
 * both.
 *
 * The interrupt path runs from RAM, see dac_at32.c.
 *
 * Stream samples are the average of the ADC samples of their period, with the
 * DC level removed by a first-order high-pass, scaled from 12 to 16 bit.
 */

#define ADC_SHIFT 4
/* DC estimate time constant: 2^12 stream samples */
#define DC_SHIFT 12

static struct streamCtx *volatile stream;
static volatile uint16_t lastSample;
static volatile uint32_t pos;   /* Stream sample being acquired */
static volatile uint32_t sub;   /* ADC sample within it */
static volatile uint32_t ratio; /* ADC samples per stream sample */
static volatile int32_t acc;    /* Sum of the ADC samples of the period */
static volatile adcAt32_filter filterFn;
static volatile int32_t dc;     /* DC level, 2^DC_SHIFT scaled, ADC counts */
static volatile bool stopRequest;
static K_SEM_DEFINE(syncSem, 0, 1);

static AUDIO_ISR_CODE stream_sample_t toStream(int32_t sum)
{
    int32_t x = sum / (int32_t)ratio;
    int32_t level = dc >> DC_SHIFT;

    dc += x - level;

    int32_t s = (x - level) << ADC_SHIFT;
    if (s > INT16_MAX)
        s = INT16_MAX;
    if (s < INT16_MIN)
        s = INT16_MIN;

    return (stream_sample_t)s;
}

/*
 * Store a stream sample; at the middle and at the end of the buffer wake up
 * the thread waiting in sync() and stop if requested.
 */
static AUDIO_ISR_CODE void store(struct streamCtx *ctx, stream_sample_t s)
{
    ctx->buffer[pos++] = s;

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

static AUDIO_ISR_CODE void tick(void)
{
    /* The conversion was started one sample period ago */
    if ((ADC3->sts & ADC_CCE_FLAG) != 0)
        lastSample = (uint16_t)ADC3->odt_bit.odt;
    ADC3->ctrl2_bit.ocswtrg = TRUE;

    adcAt32_filter filter = filterFn;
    int32_t sample = (filter != NULL) ? filter(lastSample) : lastSample;

    struct streamCtx *ctx = stream;
    if ((ctx == NULL) || (ctx->running == 0))
        return;

    acc += sample;
    if (++sub < ratio)
        return;

    sub = 0;
    store(ctx, toStream(acc));
    acc = 0;
}

void adcAt32_init(uint8_t channel)
{
    adc_base_config_type cfg;
    gpio_init_type pin;

    crm_periph_clock_enable(CRM_GPIOA_PERIPH_CLOCK, TRUE);
    crm_periph_clock_enable(CRM_ADC3_PERIPH_CLOCK, TRUE);

    gpio_default_para_init(&pin);
    pin.gpio_pins = 1U << channel;
    pin.gpio_mode = GPIO_MODE_ANALOG;
    gpio_init(GPIOA, &pin);

    adc_base_default_para_init(&cfg);
    cfg.sequence_mode = FALSE;
    cfg.repeat_mode = FALSE;
    cfg.data_align = ADC_RIGHT_ALIGNMENT;
    cfg.ordinary_channel_length = 1;
    adc_base_config(ADC3, &cfg);
    /* Short enough to fit a 48 kHz sample period at any ADC clock */
    adc_ordinary_channel_set(ADC3, (adc_channel_select_type)channel, 1,
                             ADC_SAMPLETIME_71_5);
    adc_ordinary_conversion_trigger_set(ADC3, ADC3_ORDINARY_TRIG_SOFTWARE,
                                        TRUE);
    adc_enable(ADC3, TRUE);

    adc_calibration_init(ADC3);
    while (adc_calibration_init_status_get(ADC3) == SET)
        ;
    adc_calibration_start(ADC3);
    while (adc_calibration_status_get(ADC3) == SET)
        ;

    adc_ordinary_software_trigger_enable(ADC3, TRUE);
    dacAt32_setTick(tick);
}

void adcAt32_setFilter(adcAt32_filter filter)
{
    filterFn = filter;
}

uint16_t adcAt32_lastSample(void)
{
    return lastSample;
}

static int adcAt32_start(const uint8_t instance, const void *config,
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
    acc = 0;
    ratio = DAC_AT32_RATE / ctx->sampleRate;
    dc = (int32_t)lastSample << DC_SHIFT;
    stopRequest = false;
    k_sem_reset(&syncSem);
    ctx->running = 1;
    stream = ctx;
    irq_unlock(key);

    return 0;
}

static int adcAt32_data(struct streamCtx *ctx, stream_sample_t **buf)
{
    size_t half = ctx->bufSize / 2;

    /* The ready half is the one not being filled */
    if (pos < half)
        *buf = &ctx->buffer[half];
    else
        *buf = &ctx->buffer[0];

    return half;
}

static int adcAt32_sync(struct streamCtx *ctx, uint8_t dirty)
{
    (void)dirty;

    if (ctx->running == 0)
        return -1;

    k_sem_take(&syncSem, K_FOREVER);

    return 0;
}

static void adcAt32_stop(struct streamCtx *ctx)
{
    if ((ctx != stream) || (ctx->running == 0))
        return;

    stopRequest = true;
}

static void adcAt32_terminate(struct streamCtx *ctx)
{
    unsigned int key = irq_lock();
    ctx->running = 0;
    /* Only the stream being acquired is detached, not one being started */
    bool current = (ctx == stream);
    if (current)
        stream = NULL;
    irq_unlock(key);

    if (current)
        k_sem_give(&syncSem);
}

const struct audioDriver adcAt32_driver = {
    .start = adcAt32_start,
    .data = adcAt32_data,
    .sync = adcAt32_sync,
    .stop = adcAt32_stop,
    .terminate = adcAt32_terminate,
};
