/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef BK4829_H
#define BK4829_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "peripherals/gpio.h"

struct spiDevice;

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Driver for the Beken BK4829 FM/AM baseband transceiver.
 *
 * The register interface is the same as the one of the BK4819: a 3-wire,
 * SPI-like serial bus with an 8 bit command (bit 7 set for reads, bits 6:0
 * register address) followed by 16 data bits, MSB first. The chip samples
 * data on the rising edge of the clock and drives it on the falling edge
 * (clock idle low, i.e. SPI mode 0) and supports a clock up to 8MHz. After
 * the address phase of a read the chip needs at least 1us before the first
 * data bit, which is covered by the delays inserted around the chip select.
 *
 * The register values used by this driver are derived from the Beken register
 * table and datasheet and from the initialization sequences found in the
 * firmware of radios using the chip. They have NOT been validated on real
 * hardware yet.
 *
 * Differences with respect to the BK4819, that make the two drivers not
 * interchangeable:
 * - REG37 bit 15 enables the DSP and must not be overwritten by RX enable.
 * - Frequencies are programmed in units of 10Hz, in the range 18-580MHz and
 *   760-1160MHz.
 * - Tone, CTCSS and VOX registers are laid out differently, see the
 *   individual functions.
 *
 * All the frequency-dependent constants assume the default 26MHz crystal.
 */

#ifndef BIT
#define BIT(n) (1U << (n))
#endif

#ifndef BITV
#define BITV(x, y) ((x) << (y))
#endif

/*
 * Register fields. The audio ones match the BK4819's (datasheet and
 * application note, see BK4819.h); on the BK4829 they are confirmed by the
 * bench work of Ferrite (https://github.com/FerriteFW/ferrite, chip.rs and
 * drivers/bk4829) and of this port.
 */
#define BK4829_REG2B_AF_RX_HPF300_DISABLE BIT(10)
#define BK4829_REG2B_AF_RX_LPF3K_DISABLE BIT(9)
#define BK4829_REG2B_AF_RX_DEEMPH_DISABLE BIT(8)
#define BK4829_REG2B_AF_TX_HPF300_DISABLE BIT(2)
#define BK4829_REG2B_AF_TX_LPF1_DISABLE BIT(1)
#define BK4829_REG2B_AF_TX_PREEMPH_DISABLE BIT(0)
#define BK4829_REG31_COMPANDER BIT(3)
#define BK4829_REG31_VOX BIT(2)
#define BK4829_REG47_AF_TX_FILTER_BYPASS BIT(0)
/* Undocumented; the receive audio is lost when it is clear (Ferrite) */
#define BK4829_REG47_UNDOC1 BIT(1)
#define BK4829_REG48_AF_RX_GAIN1_MASK 0x0c00U
#define BK4829_REG48_AF_RX_GAIN1(x) \
    (BITV((x), 10) & BK4829_REG48_AF_RX_GAIN1_MASK)
#define BK4829_REG48_AF_RX_GAIN2_MASK 0x03f0U
#define BK4829_REG48_AF_RX_GAIN2(x) \
    (BITV((x), 4) & BK4829_REG48_AF_RX_GAIN2_MASK)
#define BK4829_REG48_AF_DAC_GAIN_MASK 0x000fU
#define BK4829_REG48_AF_DAC_GAIN(x) ((x) & BK4829_REG48_AF_DAC_GAIN_MASK)
#define BK4829_REG70_TONE1_ENABLE BIT(15)
#define BK4829_REG70_TONE2_ENABLE BIT(7)
#define BK4829_REG7E_TX_DCF_BW_MASK 0x0038U
#define BK4829_REG7E_TX_DCF_BW(x) (BITV((x), 3) & BK4829_REG7E_TX_DCF_BW_MASK)
#define BK4829_REG7E_RX_DCF_BW_MASK 0x0007U
#define BK4829_REG7E_RX_DCF_BW(x) ((x) & BK4829_REG7E_RX_DCF_BW_MASK)

/* Values bk4829_init() sets, restored by bk4829_set_audio_mode() */
#define BK4829_DEFAULT_TX_DCF_BW 7
#define BK4829_DEFAULT_RX_DCF_BW 6
#define BK4829_DEFAULT_RX_AUDIO_GAIN1 0
#define BK4829_DEFAULT_RX_AUDIO_GAIN2 0x3f
#define BK4829_DEFAULT_AF_DAC_GAIN 0xf

#define BK4829_REG_READ 0x80
#define BK4829_REG_WRITE 0x00

/* Supported RF frequency ranges, in Hz */
#define BK4829_FREQ_LOW1_MIN 18000000UL
#define BK4829_FREQ_LOW1_MAX 580000000UL
#define BK4829_FREQ_HIGH_MIN 760000000UL
#define BK4829_FREQ_HIGH_MAX 1160000000UL

/*
 * Delay between the start of the VCO calibration and the enabling of the RX
 * or TX chain, in microseconds. This is the value used by the original radio
 * firmware; the datasheet gives a synthesizer settling time of up to 300us.
 */
#ifndef BK4829_VCO_CAL_US
#define BK4829_VCO_CAL_US 50
#endif

/* REG30: enable bits of the analog and digital blocks */
#define BK4829_REG30_VCO_CALIBRATION BIT(15)
#define BK4829_REG30_UNDOC_14 BIT(14) /* Set by both RX and TX presets */
#define BK4829_REG30_RX_LINK_ENABLE BITV(0x0F, 10)
#define BK4829_REG30_AF_DAC_ENABLE BIT(9)
#define BK4829_REG30_UNDOC_8 BIT(8) /* Set by both RX and TX presets */
#define BK4829_REG30_PLL_VCO_ENABLE BITV(0x0F, 4)
#define BK4829_REG30_PA_GAIN_ENABLE BIT(3)
#define BK4829_REG30_MIC_ADC_ENABLE BIT(2)
#define BK4829_REG30_TX_DSP_ENABLE BIT(1)
#define BK4829_REG30_RX_DSP_ENABLE BIT(0)

#define BK4829_REG30_RX_PRESET 0xBFF1
#define BK4829_REG30_TX_PRESET 0xC1FE

/* REG51: CTCSS/CDCSS control */
#define BK4829_REG51_TX_CTCDSS_ENABLE BIT(15)
#define BK4829_REG51_GPIO0_CDCSS_INPUT BIT(14)
#define BK4829_REG51_TRANSMIT_NEG_CDCSS_CODE BIT(13)
#define BK4829_REG51_CTCSCSS_MODE_SEL BIT(12)
#define BK4829_REG51_CDCSS_BIT_SEL BIT(11)
#define BK4829_REG51_1050HZ_DET_MOD BIT(10)
#define BK4829_REG51_AUTO_CDCSS_BW_DISABLE BIT(9)
#define BK4829_REG51_AUTO_CTCSS_BW_DISABLE BIT(8)
#define BK4829_REG51_CTCDCSS_TX_GAIN1(x) BITV((x) & 0x7F, 0)

/* REG07: CTCSS/CDCSS detector mode, bits 15:13 */
#define BK4829_REG07_MODE_CTC1 BITV(0, 13)
#define BK4829_REG07_MODE_CTC2 BITV(1, 13)
#define BK4829_REG07_MODE_CDCSS BITV(2, 13)
#define BK4829_REG07_MODE_CTC3 BITV(3, 13)

#define BK4829_SCAN_FRE_TIME_2 0x00  /* 0.2s */
#define BK4829_SCAN_FRE_TIME_4 0x01  /* 0.4s */
#define BK4829_SCAN_FRE_TIME_8 0x02  /* 0.8s */
#define BK4829_SCAN_FRE_TIME_16 0x03 /* 1.6s */

typedef enum {
    BK4829_REG_00 = 0x00,
    BK4829_REG_02 = 0x02,
    BK4829_REG_07 = 0x07,
    BK4829_REG_08 = 0x08,
    BK4829_REG_09 = 0x09,
    BK4829_REG_0A = 0x0A,
    BK4829_REG_0C = 0x0C,
    BK4829_REG_0D = 0x0D,
    BK4829_REG_0E = 0x0E,
    BK4829_REG_10 = 0x10,
    BK4829_REG_11 = 0x11,
    BK4829_REG_12 = 0x12,
    BK4829_REG_13 = 0x13,
    BK4829_REG_14 = 0x14,
    BK4829_REG_19 = 0x19,
    BK4829_REG_1C = 0x1C,
    BK4829_REG_1D = 0x1D,
    BK4829_REG_1E = 0x1E,
    BK4829_REG_1F = 0x1F,
    BK4829_REG_25 = 0x25,
    BK4829_REG_28 = 0x28,
    BK4829_REG_29 = 0x29,
    BK4829_REG_2A = 0x2A,
    BK4829_REG_2B = 0x2B,
    BK4829_REG_2C = 0x2C,
    BK4829_REG_2F = 0x2F,
    BK4829_REG_30 = 0x30,
    BK4829_REG_31 = 0x31,
    BK4829_REG_32 = 0x32,
    BK4829_REG_33 = 0x33,
    BK4829_REG_34 = 0x34,
    BK4829_REG_35 = 0x35,
    BK4829_REG_36 = 0x36,
    BK4829_REG_37 = 0x37,
    BK4829_REG_38 = 0x38,
    BK4829_REG_39 = 0x39,
    BK4829_REG_3A = 0x3A,
    BK4829_REG_3E = 0x3E,
    BK4829_REG_3F = 0x3F,
    BK4829_REG_40 = 0x40,
    BK4829_REG_43 = 0x43,
    BK4829_REG_44 = 0x44,
    BK4829_REG_45 = 0x45,
    BK4829_REG_46 = 0x46,
    BK4829_REG_47 = 0x47,
    BK4829_REG_48 = 0x48,
    BK4829_REG_49 = 0x49,
    BK4829_REG_4A = 0x4A,
    BK4829_REG_4D = 0x4D,
    BK4829_REG_4E = 0x4E,
    BK4829_REG_4F = 0x4F,
    BK4829_REG_50 = 0x50,
    BK4829_REG_51 = 0x51,
    BK4829_REG_53 = 0x53,
    BK4829_REG_54 = 0x54,
    BK4829_REG_55 = 0x55,
    BK4829_REG_58 = 0x58,
    BK4829_REG_59 = 0x59,
    BK4829_REG_5A = 0x5A,
    BK4829_REG_5B = 0x5B,
    BK4829_REG_5C = 0x5C,
    BK4829_REG_5D = 0x5D,
    BK4829_REG_62 = 0x62,
    BK4829_REG_63 = 0x63,
    BK4829_REG_64 = 0x64,
    BK4829_REG_65 = 0x65,
    BK4829_REG_66 = 0x66,
    BK4829_REG_67 = 0x67,
    BK4829_REG_6F = 0x6F,
    BK4829_REG_70 = 0x70,
    BK4829_REG_71 = 0x71,
    BK4829_REG_72 = 0x72,
    BK4829_REG_73 = 0x73,
    BK4829_REG_74 = 0x74,
    BK4829_REG_75 = 0x75,
    BK4829_REG_77 = 0x77,
    BK4829_REG_78 = 0x78,
    BK4829_REG_79 = 0x79,
    BK4829_REG_7A = 0x7A,
    BK4829_REG_7B = 0x7B,
    BK4829_REG_7C = 0x7C,
    BK4829_REG_7D = 0x7D,
    BK4829_REG_7E = 0x7E,
} bk4829_reg_t;

/** AF output selection, REG47 bits 11:8. */
typedef enum {
    BK4829_AF_TYPE_MUTE = 0u,
    BK4829_AF_TYPE_FM = 1u,      ///< Normal demodulated audio
    BK4829_AF_TYPE_TONE_RX = 2u, ///< Tone1 on the RX path
    BK4829_AF_TYPE_BEEP = 3u,    ///< Tone1 on the TX path
    BK4829_AF_TYPE_CTCDCSS_TEST = 6u,
    BK4829_AF_TYPE_FSK_TEST = 8u,
} bk4829_af_type_t;

/**
 * Audio processing modes, see bk4829_set_audio_mode().
 */
enum bk4829_audio_mode {
    BK4829_AUDIO_VOICE,  ///< Audio filters on, voice levels.
    BK4829_AUDIO_BYPASS, ///< Audio filters bypassed, for digital modes.
};

/** RF filter bandwidth, REG43 bits 5:4. */
typedef enum {
    BK4829_BW_12_5K = 0u,
    BK4829_BW_6_25K = 1u,
    BK4829_BW_25K = 2u,
} bk4829_bw_t;

/** Interrupt sources, bit positions in REG02 and REG3F. */
typedef enum {
    BK4829_INT_FSKTF = BIT(15),
    BK4829_INT_FSKFFAE = BIT(14),
    BK4829_INT_FSKRXF = BIT(13),
    BK4829_INT_FSKFFAF = BIT(12),
    BK4829_INT_DTMFTF = BIT(11),
    BK4829_INT_CTDSTF = BIT(10),
    BK4829_INT_CDCSF = BIT(9),
    BK4829_INT_CDCSL = BIT(8),
    BK4829_INT_CTSSF = BIT(7),
    BK4829_INT_CTCSL = BIT(6),
    BK4829_INT_VOXF = BIT(5),
    BK4829_INT_VOXL = BIT(4),
    BK4829_INT_SECF = BIT(3),
    BK4829_INT_SECL = BIT(2),
    BK4829_INT_FSKRS = BIT(1)
} bk4829_int_t;

/**
 * BK4829 device data.
 *
 * Register access happens over the chip's 3-wire, SPI-like serial bus
 * (clock and bidirectional data).
 * Configure the bus for SPI mode 0, MSB first, half duplex, at most 8MHz.
 *
 * Chip select is toggled directly by this driver, since the bus transfer
 * function does not manage it. Multiple chips can share the same bus by using
 * different chip select pins.
 * Shared buses must provide a mutex protecting the complete register access.
 */
struct BK4829 {
    const struct spiDevice *bus; ///< Serial bus
    struct gpioPin scn;          ///< Chip select, active low
};

/**
 * GPIO pins of the BK4829.
 *
 * The value is the pin index used by a GPIO driver, matching the one of the
 * BK4819 GPIO driver: index n is mapped to bit (6 - n) of REG33 (output) and
 * REG0A (input), which is the datasheet pin named GPIO(6 - n).
 */
enum BK4829_GPIO_PIN_t {
    BK4829_GPIO6_PIN28 = 0,
    BK4829_GPIO5_PIN29 = 1,
    BK4829_GPIO4_PIN30 = 2,
    BK4829_GPIO3_PIN31 = 3,
    BK4829_GPIO2_PIN32 = 4,
    BK4829_GPIO1_PIN1 = 5,
    BK4829_GPIO0_PIN2 = 6,
};

typedef enum BK4829_GPIO_PIN_t BK4829_GPIO_PIN_t;

/**
 * Read a register.
 *
 * @param dev: pointer to device data.
 * @param reg: register address.
 * @return register content.
 */
uint16_t bk4829_read_reg(const struct BK4829 *dev, bk4829_reg_t reg);

/**
 * Write a register.
 *
 * @param dev: pointer to device data.
 * @param reg: register address.
 * @param data: value to be written.
 */
void bk4829_write_reg(const struct BK4829 *dev, bk4829_reg_t reg,
                      uint16_t data);

/**
 * Initialise the BK4829 chip.
 *
 * Resets the chip and loads the default configuration. The PA is left
 * disabled: bk4829_set_tx_power() has to be called before transmitting.
 *
 * @param dev: pointer to device data.
 */
void bk4829_init(const struct BK4829 *dev);

/**
 * Check for a pending interrupt.
 *
 * Reading the interrupt flags clears them in the chip.
 *
 * @param dev: pointer to device data.
 * @param interrupt: mask of interrupt sources to be tested.
 * @return the subset of the requested sources that are pending, 0 if none.
 */
uint16_t bk4829_int_get(const struct BK4829 *dev, bk4829_int_t interrupt);

/**
 * Enable interrupt sources.
 *
 * @param dev: pointer to device data.
 * @param interrupt: mask of interrupt sources.
 */
void bk4829_int_enable(const struct BK4829 *dev, bk4829_int_t interrupt);

/**
 * Disable interrupt sources.
 *
 * @param dev: pointer to device data.
 * @param interrupt: mask of interrupt sources.
 */
void bk4829_int_disable(const struct BK4829 *dev, bk4829_int_t interrupt);

/**
 * Program the RF frequency, shared by RX and TX.
 *
 * The new frequency is applied, with a VCO calibration, the next time RX or
 * TX is enabled, so bk4829_rx_on() or bk4829_tx_on() has to be called after
 * this function.
 *
 * @param dev: pointer to device data.
 * @param freq: frequency in Hz, resolution is 10Hz.
 * @return 0 on success, -EINVAL if the frequency is out of the chip range.
 */
int bk4829_set_freq(const struct BK4829 *dev, uint32_t freq);

/**
 * Enable the receiver on the programmed frequency.
 *
 * @param dev: pointer to device data.
 */
void bk4829_rx_on(const struct BK4829 *dev);

/**
 * Enable the transmitter on the programmed frequency.
 *
 * @param dev: pointer to device data.
 */
void bk4829_tx_on(const struct BK4829 *dev);

/**
 * Disable both receiver and transmitter.
 *
 * @param dev: pointer to device data.
 */
void bk4829_rtx_off(const struct BK4829 *dev);

/**
 * Set the RF filter bandwidth.
 *
 * @param dev: pointer to device data.
 * @param bandwidth: channel bandwidth.
 */
void bk4829_set_filter_bandwidth(const struct BK4829 *dev,
                                 bk4829_bw_t bandwidth);

/**
 * Set the PA output power.
 *
 * Bias is the PA bias voltage, 0 to 3.2V in 256 steps. The PA output is
 * disabled for bias = 0.
 *
 * @param dev: pointer to device data.
 * @param bias: PA bias, 0 to 255.
 * @param gain1: PA gain 1 tuning, 0 to 7.
 * @param gain2: PA gain 2 tuning, 0 to 7.
 */
void bk4829_set_tx_power(const struct BK4829 *dev, uint8_t bias, uint8_t gain1,
                         uint8_t gain2);

/**
 * Set the microphone sensitivity.
 *
 * @param dev: pointer to device data.
 * @param gain: sensitivity, 0 (minimum) to 63 (maximum), 0.5dB per step.
 */
void bk4829_set_mic_gain(const struct BK4829 *dev, uint8_t gain);

/**
 * Enable the CTCSS encoder.
 *
 * @param dev: pointer to device data.
 * @param frequency: tone frequency in 0.1Hz units.
 */
void bk4829_enable_tx_ctcss(const struct BK4829 *dev, uint16_t frequency);

/**
 * Enable the CTCSS decoder.
 *
 * @param dev: pointer to device data.
 * @param frequency: tone frequency in 0.1Hz units.
 */
void bk4829_enable_rx_ctcss(const struct BK4829 *dev, uint16_t frequency);

/**
 * Enable the CTCSS tail (phase reversal) detector.
 *
 * @param dev: pointer to device data.
 * @param frequency: tone frequency in 0.1Hz units, the chip supports values
 *        below 100Hz.
 */
void bk4829_enable_ctcss2(const struct BK4829 *dev, uint16_t frequency);

/**
 * Enable the CDCSS encoder.
 *
 * @param dev: pointer to device data.
 * @param code_type: 0 for positive code, 1 for negative code.
 * @param bit_sel: 0 for 23 bit, 1 for 24 bit code.
 * @param cdcss_code: CDCSS code word.
 */
void bk4829_enable_tx_cdcss(const struct BK4829 *dev, uint8_t code_type,
                            uint8_t bit_sel, uint32_t cdcss_code);

/**
 * Disable the CTCSS/CDCSS encoder.
 *
 * @param dev: pointer to device data.
 */
void bk4829_disable_ctdcss(const struct BK4829 *dev);

/**
 * Check if a CTCSS tone is currently detected.
 *
 * @param dev: pointer to device data.
 * @return true if the tone is detected.
 */
bool bk4829_get_ctcss(const struct BK4829 *dev);

/**
 * Enable the VOX.
 *
 * @param dev: pointer to device data.
 * @param delay_time: VOX release delay, in 128ms units, 0 to 15.
 * @param interval_time: VOX detection interval, 0 to 31.
 * @param threshold_on: voice amplitude threshold for VOX on, 11 bit.
 * @param threshold_off: voice amplitude threshold for VOX off, 11 bit.
 */
void bk4829_enable_vox(const struct BK4829 *dev, uint8_t delay_time,
                       uint8_t interval_time, uint16_t threshold_on,
                       uint16_t threshold_off);

/**
 * Check the VOX indicator.
 *
 * @param dev: pointer to device data.
 * @return true if voice is detected.
 */
bool bk4829_get_vox(const struct BK4829 *dev);

/**
 * Set the squelch thresholds.
 *
 * @param dev: pointer to device data.
 * @param RTSO: RSSI threshold for squelch open, 0.5dB/step.
 * @param RTSC: RSSI threshold for squelch close, 0.5dB/step.
 * @param ETSO: ex-noise threshold for squelch open, 7 bit.
 * @param ETSC: ex-noise threshold for squelch close, 7 bit.
 * @param GTSO: glitch threshold for squelch open.
 * @param GTSC: glitch threshold for squelch close.
 */
void bk4829_set_squelch(const struct BK4829 *dev, uint8_t RTSO, uint8_t RTSC,
                        uint8_t ETSO, uint8_t ETSC, uint8_t GTSO, uint8_t GTSC);

/**
 * Set the audio processing mode. Bypass turns off the speech filters,
 * emphasis, compander, VOX, tones and DC filters on transmit and receive, as
 * digital modes need a flat baseband. Voice restores what bk4829_init() sets,
 * including the receive audio gains.
 *
 * @param dev: pointer to device data.
 * @param mode: audio processing mode.
 */
void bk4829_set_audio_mode(const struct BK4829 *dev,
                           enum bk4829_audio_mode mode);

/**
 * Set the receive audio gains (REG_48).
 *
 * @param dev: pointer to device data.
 * @param gain1: AF RX gain 1, 0 to 3.
 * @param gain2: AF RX gain 2, 0 to 63.
 * @param dacGain: AF DAC gain, 0 to 15.
 */
void bk4829_set_rx_audio_gain(const struct BK4829 *dev, uint8_t gain1,
                              uint8_t gain2, uint8_t dacGain);

/**
 * Set the AF output mode.
 *
 * @param dev: pointer to device data.
 * @param af: AF output selection.
 */
void bk4829_set_af(const struct BK4829 *dev, bk4829_af_type_t af);

/**
 * Get the RSSI.
 *
 * @param dev: pointer to device data.
 * @return received signal strength in dBm.
 */
int16_t bk4829_get_rssi(const struct BK4829 *dev);

/**
 * Get the microphone input level.
 *
 * @param dev: pointer to device data.
 * @return microphone level.
 */
uint16_t bk4829_get_mic_level(const struct BK4829 *dev);

/**
 * Start the frequency scan.
 *
 * @param dev: pointer to device data.
 * @param scan_time: one of BK4829_SCAN_FRE_TIME_*.
 */
void bk4829_enable_freq_scan(const struct BK4829 *dev, uint8_t scan_time);

/**
 * Stop the frequency scan.
 *
 * @param dev: pointer to device data.
 */
void bk4829_disable_freq_scan(const struct BK4829 *dev);

/**
 * Check if the frequency scan is still running.
 *
 * @param dev: pointer to device data.
 * @return true if the scan is in progress and no result is available yet.
 */
bool bk4829_get_scan_freq_flag(const struct BK4829 *dev);

/**
 * Get the frequency found by the scan.
 *
 * @param dev: pointer to device data.
 * @return frequency in Hz.
 */
uint32_t bk4829_get_scan_freq(const struct BK4829 *dev);

/**
 * Start the beeper.
 *
 * @param dev: pointer to device data.
 * @param frequency: beep frequency in Hz.
 */
void bk4829_beep_start(const struct BK4829 *dev, uint16_t frequency);

/**
 * Stop the beeper.
 *
 * @param dev: pointer to device data.
 */
void bk4829_beep_stop(const struct BK4829 *dev);

#ifdef __cplusplus
}
#endif

#endif /* BK4829_H */
