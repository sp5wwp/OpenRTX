/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <interfaces/keyboard.h>
#include <hwconfig.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/logging/log.h>
#include "drivers/GPIO/gpio_zephyr.h"

LOG_MODULE_REGISTER(keyboard, LOG_LEVEL_INF);

/*
 * Radtel RT-950 Pro keypad, as measured on hardware in
 * https://github.com/jaymart1983/Radtel-RT950-Pro-Firmware (keypad.c).
 *
 * Four columns (PC0-PC3) are driven low one at a time while the four rows
 * (PD7-PD4, top to bottom) are read back. A fifth group of keys (1, 4, 7, *)
 * is not connected to any column: these keys pull their row low regardless
 * of the column state, so they are read with all the columns idle (high).
 *
 *            Row PD7   Row PD6   Row PD5   Row PD4
 *   PC0      OK        ABC       Back      V/M
 *   PC1      3         6         9         #
 *   PC2      2         5         8         0
 *   PC3      Up        Down      Left      Right
 *   none     1         4         7         *
 *
 * PC4 is the VHF/UHF antenna relay and must never be driven by the scan.
 */

#define NUM_COLS 4
#define NUM_ROWS 4

#define KBD_SETTLE_US 20

/* Quadrature transitions per mechanical detent of the encoder */
#define ENC_STEPS_PER_DETENT 2
/* Maximum number of detents buffered between two keyboard scans */
#define ENC_MAX_PENDING 8

#define USER_NODE DT_PATH(zephyr_user)

#define GPIO_PIN(port_, prop_, idx_) \
    { .port = &(port_), .pin = DT_GPIO_PIN_BY_IDX(USER_NODE, prop_, idx_) }

static const struct gpioPin cols[NUM_COLS] = {
    GPIO_PIN(GpioC, kbd_col_gpios, 0),
    GPIO_PIN(GpioC, kbd_col_gpios, 1),
    GPIO_PIN(GpioC, kbd_col_gpios, 2),
    GPIO_PIN(GpioC, kbd_col_gpios, 3),
};

static const struct gpioPin rows[NUM_ROWS] = {
    GPIO_PIN(GpioD, kbd_row_gpios, 0),
    GPIO_PIN(GpioD, kbd_row_gpios, 1),
    GPIO_PIN(GpioD, kbd_row_gpios, 2),
    GPIO_PIN(GpioD, kbd_row_gpios, 3),
};

static const struct gpioPin sideTop = GPIO_PIN(GpioE, side_key_gpios, 0);
static const struct gpioPin sideBottom = GPIO_PIN(GpioA, side_key_gpios, 1);

/* These devicetree specs are used by Zephyr's GPIO interrupt API. */
static const struct gpio_dt_spec encA =
    GPIO_DT_SPEC_GET_BY_IDX(USER_NODE, encoder_gpios, 0);
static const struct gpio_dt_spec encB =
    GPIO_DT_SPEC_GET_BY_IDX(USER_NODE, encoder_gpios, 1);
static const struct gpioPin encAPin = GPIO_PIN(GpioB, encoder_gpios, 0);
static const struct gpioPin encBPin = GPIO_PIN(GpioB, encoder_gpios, 1);

BUILD_ASSERT(DT_SAME_NODE(DT_GPIO_CTLR_BY_IDX(USER_NODE, encoder_gpios, 0),
                          DT_GPIO_CTLR_BY_IDX(USER_NODE, encoder_gpios, 1)),
             "Encoder channels must be on the same GPIO port");
BUILD_ASSERT(DT_SAME_NODE(DT_GPIO_CTLR_BY_IDX(USER_NODE, kbd_col_gpios, 0),
                          DT_NODELABEL(gpioc)));
BUILD_ASSERT(DT_SAME_NODE(DT_GPIO_CTLR_BY_IDX(USER_NODE, kbd_col_gpios, 1),
                          DT_NODELABEL(gpioc)));
BUILD_ASSERT(DT_SAME_NODE(DT_GPIO_CTLR_BY_IDX(USER_NODE, kbd_col_gpios, 2),
                          DT_NODELABEL(gpioc)));
BUILD_ASSERT(DT_SAME_NODE(DT_GPIO_CTLR_BY_IDX(USER_NODE, kbd_col_gpios, 3),
                          DT_NODELABEL(gpioc)));
BUILD_ASSERT(DT_SAME_NODE(DT_GPIO_CTLR_BY_IDX(USER_NODE, kbd_row_gpios, 0),
                          DT_NODELABEL(gpiod)));
BUILD_ASSERT(DT_SAME_NODE(DT_GPIO_CTLR_BY_IDX(USER_NODE, kbd_row_gpios, 1),
                          DT_NODELABEL(gpiod)));
BUILD_ASSERT(DT_SAME_NODE(DT_GPIO_CTLR_BY_IDX(USER_NODE, kbd_row_gpios, 2),
                          DT_NODELABEL(gpiod)));
BUILD_ASSERT(DT_SAME_NODE(DT_GPIO_CTLR_BY_IDX(USER_NODE, kbd_row_gpios, 3),
                          DT_NODELABEL(gpiod)));
BUILD_ASSERT(DT_SAME_NODE(DT_GPIO_CTLR_BY_IDX(USER_NODE, side_key_gpios, 0),
                          DT_NODELABEL(gpioe)));
BUILD_ASSERT(DT_SAME_NODE(DT_GPIO_CTLR_BY_IDX(USER_NODE, side_key_gpios, 1),
                          DT_NODELABEL(gpioa)));
BUILD_ASSERT(DT_SAME_NODE(DT_GPIO_CTLR_BY_IDX(USER_NODE, encoder_gpios, 0),
                          DT_NODELABEL(gpiob)));
BUILD_ASSERT(DT_SAME_NODE(DT_GPIO_CTLR_BY_IDX(USER_NODE, encoder_gpios, 1),
                          DT_NODELABEL(gpiob)));

static const keyboard_t matrixMap[NUM_COLS][NUM_ROWS] = {
    { KEY_ENTER, KEY_F1, KEY_ESC, KEY_F2 },
    { KEY_3, KEY_6, KEY_9, KEY_HASH },
    { KEY_2, KEY_5, KEY_8, KEY_0 },
    { KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT },
};

static const keyboard_t directMap[NUM_ROWS] = { KEY_1, KEY_4, KEY_7, KEY_STAR };

/*
 * Quadrature decoding table, indexed by (previous state << 2) | new state,
 * with state = (B << 1) | A. Clockwise rotation (A leading B) gives +1.
 */
static const int8_t quadTable[16] = {
    0, +1, -1, 0, -1, 0, 0, +1, +1, 0, 0, -1, 0, -1, +1, 0,
};

static struct gpio_callback encoderCb;
static uint8_t encState;
static int8_t encAccum;
static atomic_t knobPending;
static bool knobReported;

static uint8_t encoderRead()
{
    return (uint8_t)((gpioPin_read(&encBPin) << 1) | gpioPin_read(&encAPin));
}

static void encoderIsr(const struct device *port, struct gpio_callback *cb,
                       gpio_port_pins_t pins)
{
    ARG_UNUSED(port);
    ARG_UNUSED(cb);
    ARG_UNUSED(pins);

    uint8_t state = encoderRead();
    encAccum += quadTable[(encState << 2) | state];
    encState = state;

    if (encAccum >= ENC_STEPS_PER_DETENT) {
        encAccum = 0;
        if (atomic_get(&knobPending) < ENC_MAX_PENDING)
            atomic_inc(&knobPending);
    } else if (encAccum <= -ENC_STEPS_PER_DETENT) {
        encAccum = 0;
        if (atomic_get(&knobPending) > -ENC_MAX_PENDING)
            atomic_dec(&knobPending);
    }
}

/* Bitmask of the rows currently pulled low, bit 0 is the top row */
static uint8_t readRows()
{
    uint8_t active = 0;

    for (int r = 0; r < NUM_ROWS; r++) {
        if (!gpioPin_read(&rows[r]))
            active |= (1 << r);
    }

    return active;
}

void kbd_init()
{
    for (int c = 0; c < NUM_COLS; c++) {
        gpioPin_set(&cols[c]);
        int ret = gpioPin_setMode(&cols[c], OUTPUT);
        if (ret < 0) {
            LOG_ERR("Failed to configure keypad column (%d)", ret);
            return;
        }
    }

    for (int r = 0; r < NUM_ROWS; r++) {
        int ret = gpioPin_setMode(&rows[r], INPUT_PULL_UP);
        if (ret < 0) {
            LOG_ERR("Failed to configure keypad row (%d)", ret);
            return;
        }
    }

    int ret = gpioPin_setMode(&sideTop, INPUT_PULL_UP);
    if (ret < 0) {
        LOG_ERR("Failed to configure top side key (%d)", ret);
        return;
    }

    ret = gpioPin_setMode(&sideBottom, INPUT_PULL_UP);
    if (ret < 0) {
        LOG_ERR("Failed to configure bottom side key (%d)", ret);
        return;
    }

    ret = gpioPin_setMode(&encAPin, INPUT_PULL_UP);
    if (ret < 0) {
        LOG_ERR("Failed to configure encoder channel A (%d)", ret);
        return;
    }

    ret = gpioPin_setMode(&encBPin, INPUT_PULL_UP);
    if (ret < 0) {
        LOG_ERR("Failed to configure encoder channel B (%d)", ret);
        return;
    }
    encState = encoderRead();

    gpio_init_callback(&encoderCb, encoderIsr, BIT(encA.pin) | BIT(encB.pin));
    if (gpio_add_callback(encA.port, &encoderCb) < 0) {
        LOG_ERR("Failed to register the encoder callback");
        return;
    }

    gpio_pin_interrupt_configure_dt(&encA, GPIO_INT_EDGE_BOTH);
    gpio_pin_interrupt_configure_dt(&encB, GPIO_INT_EDGE_BOTH);
}

void kbd_terminate()
{
    gpio_pin_interrupt_configure_dt(&encA, GPIO_INT_DISABLE);
    gpio_pin_interrupt_configure_dt(&encB, GPIO_INT_DISABLE);
    gpio_remove_callback(encA.port, &encoderCb);
}

keyboard_t kbd_getKeys()
{
    keyboard_t keys = 0;

    /* Keys not connected to any column, read with all the columns idle */
    uint8_t direct = readRows();

    for (int c = 0; c < NUM_COLS; c++) {
        gpioPin_clear(&cols[c]);

        /* Rows have weak pull-ups, give them time to settle */
        k_busy_wait(KBD_SETTLE_US);
        uint8_t active = readRows() & ~direct;

        gpioPin_set(&cols[c]);

        for (int r = 0; r < NUM_ROWS; r++) {
            if (active & (1 << r))
                keys |= matrixMap[c][r];
        }
    }

    for (int r = 0; r < NUM_ROWS; r++) {
        if (direct & (1 << r))
            keys |= directMap[r];
    }

    if (!gpioPin_read(&sideTop))
        keys |= KEY_MONI;

    if (!gpioPin_read(&sideBottom))
        keys |= KEY_F3;

    /*
     * A knob event is generated only when the key state changes: report one
     * detent per scan, separated by a scan without knob keys, so that every
     * detent is seen as a new event.
     */
    if (knobReported) {
        knobReported = false;
    } else {
        atomic_val_t pending = atomic_get(&knobPending);

        if (pending > 0) {
            atomic_dec(&knobPending);
            keys |= KNOB_RIGHT;
            knobReported = true;
        } else if (pending < 0) {
            atomic_inc(&knobPending);
            keys |= KNOB_LEFT;
            knobReported = true;
        }
    }

    static keyboard_t prevKeys;
    if (keys != prevKeys) {
        LOG_DBG("Keys 0x%08x", keys);
        prevKeys = keys;
    }

    return keys;
}
