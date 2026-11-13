/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <errno.h>
#include "stm32u5xx.h"
#include "spi_stm32.h"

/*
 * SPI master for the STM32U5 SPI peripheral (RM0456, chapter "Serial
 * peripheral interface"): 8-bit frames, software chip select, endless
 * transfer mode (TSIZE = 0), so the clock runs only while there is data in
 * the transmit FIFO. The kernel clock is PCLK, the reset selection.
 */

static uint32_t kernelClock(const SPI_TypeDef *spi)
{
    (void)spi;

    // SPI1, SPI2 and SPI3 kernel clock: PCLK with the reset clock selection,
    // equal to SYSCLK with the bus prescalers at 1 (bsp.cpp)
    return SystemCoreClock;
}

int spiStm32_init(const struct spiDevice *dev, const uint32_t speed,
                  const uint8_t flags)
{
    SPI_TypeDef *spi = (SPI_TypeDef *)dev->priv;

    switch ((uint32_t)spi) {
        case SPI1_BASE:
            RCC->APB2ENR |= RCC_APB2ENR_SPI1EN;
            break;
        case SPI2_BASE:
            RCC->APB1ENR1 |= RCC_APB1ENR1_SPI2EN;
            break;
        case SPI3_BASE:
            RCC->APB3ENR |= RCC_APB3ENR_SPI3EN;
            break;
        default:
            return -ENODEV;
    }
    __DSB();

    // Divider 2 to 256, nearest frequency not above the requested one
    uint32_t clk = kernelClock(spi);
    uint32_t mbr = 0;
    while ((mbr < 7) && ((clk >> (mbr + 1)) > speed))
        mbr++;

    if ((clk >> (mbr + 1)) > speed)
        return -EINVAL;

    /*
     * Internal chip select high (SSI) before selecting master mode with
     * software chip select: a master seeing its chip select low raises a
     * mode fault, which clears MASTER and disables the peripheral.
     */
    spi->CR1 = SPI_CR1_SSI;
    spi->IFCR = SPI_IFCR_MODFC;
    spi->CFG1 = (mbr << SPI_CFG1_MBR_Pos) | (7 << SPI_CFG1_DSIZE_Pos);
    spi->CFG2 = SPI_CFG2_MASTER | SPI_CFG2_SSM | SPI_CFG2_AFCNTR;

    if ((flags & SPI_FLAG_CPOL) != 0)
        spi->CFG2 |= SPI_CFG2_CPOL;
    if ((flags & SPI_FLAG_CPHA) != 0)
        spi->CFG2 |= SPI_CFG2_CPHA;
    if ((flags & SPI_LSB_FIRST) != 0)
        spi->CFG2 |= SPI_CFG2_LSBFRST;

    spi->CR2 = 0;
    spi->CR1 |= SPI_CR1_SPE;
    spi->CR1 |= SPI_CR1_CSTART;

    if (dev->mutex != NULL)
        pthread_mutex_init((pthread_mutex_t *)dev->mutex, NULL);

    return 0;
}

void spiStm32_terminate(const struct spiDevice *dev)
{
    SPI_TypeDef *spi = (SPI_TypeDef *)dev->priv;

    spi->CR1 &= ~SPI_CR1_SPE;

    switch ((uint32_t)spi) {
        case SPI1_BASE:
            RCC->APB2ENR &= ~RCC_APB2ENR_SPI1EN;
            break;
        case SPI2_BASE:
            RCC->APB1ENR1 &= ~RCC_APB1ENR1_SPI2EN;
            break;
        case SPI3_BASE:
            RCC->APB3ENR &= ~RCC_APB3ENR_SPI3EN;
            break;
    }
    __DSB();

    if (dev->mutex != NULL)
        pthread_mutex_destroy((pthread_mutex_t *)dev->mutex);
}

/* Polling limit for a single byte, far above any byte time used */
#define SPI_STM32_POLL_LIMIT 100000

static int sendRecv(SPI_TypeDef *spi, const uint8_t out, uint8_t *in)
{
    uint32_t n = 0;

    while ((spi->SR & SPI_SR_TXP) == 0) {
        if (++n > SPI_STM32_POLL_LIMIT)
            return -ETIMEDOUT;
    }
    *((volatile uint8_t *)&spi->TXDR) = out;

    n = 0;
    while ((spi->SR & SPI_SR_RXP) == 0) {
        if (++n > SPI_STM32_POLL_LIMIT)
            return -ETIMEDOUT;
    }
    *in = *((volatile uint8_t *)&spi->RXDR);

    return 0;
}

int spiStm32_transfer(const struct spiDevice *dev, const void *txBuf,
                      void *rxBuf, const size_t size)
{
    SPI_TypeDef *spi = (SPI_TypeDef *)dev->priv;
    const uint8_t *tx = (const uint8_t *)txBuf;
    uint8_t *rx = (uint8_t *)rxBuf;

    for (size_t i = 0; i < size; i++) {
        uint8_t in;
        int ret = sendRecv(spi, (tx != NULL) ? tx[i] : 0x00, &in);
        if (ret < 0)
            return ret;
        if (rx != NULL)
            rx[i] = in;
    }

    return 0;
}
