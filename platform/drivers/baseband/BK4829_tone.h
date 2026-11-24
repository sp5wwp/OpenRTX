/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef BK4829_TONE_H
#define BK4829_TONE_H

#include "interfaces/audio.h"
#include "BK4829.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * M17 transmitter of the Radtel RT-950 Pro: a BK4829's tone generator as the
 * baseband source, its mic ADC off.
 *
 * The radio's DAC reaches the BK4829s only through the microphone line,
 * which the microphone shares, so a baseband sent there carries the speech
 * too. Instead the keyed chip transmits its tone 1, started at 1kHz and
 * stopped at its peak: from then on the tone's 7-bit gain (REG_70) is the
 * deviation, and it is rewritten 48000 times a second with the baseband. The
 * writes are bus slots clocked out of a buffer by a timer and DMA, so their
 * timing is exact and the CPU only refills the buffer.
 */

/**
 * Baseband output stream at 48kHz, the M17 modulator's: its symbols are
 * recovered with the matched filter and shaped again with a pulse that also
 * undoes the tone path's own response.
 */
extern const struct audioDriver bk4829_tone_audio_driver;

/**
 * Key a chip in tone transmit on a frequency, the tone stopped at its peak
 * and at the level of a zero baseband. The transmit path switch is the
 * caller's.
 *
 * @param chip: chip to key, tuned by this call.
 * @param freq: frequency in Hz.
 * @return zero on success, a negative error code otherwise.
 */
int bk4829Tone_keyUp(const struct BK4829 *chip, uint32_t freq);

/**
 * Stop transmitting and put back the registers the key-up changed.
 */
void bk4829Tone_keyDown(void);

/** Bench test levels of the tone transmitter, in place of the baseband. */
enum bk4829ToneTest {
    BK4829_TONE_TEST_OFF = 0,   ///< The baseband, normal operation
    BK4829_TONE_TEST_ZERO = 1,  ///< The zero level: an unmodulated carrier
    BK4829_TONE_TEST_SINE = 2,  ///< A 500Hz sine, 48 gain units peak
    BK4829_TONE_TEST_CODES = 3, ///< Every gain code against the zero code
    BK4829_TONE_TEST_SLOW = 4,  ///< A 1Hz square wave, 40 units each way
};

/** Bench: an enum bk4829ToneTest, set from the debug monitor. */
extern volatile uint8_t bk4829ToneTest;

/** Bench: the high-pass compensation's gain, 2 pi fc / 24kHz in Q20. */
extern volatile int32_t bk4829ToneLowFreqGain;

/**
 * Handle the bus DMA's half and full transfer interrupts.
 */
void bk4829Tone_isr(void);

#ifdef __cplusplus
}
#endif

#endif /* BK4829_TONE_H */
