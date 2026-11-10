/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "stm32u5xx.h"
#include <pthread.h>
#include <errno.h>
#include <stdbool.h>
#include "interfaces/delays.h"
#include "adc_stm32.h"

/*
 * Driver for the 14-bit ADC1 of STM32U5 devices. Bring-up sequence from
 * RM0456 rev 6, section "ADC on-off control": exit deep power-down, enable the
 * voltage regulator, calibrate, enable.
 */

/* Highest channel number of ADC1 */
#define ADC1_MAX_CHANNEL 19

/* Sampling time 391.5 ADC clock cycles (SMPx = 6) for every channel */
#define ADC_SMPR_ALL_391CYC 0x36DB6DB6

/* ADC12_COMMON CCR prescaler, divide by 4 as in the stock D890UV firmware */
#define ADC_CCR_PRESC_DIV4 (2u << ADC_CCR_PRESC_Pos)

/* Upper bound for every wait on the ADC, except calibration */
#define ADC_TIMEOUT_US 10000

/* Upper bound for the calibration, which takes much longer */
#define ADC_CAL_TIMEOUT_US 1000000

/*
 * Calibration workaround applied by the STM32CubeU5 HAL
 * (HAL_ADCEx_Calibration_Start) to ADC1 on silicon revisions from 0x3000 on:
 * load these linearity calibration factors at CALINDEX 9 before calibrating.
 */
#define ADC1_CAL_WORKAROUND_REV  0x3000
#define ADC1_CAL_WORKAROUND_IDX  9
#define ADC1_CAL_WORKAROUND_FACT 0x03021100
#define ADC1_CAL_WORKAROUND_MASK 0xFFFFFF00

/*
 * ADC1 regulator start-up time, tADCVREG_STUP in the STM32U575 datasheet.
 * Only ADC4 reports the end of the start-up with the LDORDY flag.
 */
#define ADC1_VREG_STARTUP_US 20

static bool adcReady = false;

/**
 * Wait until (reg & mask) == value, for at most ADC_TIMEOUT_US.
 *
 * @return 0 on success, -ETIMEDOUT otherwise.
 */
static int waitFlagFor(volatile uint32_t *reg, uint32_t mask, uint32_t value,
                       uint32_t timeoutUs)
{
    for (uint32_t i = 0; i < timeoutUs; i++) {
        if ((*reg & mask) == value)
            return 0;

        delayUs(1);
    }

    return -ETIMEDOUT;
}

static inline int waitFlag(volatile uint32_t *reg, uint32_t mask,
                           uint32_t value)
{
    return waitFlagFor(reg, mask, value, ADC_TIMEOUT_US);
}

/*
 * Enable the ADC. As in ADC_Enable() of the STM32CubeU5 HAL, ADEN is set again
 * while ADRDY is low, since the calibration logic can clear it when it is set
 * too soon after the end of a calibration.
 */
static int adcEnable(ADC_TypeDef *pAdc)
{
    /* Clear the ready flag by writing 1 */
    pAdc->ISR = ADC_ISR_ADRDY;

    for (uint32_t i = 0; i < ADC_TIMEOUT_US; i++) {
        if ((pAdc->ISR & ADC_ISR_ADRDY) != 0)
            return 0;

        if ((pAdc->CR & ADC_CR_ADEN) == 0)
            pAdc->CR |= ADC_CR_ADEN;

        delayUs(1);
    }

    return -ETIMEDOUT;
}

static int adcDisable(ADC_TypeDef *pAdc)
{
    pAdc->CR |= ADC_CR_ADDIS;

    return waitFlag(&pAdc->CR, ADC_CR_ADEN, 0);
}

/*
 * Offset calibration, following HAL_ADCEx_Calibration_Start of STM32CubeU5 for
 * ADC1, as called by the stock D890UV firmware (ADC_CALIB_OFFSET). The ADC must
 * be disabled on entry.
 *
 * RM0456 rev 6: writes to ADC_CR (CALINDEX, ADCALLIN), ADC_CALFACT and
 * ADC_CALFACT2 are applied asynchronously from the ADC kernel clock, so each
 * access is followed by a DMB.
 */
static int adcCalibrate(ADC_TypeDef *pAdc)
{
    uint32_t revId = (DBGMCU->IDCODE & DBGMCU_IDCODE_REV_ID)
                   >> DBGMCU_IDCODE_REV_ID_Pos;
    int ret;

    if (revId >= ADC1_CAL_WORKAROUND_REV) {
        ret = adcEnable(pAdc);
        if (ret < 0)
            return ret;

        pAdc->CR = (pAdc->CR & ~ADC_CR_CALINDEX)
                 | (ADC1_CAL_WORKAROUND_IDX << ADC_CR_CALINDEX_Pos);
        __DMB();
        pAdc->CALFACT2 = (pAdc->CALFACT2 & ~ADC1_CAL_WORKAROUND_MASK)
                       | ADC1_CAL_WORKAROUND_FACT;
        __DMB();
        pAdc->CALFACT |= ADC_CALFACT_LATCH_COEF;
        __DMB();

        ret = adcDisable(pAdc);
        if (ret < 0)
            return ret;
    }

    pAdc->CR |= ADC_CR_ADCAL;
    __DMB();

    return waitFlagFor(&pAdc->CR, ADC_CR_ADCAL, 0, ADC_CAL_TIMEOUT_US);
}

/*
 * Bring ADC1 up from reset: HCLK / 4 kernel clock as in the stock D890UV
 * firmware, regulator on, calibration, enable.
 *
 * The peripheral is reset first: ADC1 does not become ready after the AnyTone
 * bootloader has run unless it is reset (checked on the radio).
 */
static int adcStart(ADC_TypeDef *pAdc, bool calibrate)
{
    int ret;

    RCC->AHB2RSTR1 |= RCC_AHB2RSTR1_ADC12RST;
    __DSB();
    RCC->AHB2RSTR1 &= ~RCC_AHB2RSTR1_ADC12RST;
    __DSB();

    RCC->CCIPR3 &= ~RCC_CCIPR3_ADCDACSEL;
    ADC12_COMMON->CCR = (ADC12_COMMON->CCR & ~ADC_CCR_PRESC) | ADC_CCR_PRESC_DIV4;

    /*
     * Write ADC_CR as a whole, as the HAL does: ADEN may only be set while all
     * the other ADC_CR bits but ADVREGEN are 0 (RM0456).
     */
    pAdc->CR = 0;
    pAdc->CR = ADC_CR_ADVREGEN;
    delayUs(ADC1_VREG_STARTUP_US);

    if (calibrate) {
        ret = adcCalibrate(pAdc);
        if (ret < 0)
            return ret;
    }

    pAdc->SMPR1 = ADC_SMPR_ALL_391CYC;
    pAdc->SMPR2 = ADC_SMPR_ALL_391CYC;

    return adcEnable(pAdc);
}

int adcStm32_init(const struct Adc *adc)
{
    if ((uint32_t)adc->priv != ADC1_BASE)
        return -EINVAL;

    /* ADC1 needs the VDDA independent analog supply to be declared valid */
    RCC->AHB3ENR |= RCC_AHB3ENR_PWREN;
    __DSB();
    PWR->SVMCR |= PWR_SVMCR_ASV;

    RCC->AHB2ENR1 |= RCC_AHB2ENR1_ADC12EN;
    __DSB();

    if (adc->mutex != NULL)
        pthread_mutex_init((pthread_mutex_t *)adc->mutex, NULL);

    ADC_TypeDef *pAdc = (ADC_TypeDef *)adc->priv;

    /* An uncalibrated ADC is better than none: retry without calibration */
    int ret = adcStart(pAdc, true);
    if (ret < 0)
        ret = adcStart(pAdc, false);

    if (ret == 0)
        adcReady = true;

    return ret;
}

void adcStm32_terminate(const struct Adc *adc)
{
    /* A conversion may be in progress, wait until it finishes */
    if (adc->mutex != NULL)
        pthread_mutex_lock((pthread_mutex_t *)adc->mutex);

    ADC_TypeDef *pAdc = (ADC_TypeDef *)adc->priv;

    adcReady = false;
    adcDisable(pAdc);

    pAdc->CR &= ~ADC_CR_ADVREGEN;
    pAdc->CR |= ADC_CR_DEEPPWD;

    RCC->AHB2ENR1 &= ~RCC_AHB2ENR1_ADC12EN;
    __DSB();

    if (adc->mutex != NULL)
        pthread_mutex_destroy((pthread_mutex_t *)adc->mutex);
}

uint16_t adcStm32_sample(const struct Adc *adc, const uint32_t channel)
{
    if ((adcReady == false) || (channel > ADC1_MAX_CHANNEL))
        return 0;

    ADC_TypeDef *pAdc = (ADC_TypeDef *)adc->priv;

    pAdc->PCSEL = 1u << channel;
    pAdc->SQR1 = channel << ADC_SQR1_SQ1_Pos;
    pAdc->CR |= ADC_CR_ADSTART;

    if (waitFlag(&pAdc->ISR, ADC_ISR_EOC, ADC_ISR_EOC) < 0)
        return 0;

    return pAdc->DR;
}
