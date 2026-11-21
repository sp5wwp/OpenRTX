/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <errno.h>
#include <stddef.h>
#include "drivers/baseband/BK4829.h"
#include "peripherals/spi.h"
#include "interfaces/delays.h"
#include "core/utils.h"

struct bk4829_reg_init {
    uint8_t reg;
    uint16_t val;
};

/*
 * Initialization values. Where the two reference firmwares differ, the value
 * of the RT-950 Pro is used and the alternative is reported in the comment.
 * Registers which are not documented in the Beken register table are marked
 * with "(undoc)".
 */
static const struct bk4829_reg_init init_regs[] = {
    { BK4829_REG_37, 0x9D1F }, /* DSP enable, LDO selection */
    { BK4829_REG_13, 0x03DF }, /* RX AGC table */
    { BK4829_REG_12, 0x03DB },
    { BK4829_REG_11, 0x033A },
    { BK4829_REG_10, 0x0318 },
    { BK4829_REG_14, 0x0210 },
    { BK4829_REG_49, 0x2AB2 },
    { BK4829_REG_7B, 0x73DC }, /* RSSI table */
    { BK4829_REG_1C, 0x0F80 }, /* K5: 0x07C0 */
    { BK4829_REG_1D, 0xE555 },
    { BK4829_REG_1E, 0x4C58 },
    { BK4829_REG_1F, 0x865A }, /* K5: 0xC65A */
    { BK4829_REG_3E, 0x94C6 }, /* Band selection threshold */
    { BK4829_REG_3F, 0x07FE }, /* Interrupt enable. K5: 0 */
    { BK4829_REG_25, 0xC1BA }, /* (undoc) */
    { BK4829_REG_3A, 0x9A7C }, /* (undoc) */
    { BK4829_REG_19, 0x1041 }, /* MIC AGC enabled */
    { BK4829_REG_28, 0x0B40 }, /* Expander */
    { BK4829_REG_29, 0xAA00 }, /* Compressor */
    { BK4829_REG_2A, 0xCC00 }, /* K5: 0x6600 */
    { BK4829_REG_2C, 0x0022 }, /* K5: 0x1822 */
    { BK4829_REG_2F, 0x9890 },
    { BK4829_REG_53, 0x2028 },
    { BK4829_REG_7E, 0x303E }, /* AGC mode */
    { BK4829_REG_46, 0x6050 }, /* VOX on threshold. K5: 0x600A */
    { BK4829_REG_4A, 0x5430 },
    { BK4829_REG_4D, 0xA004 }, /* Squelch glitch threshold */
    { BK4829_REG_4E, 0x3815 }, /* Squelch delays and glitch threshold */
    { BK4829_REG_4F, 0x3F3B }, /* Squelch ex-noise thresholds */
    { BK4829_REG_77, 0xCCEF }, /* K5: 0x88EF */
};

/* Written after the deviation setting */
static const struct bk4829_reg_init init_regs_post[] = {
    { BK4829_REG_7D, 0xE912 }, /* Mic sensitivity 0x12. K5: 0xE920 */
    { BK4829_REG_48, 0xB3FF }, /* AF gains. K5: 0x33A8 */
    { BK4829_REG_74, 0xDF22 }, /* 3000Hz AF response, TX */
    { BK4829_REG_44, 0x91C1 }, /* 300Hz AF response, TX */
    { BK4829_REG_45, 0x3040 },
    { BK4829_REG_75, 0xE61C }, /* 3000Hz AF response, RX */
    { BK4829_REG_54, 0x91C1 },
    { BK4829_REG_55, 0x3040 },
    { BK4829_REG_47, 0x6142 }, /* AF output: normal. Bit 1 is (undoc) */
};

static inline uint16_t bk4829_update_reg(const struct BK4829 *dev,
                                         bk4829_reg_t reg, uint16_t mask,
                                         uint16_t value)
{
    uint16_t data = (bk4829_read_reg(dev, reg) & ~mask) | (value & mask);
    bk4829_write_reg(dev, reg, data);
    return data;
}

uint16_t bk4829_read_reg(const struct BK4829 *dev, bk4829_reg_t reg)
{
    const uint8_t cmd = reg | BK4829_REG_READ;
    uint8_t rx[2];

    spiDevice_acquire(dev->bus);
    gpioPin_clear(&dev->scn);
    delayUs(1);

    spiDevice_send(dev->bus, &cmd, 1);
    spiDevice_receive(dev->bus, rx, sizeof(rx));

    delayUs(1);
    gpioPin_set(&dev->scn);
    spiDevice_release(dev->bus);

    return ((uint16_t)rx[0] << 8) | rx[1];
}

void bk4829_write_reg(const struct BK4829 *dev, bk4829_reg_t reg, uint16_t data)
{
    const uint8_t cmd = reg | BK4829_REG_WRITE;
    const uint8_t tx[2] = { (data >> 8) & 0xFF, data & 0xFF };

    spiDevice_acquire(dev->bus);
    gpioPin_clear(&dev->scn);
    delayUs(1);

    spiDevice_send(dev->bus, &cmd, 1);
    spiDevice_send(dev->bus, tx, sizeof(tx));

    delayUs(1);
    gpioPin_set(&dev->scn);
    spiDevice_release(dev->bus);
}

void bk4829_init(const struct BK4829 *dev)
{
    /* Configure CS as output, idle high */
    gpioPin_setMode(&dev->scn, OUTPUT);
    gpioPin_set(&dev->scn);

    bk4829_write_reg(dev, BK4829_REG_00, BIT(15)); /* Soft reset */
    bk4829_write_reg(dev, BK4829_REG_00, 0);       /* Normal operation */

    for (size_t i = 0; i < sizeof(init_regs) / sizeof(init_regs[0]); i++)
        bk4829_write_reg(dev, init_regs[i].reg, init_regs[i].val);

    /* RF TX deviation: keep the enable bit, set the default deviation */
    bk4829_update_reg(dev, BK4829_REG_40, 0x0FFF, 0x04D2);

    for (size_t i = 0; i < sizeof(init_regs_post) / sizeof(init_regs_post[0]);
         i++)
        bk4829_write_reg(dev, init_regs_post[i].reg, init_regs_post[i].val);

    /*
     * The DTMF decoder coefficients (REG09) are not written, as the
     * original RT-950 Pro firmware relies on the reset values. The PA is
     * left disabled (REG36 reset value) until bk4829_set_tx_power().
     */
}

uint16_t bk4829_int_get(const struct BK4829 *dev, bk4829_int_t interrupt)
{
    if ((bk4829_read_reg(dev, BK4829_REG_0C) & BIT(0)) == 0)
        return 0;

    return bk4829_read_reg(dev, BK4829_REG_02) & interrupt;
}

void bk4829_int_enable(const struct BK4829 *dev, bk4829_int_t interrupt)
{
    bk4829_update_reg(dev, BK4829_REG_3F, interrupt, interrupt);
}

void bk4829_int_disable(const struct BK4829 *dev, bk4829_int_t interrupt)
{
    bk4829_update_reg(dev, BK4829_REG_3F, interrupt, 0);
}

int bk4829_set_freq(const struct BK4829 *dev, uint32_t freq)
{
    const bool in_range =
        (freq >= BK4829_FREQ_LOW1_MIN && freq <= BK4829_FREQ_LOW1_MAX)
        || (freq >= BK4829_FREQ_HIGH_MIN && freq <= BK4829_FREQ_HIGH_MAX);

    if (!in_range)
        return -EINVAL;

    freq = freq / 10; /* Convert to 10 Hz units */
    bk4829_write_reg(dev, BK4829_REG_39, (freq >> 16) & 0xFFFF);
    bk4829_write_reg(dev, BK4829_REG_38, freq & 0xFFFF);

    return 0;
}

void bk4829_rx_on(const struct BK4829 *dev)
{
    /*
     * Enabling RX with bit 15 set triggers the VCO calibration, which is
     * performed on the rising edge: start from a state where it is clear.
     */
    bk4829_write_reg(dev, BK4829_REG_30, BK4829_REG30_AF_DAC_ENABLE);
    delayUs(BK4829_VCO_CAL_US);
    bk4829_write_reg(dev, BK4829_REG_30, BK4829_REG30_RX_PRESET);
}

void bk4829_tx_on(const struct BK4829 *dev)
{
    /* As the vendor firmware does before keying (RT-950 Pro V0.27, 0x0801EAF8) */
    bk4829_update_reg(dev, BK4829_REG_31, BK4829_REG31_UNDOC1, 0);

    bk4829_write_reg(dev, BK4829_REG_30, 0x00);
    delayUs(BK4829_VCO_CAL_US);
    bk4829_write_reg(dev, BK4829_REG_30, BK4829_REG30_TX_PRESET);
}

void bk4829_rtx_off(const struct BK4829 *dev)
{
    bk4829_write_reg(dev, BK4829_REG_30, 0x00);
    bk4829_write_reg(dev, BK4829_REG_30, BK4829_REG30_AF_DAC_ENABLE);
}

void bk4829_set_filter_bandwidth(const struct BK4829 *dev,
                                 bk4829_bw_t bandwidth)
{
    /*
     * The original firmware overwrites the whole register with 0x3028 (wide)
     * or 0x4008 (narrow). Only the bandwidth mode is changed here.
     */
    bk4829_update_reg(dev, BK4829_REG_43, 0x0030, BITV(bandwidth, 4));
}

void bk4829_set_tx_power(const struct BK4829 *dev, uint8_t bias, uint8_t gain1,
                         uint8_t gain2)
{
    /* Bit 7 enables the PACTL output */
    uint16_t value = BITV(bias, 8) | BIT(7) | BITV(gain1 & 0x07, 3)
                   | BITV(gain2 & 0x07, 0);

    if (bias == 0)
        value &= ~BIT(7);

    bk4829_write_reg(dev, BK4829_REG_36, value);
}

void bk4829_set_pa_gain(const struct BK4829 *dev, uint8_t gain1, uint8_t gain2)
{
    bk4829_write_reg(dev, BK4829_REG_36,
                     BK4829_REG36_UNDOC6 | BK4829_REG36_PA_GAIN1(gain1)
                         | BK4829_REG36_PA_GAIN2(gain2));
}

void bk4829_set_tx_deviation(const struct BK4829 *dev, uint16_t deviation)
{
    bk4829_update_reg(dev, BK4829_REG_40, BK4829_REG40_DEVIATION_MASK,
                      deviation & BK4829_REG40_DEVIATION_MASK);
}

/* Tone 1 level on the receive audio output (REG_70 bits 14:8) */
#define RX_TONE_GAIN 0x20

uint16_t bk4829_tone_word(uint32_t hz)
{
    return (uint16_t)((((uint64_t)hz << 28) + 13000000U) / 26000000U);
}

float bk4829_tone_hz(uint16_t word)
{
    return (float)word * (26e6f / 268435456.0f);
}

void bk4829_rx_tone_start(const struct BK4829 *dev, uint16_t word)
{
    bk4829_write_reg(dev, BK4829_REG_71, word);
    bk4829_update_reg(dev, BK4829_REG_70, BK4829_REG70_TONE1_ENABLE | 0x7F00,
                      BK4829_REG70_TONE1_ENABLE | BITV(RX_TONE_GAIN, 8));
    bk4829_set_af(dev, BK4829_AF_TYPE_TONE_RX);
}

void bk4829_rx_tone_stop(const struct BK4829 *dev)
{
    bk4829_set_af(dev, BK4829_AF_TYPE_MUTE);
    bk4829_update_reg(dev, BK4829_REG_70, BK4829_REG70_TONE1_ENABLE, 0);
}

void bk4829_set_mic_gain(const struct BK4829 *dev, uint8_t gain)
{
    bk4829_update_reg(dev, BK4829_REG_7D, 0x003F, gain);
}

void bk4829_enable_tx_ctcss(const struct BK4829 *dev, uint16_t frequency)
{
    /* Control word is freq(Hz) * 20.64888 for a 26MHz crystal */
    const uint32_t ctrl_word = ((uint32_t)frequency * 206488 + 50000) / 100000;

    /* 0x40 is the TX gain used by the K5 firmware */
    bk4829_write_reg(dev, BK4829_REG_51,
                     BK4829_REG51_TX_CTCDSS_ENABLE
                         | BK4829_REG51_CTCSCSS_MODE_SEL
                         | BK4829_REG51_CTCDCSS_TX_GAIN1(0x40));
    bk4829_write_reg(dev, BK4829_REG_07,
                     BK4829_REG07_MODE_CTC1 | (ctrl_word & 0x1FFF));
}

void bk4829_enable_rx_ctcss(const struct BK4829 *dev, uint16_t frequency)
{
    const uint32_t ctrl_word = ((uint32_t)frequency * 206488 + 50000) / 100000;

    bk4829_update_reg(dev, BK4829_REG_51, BK4829_REG51_CTCSCSS_MODE_SEL,
                      BK4829_REG51_CTCSCSS_MODE_SEL);
    bk4829_write_reg(dev, BK4829_REG_07,
                     BK4829_REG07_MODE_CTC1 | (ctrl_word & 0x1FFF));
}

void bk4829_enable_ctcss2(const struct BK4829 *dev, uint16_t frequency)
{
    if (frequency == 0)
        return;

    /* Control word is 25391 / freq(Hz) for a 26MHz crystal */
    const uint32_t ctrl_word = (253910 + frequency / 2) / frequency;

    bk4829_update_reg(dev, BK4829_REG_51, BK4829_REG51_CTCSCSS_MODE_SEL,
                      BK4829_REG51_CTCSCSS_MODE_SEL);
    bk4829_write_reg(dev, BK4829_REG_07,
                     BK4829_REG07_MODE_CTC2 | (ctrl_word & 0x1FFF));
}

void bk4829_enable_tx_cdcss(const struct BK4829 *dev, uint8_t code_type,
                            uint8_t bit_sel, uint32_t cdcss_code)
{
    /* 0x33 is the TX gain used by the K5 firmware */
    bk4829_write_reg(dev, BK4829_REG_51,
                     BK4829_REG51_TX_CTCDSS_ENABLE | BITV(code_type & 1, 13)
                         | BITV(bit_sel & 1, 11)
                         | BK4829_REG51_CTCDCSS_TX_GAIN1(0x33));

    /* Baud rate is 134.4Hz * 20.64888 */
    bk4829_write_reg(dev, BK4829_REG_07, BK4829_REG07_MODE_CDCSS | 0x0AD7);

    /* Bit 15 selects the upper 12 bits of the code */
    bk4829_write_reg(dev, BK4829_REG_08,
                     BIT(15) | ((cdcss_code >> 12) & 0xFFF));
    bk4829_write_reg(dev, BK4829_REG_08, cdcss_code & 0xFFF);
}

void bk4829_disable_ctdcss(const struct BK4829 *dev)
{
    bk4829_update_reg(dev, BK4829_REG_51, BK4829_REG51_TX_CTCDSS_ENABLE, 0);
}

bool bk4829_get_ctcss(const struct BK4829 *dev)
{
    return (bk4829_read_reg(dev, BK4829_REG_0C) & BIT(10)) != 0;
}

void bk4829_enable_vox(const struct BK4829 *dev, uint8_t delay_time,
                       uint8_t interval_time, uint16_t threshold_on,
                       uint16_t threshold_off)
{
    bk4829_update_reg(dev, BK4829_REG_46, 0x07FF, threshold_on);
    bk4829_write_reg(dev, BK4829_REG_79,
                     BITV(interval_time & 0x1F, 11) | (threshold_off & 0x07FF));
    bk4829_update_reg(dev, BK4829_REG_7A, 0xF000, BITV(delay_time & 0x0F, 12));
    bk4829_update_reg(dev, BK4829_REG_31, BIT(2), BIT(2));
}

bool bk4829_get_vox(const struct BK4829 *dev)
{
    return (bk4829_read_reg(dev, BK4829_REG_0C) & BIT(2)) != 0;
}

void bk4829_set_squelch(const struct BK4829 *dev, uint8_t RTSO, uint8_t RTSC,
                        uint8_t ETSO, uint8_t ETSC, uint8_t GTSO, uint8_t GTSC)
{
    bk4829_write_reg(dev, BK4829_REG_78, (RTSO << 8) | RTSC);

    /* Only change threshold fields, preserve timing and reserved bits */
    bk4829_update_reg(dev, BK4829_REG_4F, 0x7F7F,
                      ((ETSC & 0x7F) << 8) | (ETSO & 0x7F));
    bk4829_update_reg(dev, BK4829_REG_4D, 0x00FF, GTSC);
    bk4829_update_reg(dev, BK4829_REG_4E, 0x00FF, GTSO);
}

/* Speech filters and emphasis, on transmit and receive */
#define REG2B_AF_FILTERS_DISABLE                                             \
    (BK4829_REG2B_AF_RX_HPF300_DISABLE | BK4829_REG2B_AF_RX_LPF3K_DISABLE    \
     | BK4829_REG2B_AF_RX_DEEMPH_DISABLE | BK4829_REG2B_AF_TX_HPF300_DISABLE \
     | BK4829_REG2B_AF_TX_LPF1_DISABLE | BK4829_REG2B_AF_TX_PREEMPH_DISABLE)

/*
 * Audio processing fields, with their values by enum bk4829_audio_mode:
 * voice (the values bk4829_init() sets), then bypass. Unlike the BK4819's,
 * REG_47 bit 1 stays set in both.
 */
struct audioModeField {
    bk4829_reg_t reg;
    uint16_t mask;
    uint16_t value[2];
};

static const struct audioModeField audioModeFields[] = {
    { BK4829_REG_2B, REG2B_AF_FILTERS_DISABLE, { 0, REG2B_AF_FILTERS_DISABLE } },
    { BK4829_REG_31, BK4829_REG31_COMPANDER | BK4829_REG31_VOX, { 0, 0 } },
    { BK4829_REG_47,
      BK4829_REG47_AF_TX_FILTER_BYPASS,
      { 0, BK4829_REG47_AF_TX_FILTER_BYPASS } },
    { BK4829_REG_70,
      BK4829_REG70_TONE1_ENABLE | BK4829_REG70_TONE2_ENABLE,
      { 0, 0 } },
    { BK4829_REG_7E,
      BK4829_REG7E_TX_DCF_BW_MASK | BK4829_REG7E_RX_DCF_BW_MASK,
      { BK4829_REG7E_TX_DCF_BW(BK4829_DEFAULT_TX_DCF_BW)
            | BK4829_REG7E_RX_DCF_BW(BK4829_DEFAULT_RX_DCF_BW),
        0 } },
};

void bk4829_set_audio_mode(const struct BK4829 *dev,
                           enum bk4829_audio_mode mode)
{
    const bool bypass = (mode == BK4829_AUDIO_BYPASS);

    for (size_t i = 0; i < ARRAY_SIZE(audioModeFields); i++) {
        const struct audioModeField *field = &audioModeFields[i];
        bk4829_update_reg(dev, field->reg, field->mask, field->value[bypass]);
    }

    if (bypass)
        return;

    bk4829_set_rx_audio_gain(dev, BK4829_DEFAULT_RX_AUDIO_GAIN1,
                             BK4829_DEFAULT_RX_AUDIO_GAIN2,
                             BK4829_DEFAULT_AF_DAC_GAIN);
}

void bk4829_set_rx_audio_gain(const struct BK4829 *dev, uint8_t gain1,
                              uint8_t gain2, uint8_t dacGain)
{
    const uint16_t mask = BK4829_REG48_AF_RX_GAIN1_MASK
                        | BK4829_REG48_AF_RX_GAIN2_MASK
                        | BK4829_REG48_AF_DAC_GAIN_MASK;
    const uint16_t value = BK4829_REG48_AF_RX_GAIN1(gain1)
                         | BK4829_REG48_AF_RX_GAIN2(gain2)
                         | BK4829_REG48_AF_DAC_GAIN(dacGain);

    bk4829_update_reg(dev, BK4829_REG_48, mask, value);
}

void bk4829_set_af(const struct BK4829 *dev, bk4829_af_type_t af)
{
    /* Preserve polarity and TX filter bypass */
    bk4829_update_reg(dev, BK4829_REG_47, 0x0F00, BITV(af & 0x0F, 8));
}

int16_t bk4829_get_rssi(const struct BK4829 *dev)
{
    return ((bk4829_read_reg(dev, BK4829_REG_67) & 0x01FF) / 2) - 160;
}

uint16_t bk4829_get_mic_level(const struct BK4829 *dev)
{
    /* Bits 6:0 of the AF TX/RX input amplitude */
    return (bk4829_read_reg(dev, BK4829_REG_6F) & 0x7F) * 2;
}

void bk4829_enable_freq_scan(const struct BK4829 *dev, uint8_t scan_time)
{
    /* The scan time can only be changed while the scan is disabled */
    bk4829_update_reg(dev, BK4829_REG_32, BIT(0) | BITV(3, 14),
                      BITV(scan_time & 3, 14));
    bk4829_update_reg(dev, BK4829_REG_32, BIT(0), BIT(0));
}

void bk4829_disable_freq_scan(const struct BK4829 *dev)
{
    bk4829_update_reg(dev, BK4829_REG_32, BIT(0), 0);
}

bool bk4829_get_scan_freq_flag(const struct BK4829 *dev)
{
    return (bk4829_read_reg(dev, BK4829_REG_0D) & BIT(15)) != 0;
}

uint32_t bk4829_get_scan_freq(const struct BK4829 *dev)
{
    const uint32_t high = bk4829_read_reg(dev, BK4829_REG_0D) & 0x07FF;
    const uint32_t low = bk4829_read_reg(dev, BK4829_REG_0E);

    return ((high << 16) | low) * 10;
}

void bk4829_beep_start(const struct BK4829 *dev, uint16_t frequency)
{
    /* Tone1 control word is freq(Hz) * 10.32444 for a 26MHz crystal */
    const uint16_t tone_word = (((uint64_t)frequency * 1353245) + (1UL << 16))
                            >> 17;

    bk4829_update_reg(dev, BK4829_REG_50, BIT(15), BIT(15)); /* TX mute */
    bk4829_set_af(dev, BK4829_AF_TYPE_BEEP);
    bk4829_write_reg(dev, BK4829_REG_70, BIT(15) | BITV(96, 8));
    bk4829_write_reg(dev, BK4829_REG_71, tone_word);
    bk4829_write_reg(dev, BK4829_REG_30, 0);
    bk4829_write_reg(dev, BK4829_REG_30,
                     BK4829_REG30_AF_DAC_ENABLE | BK4829_REG30_UNDOC_8
                         | BK4829_REG30_TX_DSP_ENABLE);
    bk4829_update_reg(dev, BK4829_REG_50, BIT(15), 0);
}

void bk4829_beep_stop(const struct BK4829 *dev)
{
    bk4829_update_reg(dev, BK4829_REG_50, BIT(15), BIT(15)); /* TX mute */
    bk4829_update_reg(dev, BK4829_REG_70, BIT(15), 0);
    bk4829_set_af(dev, BK4829_AF_TYPE_FM);
}
