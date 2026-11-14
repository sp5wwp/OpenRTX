/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <drivers/ADC/adc_stm32.h>
#include <hwconfig.h>
#include <interfaces/nvmem.h>
#include <interfaces/platform.h>
#include <peripherals/gpio.h>
#include "drivers/backlight/backlight.h"
#include "drivers/USB/usb.h"
#include "drivers/I2C/i2c_bitbang.h"

void radio_setVolume(uint8_t volume);

/*
 * Power switch level on PA0 while the switch is on. The stock firmware
 * (0x080D6954) treats PA0 high as "switch off", the I/O table says the
 * opposite. At start-up the switch is necessarily on, so the level read
 * then is taken as the "on" level.
 */
static uint8_t pwrOnLevel;
static uint8_t pwrOffCount;

static const hwInfo_t hwInfo = {
    .vhf_maxFreq = 174,
    .vhf_minFreq = 136,
    .vhf_band = 1,
    .uhf_maxFreq = 520,
    .uhf_minFreq = 400,
    .uhf_band = 1,
    .hw_version = 0,
    .flags = 0,
    .name = "D890UV",
};

void platform_init()
{
    gpio_setMode(PTT_SW, INPUT_PULL_UP);
    gpio_setMode(PWR_ON_DET, INPUT_PULL_UP);

    // Hold the supply on, so that settings can be saved after the power
    // switch is turned off. Stock does the same with PC3.
    gpio_setPin(PWR_HOLD);
    gpio_setMode(PWR_HOLD, OUTPUT);
    gpio_setMode(AIN_VBAT, ANALOG);
    gpio_setMode(AIN_VOLUME, ANALOG);

    // Speaker amplifier off until audio is needed
    gpio_clearPin(SPK_EN);
    gpio_setMode(SPK_EN, OUTPUT);

    // UHF front-end filter tuning voltage from DAC1 channel 2: normal mode,
    // buffered, pin output. Mid-scale until the radio driver applies the
    // factory calibration. HFSEL 01 is required for an AHB clock between 80
    // and 160MHz.
    // Channel 1 is the transmitter power control (APC), held at zero: the
    // stock firmware drives it only while transmitting (0x0801694C).
    gpio_setMode(UHF_FILTER, ANALOG);
    gpio_setMode(APC_DAC, ANALOG);
    RCC->AHB3ENR |= RCC_AHB3ENR_DAC1EN;
    __DSB();
    DAC1->MCR = (DAC1->MCR & ~(DAC_MCR_HFSEL | DAC_MCR_MODE1 | DAC_MCR_MODE2))
              | DAC_MCR_HFSEL_0;
    DAC1->DHR12R1 = 0;
    DAC1->DHR12R2 = 0x800;
    DAC1->CR |= DAC_CR_EN1 | DAC_CR_EN2;

    gpio_clearPin(LED_DIGI_RX);
    gpio_clearPin(LED_ANL_RX);
    gpio_clearPin(LED_TX);
    gpio_setMode(LED_DIGI_RX, OUTPUT);
    gpio_setMode(LED_ANL_RX, OUTPUT);
    gpio_setMode(LED_TX, OUTPUT);

    adcStm32_init(&adc1);
    nvm_init();
    i2cBitbang_init(&i2c_bus);

    pwrOnLevel = gpio_readPin(PWR_ON_DET);
    pwrOffCount = 0;

    usb_init();
}

void platform_terminate()
{
    DAC1->DHR12R1 = 0;
    gpio_clearPin(TX_PWR_EN);
    gpio_clearPin(SPK_EN);
    gpio_clearPin(LED_ANL_RX);
    gpio_clearPin(LED_TX);

    /*
     * Release the supply, as stock does at the end of its power-off
     * sequence. The settings are already saved (state_terminate()). Done
     * before stopping the other subsystems, so that one of them hanging
     * cannot keep the radio on with the power switch off.
     */
    gpio_clearPin(PWR_HOLD);

    backlight_terminate();
    adcStm32_terminate(&adc1);
    usb_terminate();
}

/*
 * Battery voltage divider, measured on a D890UV powered from a bench supply:
 *
 *   supply [V]  6.0   6.5   7.0   8.0
 *   ADC count   7525  8150  8775  10025   (14-bit, VREF+ 3.3V)
 *
 * which is 1250 counts per volt with an offset of 25 counts: 251.8mV at the
 * pin per battery volt (divider ratio ~3.97) and a 20mV offset.
 */
#define VBAT_PIN_UV_PER_V 251770
#define VBAT_OFFSET_MV 20

uint16_t platform_getVbat()
{
    uint32_t pin = adc_getVoltage(&adc1, ADC_VBAT_CH);
    uint32_t vbat = ((uint64_t)pin * 1000) / VBAT_PIN_UV_PER_V;

    if (vbat < VBAT_OFFSET_MV)
        return 0;

    return vbat - VBAT_OFFSET_MV;
}

uint8_t platform_getMicLevel()
{
    return 0;
}

uint8_t platform_getVolumeLevel()
{
    // Potentiometer between ground and 3.3V
    uint32_t pin = adc_getVoltage(&adc1, ADC_VOL_CH);
    uint32_t level = (pin * 255) / 3300000;
    if (level > 255)
        level = 255;

    // Update the audio level only when the knob moves, ignoring ADC noise
    static uint32_t lastLevel = 0xFFFF;
    if ((level + 2 < lastLevel) || (level > lastLevel + 2)) {
        lastLevel = level;
        radio_setVolume(level);
    }

    return level;
}

int8_t platform_getChSelector()
{
    return 0;
}

bool platform_getPttStatus()
{
    return gpio_readPin(PTT_SW) == 0;
}

bool platform_pwrButtonStatus()
{
    /*
     * Called every 5ms. Like stock, require the "off" level for at least
     * 10ms before shutting down.
     */
    if (gpio_readPin(PWR_ON_DET) == pwrOnLevel) {
        pwrOffCount = 0;
        return true;
    }

    if (pwrOffCount < 2) {
        pwrOffCount++;
        return true;
    }

    return false;
}

void platform_ledOn(led_t led)
{
    switch (led) {
        case GREEN:
            gpio_setPin(LED_ANL_RX);
            break;
        case RED:
            gpio_setPin(LED_TX);
            break;
        default:
            break;
    }
}

void platform_ledOff(led_t led)
{
    switch (led) {
        case GREEN:
            gpio_clearPin(LED_ANL_RX);
            break;
        case RED:
            gpio_clearPin(LED_TX);
            break;
        default:
            break;
    }
}

/*
 * Beeps are not supported yet: the tone generator settings of the stock
 * firmware alone (0x14 frequency, 0x16 = 0x8000 enable, 0x30 level) send the
 * received noise to the speaker instead of a tone, so the speaker stays muted.
 */
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
