/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <interfaces/platform.h>
#include <interfaces/display.h>
#include <interfaces/keyboard.h>
#include <interfaces/delays.h>
#include <hwconfig.h>
#include <peripherals/rtc.h>
#include "core/gps.h"
#include "drivers/GPS/gps_zephyr.h"
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/logging/log.h>
#include <errno.h>
#include <string.h>

LOG_MODULE_REGISTER(platform, LOG_LEVEL_INF);

#define VBAT_ADC_SPEC ADC_DT_SPEC_GET_BY_NAME(DT_PATH(zephyr_user), vbat)

static const struct adc_dt_spec vbatAdc = VBAT_ADC_SPEC;
static bool vbatAdcReady;

/*
 * Number of consecutive "switched off" readings of the power knob, polled
 * every 5ms by the OpenRTX device thread, before the radio is turned off.
 */
#define PWR_SWITCH_DEBOUNCE 20

static const hwInfo_t hwInfo = {
    .name = "RT-950Pro",
    .hw_version = 0,
    .uhf_band = 1,
    .vhf_band = 1,
    .uhf_maxFreq = 480,
    .uhf_minFreq = 400,
    .vhf_maxFreq = 174,
    .vhf_minFreq = 136,
};

static uint8_t pwrOffCount;
static bool gpsPowerReady;
static const struct device *const gpsUart = DEVICE_DT_GET(DT_ALIAS(gps));
static bool gpsInitialized;

static void gpsEnable(void *priv)
{
    (void)priv;
    /* Reset stale sentences while the module is still powered off. */
    gpsZephyr_terminate();
    gpsZephyr_init();
    gpioPin_clear(&rt950_gps_pwr);
}

static void gpsDisable(void *priv)
{
    (void)priv;
    gpioPin_set(&rt950_gps_pwr);
}

static int gpsGetSentence(void *priv, char *buf, const size_t maxLength)
{
    /*
     * The shared reader does not terminate strings and can overrun a small
     * destination on truncation. A full-ring-sized scratch buffer avoids
     * that path without changing the shared driver. Only the device thread
     * calls this function; static storage keeps it off the small stack.
     */
    static char sentence[CONFIG_NMEA_RBUF_SIZE + 1];
    (void)priv;
    unsigned int readKey = irq_lock();
    int ret = gpsZephyr_getNmeaSentence(NULL, sentence, sizeof(sentence));
    irq_unlock(readKey);
    if (ret > 0) {
        sentence[ret] = '\0';
        if ((size_t)ret >= maxLength) {
            LOG_ERR("GPS sentence exceeds destination buffer");
            if (maxLength > 0)
                buf[0] = '\0';
            ret = -EMSGSIZE;
        } else {
            memcpy(buf, sentence, ret + 1);
        }
    } else if (ret < 0) {
        LOG_ERR("GPS sentence read failed: %d", ret);
    }
    return ret;
}

static const struct gpsDevice gps = {
    .enable = gpsEnable,
    .disable = gpsDisable,
    .getSentence = gpsGetSentence,
};

static void configureGpio(const struct gpioPin *pin, uint16_t mode)
{
    int ret = gpioPin_setMode(pin, mode);
    if (ret < 0)
        LOG_ERR("GPIO configuration failed (%d)", ret);
}

void platform_init()
{
    LOG_INF("Radtel RT-950 Pro, OpenRTX %s", GIT_VERSION);

    gpioPin_set(&rt950_power_enable);
    configureGpio(&rt950_power_enable, OUTPUT);

    configureGpio(&rt950_pwr_switch, INPUT_PULL_UP);
    configureGpio(&rt950_ptt, INPUT_PULL_UP);
    configureGpio(&rt950_ptt2, INPUT_PULL_UP);

    LOG_INF("Power switch %s",
            (!gpioPin_read(&rt950_pwr_switch)) ? "on" : "off");

    gpioPin_clear(&rt950_led_red);
    configureGpio(&rt950_led_red, OUTPUT);
    gpioPin_clear(&rt950_led_green);
    configureGpio(&rt950_led_green, OUTPUT);

    gpioPin_set(&rt950_gps_pwr);
    int gpsRet = gpioPin_setMode(&rt950_gps_pwr, OUTPUT);
    if (gpsRet < 0)
        LOG_ERR("GPS power GPIO configuration failed (%d)", gpsRet);
    else
        gpsPowerReady = true;

    rtc_init();

    configureGpio(&rt950_vbat, ANALOG);
    int ret = adc_channel_setup_dt(&vbatAdc);
    if (ret < 0) {
        LOG_ERR("ADC channel setup failed (%d)", ret);
    } else {
        vbatAdcReady = true;
        LOG_INF("Battery voltage %u mV", (unsigned int)platform_getVbat());
    }
}

void platform_terminate()
{
    LOG_INF("Power off");

    display_terminate();

    gpioPin_clear(&rt950_led_red);
    gpioPin_clear(&rt950_led_green);

    kbd_terminate();
    gpsDisable(NULL);
    if (gpsInitialized)
        gpsZephyr_terminate();
    rtc_terminate();

    /* Power enable switch off */
    gpioPin_clear(&rt950_power_enable);
    delayMs(100); // Wait for the power to actually turn off
    NVIC_SystemReset();
}

uint16_t platform_getVbat()
{
    if (!vbatAdcReady)
        return 0;

    struct adc_sequence sequence = { 0 };
    uint16_t raw;

    int ret = adc_sequence_init_dt(&vbatAdc, &sequence);
    if (ret < 0) {
        LOG_ERR("ADC sequence initialization failed (%d)", ret);
        return 0;
    }
    sequence.buffer = &raw;
    sequence.buffer_size = sizeof(raw);

    /* Average a few conversions, the divider has a high output impedance */
    const int samples = 8;
    uint32_t sum = 0;

    for (int i = 0; i < samples; i++) {
        ret = adc_read_dt(&vbatAdc, &sequence);
        if (ret < 0) {
            LOG_ERR("ADC read failed (%d)", ret);
            return 0;
        }

        sum += raw;
    }

    return (uint16_t)((sum * RT950_VBAT_SCALE_NUM)
                      / (samples * RT950_VBAT_SCALE_DEN));
}

uint8_t platform_getMicLevel()
{
    return 0;
}

uint8_t platform_getVolumeLevel()
{
    return 0;
}

int8_t platform_getChSelector()
{
    return 0;
}

bool platform_getPttStatus()
{
    return !gpioPin_read(&rt950_ptt);
}

bool platform_pwrButtonStatus()
{
    if (!gpioPin_read(&rt950_pwr_switch)) {
        pwrOffCount = 0;
        return true;
    }

    if (pwrOffCount < PWR_SWITCH_DEBOUNCE) {
        pwrOffCount++;
        return true;
    }

    return false;
}

void platform_ledOn(led_t led)
{
    switch (led) {
        case GREEN:
            gpioPin_set(&rt950_led_green);
            break;
        case RED:
            gpioPin_set(&rt950_led_red);
            break;
        default:
            break;
    }
}

void platform_ledOff(led_t led)
{
    switch (led) {
        case GREEN:
            gpioPin_clear(&rt950_led_green);
            break;
        case RED:
            gpioPin_clear(&rt950_led_red);
            break;
        default:
            break;
    }
}

void platform_beepStart(uint16_t freq)
{
    (void)freq;
}

void platform_beepStop()
{
}

const hwInfo_t *platform_getHwInfo()
{
    return &hwInfo;
}

const struct gpsDevice *platform_initGps()
{
    if (!gpsPowerReady) {
        LOG_ERR("GPS power GPIO is not ready");
        return NULL;
    }
    if (!device_is_ready(gpsUart)) {
        LOG_ERR("GPS UART %s is not ready", gpsUart->name);
        return NULL;
    }
    if (!gpsInitialized) {
        gpsZephyr_init();
        gpsInitialized = true;
    }
    return &gps;
}

datetime_t platform_getCurrentTime()
{
    return rtc_getTime();
}

void platform_setTime(datetime_t time)
{
    rtc_setTime(time);
}
