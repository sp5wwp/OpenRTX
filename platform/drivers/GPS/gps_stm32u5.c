/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <hwconfig.h>
#include "nmea_rbuf.h"
#include "gps_stm32u5.h"

/*
 * NMEA receiver on an STM32U5 USART, receive only, one interrupt per byte
 * into the NMEA ring buffer. The USART kernel clock is the default one,
 * PCLK, equal to the core clock (APB prescalers at 1).
 */

#if defined(CONFIG_GPS_STM32U5_USART1)
#define PORT USART1
#define IRQ USART1_IRQn
#define HANDLER _Z17USART1_IRQHandlerv
#define CLK_EN() (RCC->APB2ENR |= RCC_APB2ENR_USART1EN)
#define CLK_DIS() (RCC->APB2ENR &= ~RCC_APB2ENR_USART1EN)
#elif defined(CONFIG_GPS_STM32U5_USART2)
#define PORT USART2
#define IRQ USART2_IRQn
#define HANDLER _Z17USART2_IRQHandlerv
#define CLK_EN() (RCC->APB1ENR1 |= RCC_APB1ENR1_USART2EN)
#define CLK_DIS() (RCC->APB1ENR1 &= ~RCC_APB1ENR1_USART2EN)
#elif defined(CONFIG_GPS_STM32U5_USART3)
#define PORT USART3
#define IRQ USART3_IRQn
#define HANDLER _Z17USART3_IRQHandlerv
#define CLK_EN() (RCC->APB1ENR1 |= RCC_APB1ENR1_USART3EN)
#define CLK_DIS() (RCC->APB1ENR1 &= ~RCC_APB1ENR1_USART3EN)
#else
#error No USART selected for the GPS
#endif

static struct nmeaRbuf nmea;

void HANDLER()
{
    uint32_t isr = PORT->ISR;

    // Clear overrun and framing/noise errors, which would stop reception
    PORT->ICR = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NECF;

    if (isr & USART_ISR_RXNE_RXFNE)
        nmeaRbuf_putChar(&nmea, (char)PORT->RDR);
}

void gpsStm32u5_init(const uint32_t baud)
{
    CLK_EN();
    __DSB();

    PORT->CR1 = 0;
    PORT->BRR = (SystemCoreClock + (baud / 2)) / baud;
    PORT->CR3 = USART_CR3_ONEBIT;
    PORT->CR1 = USART_CR1_RE | USART_CR1_RXNEIE_RXFNEIE;

    NVIC_SetPriority(IRQ, 14);
    nmeaRbuf_reset(&nmea);
}

void gpsStm32u5_terminate()
{
    gpsStm32u5_disable(NULL);
    CLK_DIS();
}

void gpsStm32u5_enable(void *priv)
{
    (void)priv;

    nmeaRbuf_reset(&nmea);
    PORT->CR1 |= USART_CR1_UE;
    NVIC_ClearPendingIRQ(IRQ);
    NVIC_EnableIRQ(IRQ);
}

void gpsStm32u5_disable(void *priv)
{
    (void)priv;

    PORT->CR1 &= ~USART_CR1_UE;
    NVIC_DisableIRQ(IRQ);
}

int gpsStm32u5_getNmeaSentence(void *priv, char *buf, const size_t maxLength)
{
    (void)priv;

    return nmeaRbuf_getSentence(&nmea, buf, maxLength);
}
