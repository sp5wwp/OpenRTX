/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <interfaces/display.h>
#include <hwconfig.h>
#include <zephyr/dt-bindings/gpio/gpio.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/mipi_dbi.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/sys_io.h>

LOG_MODULE_REGISTER(display, LOG_LEVEL_INF);

/*
 * The OpenRTX framebuffer (CONFIG_SCREEN_WIDTH x CONFIG_SCREEN_HEIGHT) is
 * smaller than the panel: it is scaled up by SCALE_NUM / SCALE_DEN with a
 * nearest-neighbour filter and drawn centered on the panel, the rest of the
 * panel is kept black.
 */
#define DISPLAY_NODE DT_CHOSEN(zephyr_display)
#define MIPI_NODE DT_PARENT(DISPLAY_NODE)
#define PANEL_WIDTH DT_PROP(DISPLAY_NODE, width)
#define PANEL_HEIGHT DT_PROP(DISPLAY_NODE, height)

#define SCALE_NUM 3
#define SCALE_DEN 2
#define OUT_WIDTH ((CONFIG_SCREEN_WIDTH * SCALE_NUM) / SCALE_DEN)
#define OUT_HEIGHT ((CONFIG_SCREEN_HEIGHT * SCALE_NUM) / SCALE_DEN)
#define OUT_X_OFFSET ((PANEL_WIDTH - OUT_WIDTH) / 2)
#define OUT_Y_OFFSET ((PANEL_HEIGHT - OUT_HEIGHT) / 1)

BUILD_ASSERT(PANEL_WIDTH >= OUT_WIDTH);
BUILD_ASSERT(PANEL_HEIGHT >= OUT_HEIGHT);
BUILD_ASSERT(SCALE_NUM >= SCALE_DEN);

/* Timer driving the LCD backlight PWM output */
#define LCD_BL_TIMER_BASE \
    DT_REG_ADDR(DT_PARENT(DT_PWMS_CTLR(DT_NODELABEL(lcd_bl_pwm))))
#define TMR_BRK_OFFSET 0x44
#define TMR_BRK_OEN_BIT 15

/*
 * Fast 8080 bus write path.
 *
 * Commands are sent through the Zephyr MIPI DBI bit-bang driver, while pixel
 * data is streamed by writing directly the set/clear register of the bus
 * GPIO port: a single write puts a byte on the data lines and drives WR low,
 * a second one drives WR high, latching the byte. This is about ten times
 * faster than the generic bit-bang driver, which matters because the OpenRTX
 * UI processes one event per redraw and status events arrive every 100ms.
 *
 * The devicetree must describe the wiring this code relies on: all the bus
 * signals on the same port, the data lines on consecutive pins in ascending
 * order, WR and DC active high, CS active low.
 */
#define BUS_PORT_NODE DT_GPIO_CTLR(MIPI_NODE, wr_gpios)
#define BUS_PORT_BASE DT_REG_ADDR(BUS_PORT_NODE)
#define BUS_WR_PIN DT_GPIO_PIN(MIPI_NODE, wr_gpios)
#define BUS_CS_PIN DT_GPIO_PIN(MIPI_NODE, cs_gpios)
#define BUS_DC_PIN DT_GPIO_PIN(MIPI_NODE, dc_gpios)
#define BUS_DATA_SHIFT DT_GPIO_PIN_BY_IDX(MIPI_NODE, data_gpios, 0)

#define GPIO_CFGLR 0x00
#define GPIO_CFGHR 0x04
#define GPIO_ODT 0x0C
#define GPIO_SCR 0x10
#define PIN_SET(pin) BIT(pin)
#define PIN_CLR(pin) BIT((pin) + 16)

#define CHECK_DATA_PIN(i, _)                                                 \
    BUILD_ASSERT(DT_GPIO_PIN_BY_IDX(MIPI_NODE, data_gpios, i)                \
                 == BUS_DATA_SHIFT + i);                                     \
    BUILD_ASSERT(DT_SAME_NODE(DT_GPIO_CTLR_BY_IDX(MIPI_NODE, data_gpios, i), \
                              BUS_PORT_NODE));

BUILD_ASSERT(DT_PROP_LEN(MIPI_NODE, data_gpios) == 8);
LISTIFY(8, CHECK_DATA_PIN, ())
BUILD_ASSERT(BUS_DATA_SHIFT + 8 <= 16);
BUILD_ASSERT(DT_SAME_NODE(DT_GPIO_CTLR(MIPI_NODE, cs_gpios), BUS_PORT_NODE));
BUILD_ASSERT(DT_SAME_NODE(DT_GPIO_CTLR(MIPI_NODE, dc_gpios), BUS_PORT_NODE));
BUILD_ASSERT((DT_GPIO_FLAGS(MIPI_NODE, wr_gpios) & GPIO_ACTIVE_LOW) == 0);
BUILD_ASSERT((DT_GPIO_FLAGS(MIPI_NODE, dc_gpios) & GPIO_ACTIVE_LOW) == 0);
BUILD_ASSERT((DT_GPIO_FLAGS(MIPI_NODE, cs_gpios) & GPIO_ACTIVE_LOW) != 0);

/* ST7789V commands */
#define CMD_CASET 0x2A
#define CMD_RASET 0x2B
#define CMD_RAMWR 0x2C

static const struct device *const displayDev = DEVICE_DT_GET(DISPLAY_NODE);
static const struct device *const mipiDev = DEVICE_DT_GET(MIPI_NODE);
static const struct mipi_dbi_config dbiConfig = {
    .mode = MIPI_DBI_MODE_8080_BUS_8_BIT,
};

static uint8_t srcColumn[OUT_WIDTH];
static bool displayReady;
static uint8_t backlightLevel;
static K_MUTEX_DEFINE(displayMutex);

static inline uint16_t srcCoord(uint16_t outCoord)
{
    return (outCoord * SCALE_DEN) / SCALE_NUM;
}

/*
 * Write to the port set/clear register and wait for the write to reach the
 * pins: GPIO writes are posted through the bus bridge, a read from the same
 * port completes only after them. This makes the delays between the edges
 * deterministic.
 */
static ALWAYS_INLINE void busPortWrite(uint32_t value)
{
    sys_write32(value, BUS_PORT_BASE + GPIO_SCR);
    (void)sys_read32(BUS_PORT_BASE + GPIO_ODT);
}

static ALWAYS_INLINE void busWrite(uint8_t value)
{
    /* Data on the bus, WR still high */
    busPortWrite(((uint32_t)value << BUS_DATA_SHIFT)
                 | ((uint32_t)(uint8_t)~value << (BUS_DATA_SHIFT + 16)));

    /* WR low: data setup time is at least 10ns */
    busPortWrite(PIN_CLR(BUS_WR_PIN));

    /* WR low pulse width, at least 15ns */
    arch_nop();
    arch_nop();
    arch_nop();
    arch_nop();
    arch_nop();
    arch_nop();
    arch_nop();
    arch_nop();

    /* WR rising edge latches the data */
    busPortWrite(PIN_SET(BUS_WR_PIN));

    /* WR high time and data hold time, at least 15ns */
    arch_nop();
    arch_nop();
    arch_nop();
    arch_nop();
}

static ALWAYS_INLINE void busWritePixel(uint16_t pixel)
{
    /* RGB565, MSB first */
    busWrite(pixel >> 8);
    busWrite(pixel & 0xFF);
}

static void busStartData()
{
    busPortWrite(PIN_SET(BUS_DC_PIN) | PIN_SET(BUS_WR_PIN));
    busPortWrite(PIN_CLR(BUS_CS_PIN));
}

static void busEndData()
{
    busPortWrite(PIN_SET(BUS_CS_PIN));
}

/*
 * Switch the bus pins to the fastest output mode (0b11, 50MHz, push-pull), as
 * done by the vendor firmware: with the mode set by the Zephyr GPIO driver
 * (0b01) the edges are too slow for strobes of about 100ns.
 */
static void busSetFastOutputs()
{
    uint32_t pins = BIT(BUS_WR_PIN) | BIT(BUS_CS_PIN) | BIT(BUS_DC_PIN)
                  | (0xFFUL << BUS_DATA_SHIFT);
    uint32_t key = irq_lock();

    for (uint8_t pin = 0; pin < 16; pin++) {
        if ((pins & BIT(pin)) == 0)
            continue;

        mem_addr_t reg = BUS_PORT_BASE + ((pin < 8) ? GPIO_CFGLR : GPIO_CFGHR);
        uint32_t shift = (pin % 8) * 4;
        uint32_t val = sys_read32(reg);

        val = (val & ~(0xFUL << shift)) | (0x3UL << shift);
        sys_write32(val, reg);
    }

    irq_unlock(key);
}

/*
 * Set the panel memory window and start a memory write: the pixel data has
 * to be sent right after, between busStartData() and busEndData().
 */
static void setWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    uint16_t xEnd = x + w - 1;
    uint16_t yEnd = y + h - 1;
    uint8_t col[] = { x >> 8, x & 0xFF, xEnd >> 8, xEnd & 0xFF };
    uint8_t row[] = { y >> 8, y & 0xFF, yEnd >> 8, yEnd & 0xFF };

    mipi_dbi_command_write(mipiDev, &dbiConfig, CMD_CASET, col, sizeof(col));
    mipi_dbi_command_write(mipiDev, &dbiConfig, CMD_RASET, row, sizeof(row));
    mipi_dbi_command_write(mipiDev, &dbiConfig, CMD_RAMWR, NULL, 0);
}

/*
 * Set the backlight brightness, 0 to 100. The LCD backlight PWM duty cycle
 * follows a quadratic curve, for a finer control at low brightness. The keypad
 * backlight has no dimming and follows the LCD one.
 */
static void backlight(uint8_t level)
{
    if (level > 100)
        level = 100;

    if (level == backlightLevel)
        return;

    backlightLevel = level;

    if (level == 0) {
        gpioPin_clear(&rt950_kbd_bl);
        pwm_set_pulse_dt(&rt950_lcd_bl_pwm, 0);
        return;
    }

    uint32_t duty = MIN(((uint32_t)level * level) / 105 + 5, 100);
    uint32_t pulse = (rt950_lcd_bl_pwm.period * duty) / 100;

    pwm_set_pulse_dt(&rt950_lcd_bl_pwm, pulse);
    gpioPin_set(&rt950_kbd_bl);
}

static void clearPanel()
{
    setWindow(0, 0, PANEL_WIDTH, PANEL_HEIGHT);
    busStartData();

    for (uint32_t i = 0; i < (PANEL_WIDTH * PANEL_HEIGHT); i++)
        busWritePixel(0x0000);

    busEndData();
}

static void writeRows(uint8_t startRow, uint8_t endRow, const uint16_t *fb)
{
    /* Range of output rows whose source row is within startRow and endRow */
    uint16_t outStart = ((startRow * SCALE_NUM) + SCALE_DEN - 1) / SCALE_DEN;
    uint16_t outEnd = (((endRow + 1) * SCALE_NUM) + SCALE_DEN - 1) / SCALE_DEN;
    outEnd = MIN(outEnd, OUT_HEIGHT);

    if (outStart >= outEnd)
        return;

    setWindow(OUT_X_OFFSET, OUT_Y_OFFSET + outStart, OUT_WIDTH,
              outEnd - outStart);
    busStartData();

    for (uint16_t row = outStart; row < outEnd; row++) {
        const uint16_t *src = &fb[srcCoord(row) * CONFIG_SCREEN_WIDTH];

        for (uint16_t x = 0; x < OUT_WIDTH; x++)
            busWritePixel(src[srcColumn[x]]);
    }

    busEndData();
}

void display_init()
{
    /* Keep the backlights off until the first frame has been drawn */
    gpioPin_clear(&rt950_kbd_bl);
    if (gpioPin_setMode(&rt950_kbd_bl, OUTPUT) < 0)
        LOG_ERR("Keypad backlight GPIO configuration failed");

    if (pwm_is_ready_dt(&rt950_lcd_bl_pwm)) {
        pwm_set_pulse_dt(&rt950_lcd_bl_pwm, 0);

        /*
         * TMR8 is an advanced timer, its outputs are driven only when the
         * main output enable bit (OEN, bit 15 of the BRK register) is set.
         * The AT32 PWM driver does it only for the timers listed by the HAL
         * IS_TMR_BREAK() macro, which for the AT32F403A misses TMR8.
         */
        sys_set_bit(LCD_BL_TIMER_BASE + TMR_BRK_OFFSET, TMR_BRK_OEN_BIT);
    } else {
        LOG_ERR("Backlight PWM not ready");
    }

    for (uint16_t x = 0; x < OUT_WIDTH; x++)
        srcColumn[x] = srcCoord(x);

    if (!device_is_ready(displayDev)) {
        LOG_ERR("Display device not ready");
        return;
    }

    busSetFastOutputs();

    /* The panel memory content is undefined at power on */
    clearPanel();
    display_blanking_off(displayDev);
    displayReady = true;

    LOG_INF("Display ready, %dx%d frame scaled to %dx%d at (%d,%d)",
            CONFIG_SCREEN_WIDTH, CONFIG_SCREEN_HEIGHT, OUT_WIDTH, OUT_HEIGHT,
            OUT_X_OFFSET, OUT_Y_OFFSET);
}

void display_terminate()
{
    k_mutex_lock(&displayMutex, K_FOREVER);

    backlight(0);

    if (displayReady) {
        clearPanel();
        display_blanking_on(displayDev);
        displayReady = false;
    }

    k_mutex_unlock(&displayMutex);
}

void display_renderRows(uint8_t startRow, uint8_t endRow, void *fb)
{
    if ((startRow > endRow) || (endRow >= CONFIG_SCREEN_HEIGHT))
        return;

    k_mutex_lock(&displayMutex, K_FOREVER);

    if (displayReady)
        writeRows(startRow, endRow, (const uint16_t *)fb);

    k_mutex_unlock(&displayMutex);
}

void display_render(void *fb)
{
    display_renderRows(0, CONFIG_SCREEN_HEIGHT - 1, fb);
}

void display_setContrast(uint8_t contrast)
{
    (void)contrast;
}

void display_setBacklightLevel(uint8_t level)
{
    k_mutex_lock(&displayMutex, K_FOREVER);

    if (displayReady)
        backlight(level);

    k_mutex_unlock(&displayMutex);
}
