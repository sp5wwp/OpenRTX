/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "peripherals/gpio.h"
#include "interfaces/display.h"
#include "interfaces/delays.h"
#include "drivers/backlight/backlight.h"
#include "hwconfig.h"
#include <stddef.h>

/*
 * ST7735S driver for the AnyTone D890UV, 8080 8-bit bus driven by GPIOs.
 *
 * The panel frame memory is 128 columns x 160 rows (MADCTL as set by the stock
 * firmware), while the screen is mounted as a 160x128 landscape display: the
 * user sees the frame memory transposed. Rendering therefore walks the
 * framebuffer by columns, so no MADCTL rotation has to be guessed.
 */

enum st7735sCmd {
    ST7735S_CMD_SLPOUT = 0x11,
    ST7735S_CMD_DISPON = 0x29,
    ST7735S_CMD_CASET  = 0x2A,
    ST7735S_CMD_RASET  = 0x2B,
    ST7735S_CMD_RAMWR  = 0x2C,
    ST7735S_CMD_MADCTL = 0x36,
    ST7735S_CMD_COLMOD = 0x3A,
    ST7735S_CMD_FRMCTR1 = 0xB1,
    ST7735S_CMD_FRMCTR2 = 0xB2,
    ST7735S_CMD_FRMCTR3 = 0xB3,
    ST7735S_CMD_INVCTR = 0xB4,
    ST7735S_CMD_PWCTR1 = 0xC0,
    ST7735S_CMD_PWCTR2 = 0xC1,
    ST7735S_CMD_PWCTR3 = 0xC2,
    ST7735S_CMD_PWCTR4 = 0xC3,
    ST7735S_CMD_PWCTR5 = 0xC4,
    ST7735S_CMD_VMCTR1 = 0xC5,
    ST7735S_CMD_GMCTRP1 = 0xE0,
    ST7735S_CMD_GMCTRN1 = 0xE1,
};

/* Frame memory size, in panel coordinates */
#define MEM_COLUMNS CONFIG_SCREEN_HEIGHT
#define MEM_ROWS    CONFIG_SCREEN_WIDTH

static inline GPIO_TypeDef *portOf(const void *port, const uint8_t pin)
{
    (void)pin;
    return (GPIO_TypeDef *)port;
}

static inline uint8_t pinOf(const void *port, const uint8_t pin)
{
    (void)port;
    return pin;
}

/*
 * Put a byte on D0-D7 and strobe WR. D0-D7 are consecutive pins of one port,
 * so all eight lines change with a single BSRR write.
 */
static inline void writeByte(const uint8_t val)
{
    const uint8_t shift = pinOf(LCD_D0);
    const uint32_t set = (uint32_t)val << shift;
    const uint32_t clr = (uint32_t)(uint8_t)~val << (shift + 16);

    /*
     * ST7735S write cycle: WR low and high at least 15ns each, whole cycle at
     * least 66ns. Four NOPs per phase keep that at 160MHz.
     */
    portOf(LCD_D0)->BSRR = set | clr;
    gpio_clearPin(LCD_WR);
    __NOP();
    __NOP();
    __NOP();
    __NOP();
    gpio_setPin(LCD_WR);
    __NOP();
    __NOP();
    __NOP();
    __NOP();
}

static void sendCmd(const uint8_t cmd)
{
    gpio_clearPin(LCD_DC);
    writeByte(cmd);
    gpio_setPin(LCD_DC);
}

static void sendCmdData(const uint8_t cmd, const uint8_t *data, size_t len)
{
    sendCmd(cmd);
    for (size_t i = 0; i < len; i++)
        writeByte(data[i]);
}

#define CMD(c, ...)                                                   \
    do {                                                              \
        static const uint8_t _d[] = { __VA_ARGS__ };                  \
        sendCmdData(c, _d, sizeof(_d));                               \
    } while (0)

static void setWindow(uint8_t col0, uint8_t col1, uint8_t row0, uint8_t row1)
{
    const uint8_t caset[] = { 0x00, col0, 0x00, col1 };
    const uint8_t raset[] = { 0x00, row0, 0x00, row1 };

    sendCmdData(ST7735S_CMD_CASET, caset, sizeof(caset));
    sendCmdData(ST7735S_CMD_RASET, raset, sizeof(raset));
    sendCmd(ST7735S_CMD_RAMWR);
}

static void clearMemory()
{
    setWindow(0, MEM_COLUMNS - 1, 0, MEM_ROWS - 1);
    for (size_t i = 0; i < (MEM_COLUMNS * MEM_ROWS * 2); i++)
        writeByte(0x00);
}

void display_init()
{
    backlight_init();

    gpio_setMode(LCD_D0, OUTPUT);
    gpio_setMode(LCD_D1, OUTPUT);
    gpio_setMode(LCD_D2, OUTPUT);
    gpio_setMode(LCD_D3, OUTPUT);
    gpio_setMode(LCD_D4, OUTPUT);
    gpio_setMode(LCD_D5, OUTPUT);
    gpio_setMode(LCD_D6, OUTPUT);
    gpio_setMode(LCD_D7, OUTPUT);

    gpio_setPin(LCD_CS);    /* CS idle state is high level */
    gpio_setPin(LCD_DC);
    gpio_setPin(LCD_WR);
    gpio_setPin(LCD_RST);
    gpio_setMode(LCD_CS, OUTPUT);
    gpio_setMode(LCD_DC, OUTPUT);
    gpio_setMode(LCD_WR, OUTPUT);
    gpio_setMode(LCD_RST, OUTPUT);

    /*
     * Reset pulse and initialisation sequence taken from the LCD init routine
     * of the stock AnyTone D890UV firmware V1.05 (D890UV_V1.05_20260521.CDD,
     * function at 0x0805D830), recovered by emulating it.
     */
    delayMs(100);
    gpio_clearPin(LCD_RST);
    delayMs(100);
    gpio_setPin(LCD_RST);
    delayMs(500);

    gpio_clearPin(LCD_CS);

    sendCmd(ST7735S_CMD_SLPOUT);
    delayMs(120);

    CMD(ST7735S_CMD_FRMCTR1, 0x01, 0x01, 0x01);
    CMD(ST7735S_CMD_FRMCTR2, 0x01, 0x01, 0x01);
    CMD(ST7735S_CMD_FRMCTR3, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01);
    CMD(ST7735S_CMD_INVCTR, 0x03);
    CMD(ST7735S_CMD_PWCTR1, 0x28, 0x08, 0x84);
    CMD(ST7735S_CMD_PWCTR2, 0xC0);
    CMD(ST7735S_CMD_PWCTR3, 0x0C, 0x00);
    CMD(ST7735S_CMD_PWCTR4, 0x8C, 0x2A);
    CMD(ST7735S_CMD_PWCTR5, 0x8A, 0xEE);
    CMD(ST7735S_CMD_VMCTR1, 0x0C);
    CMD(ST7735S_CMD_MADCTL, 0x88);   /* MY | BGR */
    CMD(ST7735S_CMD_GMCTRP1, 0x05, 0x1A, 0x0C, 0x0E, 0x3A, 0x34, 0x2D, 0x2F,
        0x2D, 0x2A, 0x2F, 0x3C, 0x00, 0x01, 0x02, 0x10);
    CMD(ST7735S_CMD_GMCTRN1, 0x04, 0x1B, 0x0D, 0x0E, 0x2D, 0x29, 0x24, 0x29,
        0x28, 0x26, 0x31, 0x3B, 0x00, 0x00, 0x03, 0x12);
    CMD(ST7735S_CMD_COLMOD, 0x55);   /* 16bpp, RGB565 */

    clearMemory();
    sendCmd(ST7735S_CMD_DISPON);

    gpio_setPin(LCD_CS);
}

void display_terminate()
{
    backlight_terminate();
}

void display_renderRows(uint8_t startRow, uint8_t endRow, void *fb)
{
    const uint16_t *pix = (const uint16_t *)fb;

    if (endRow <= startRow)
        return;

    /* Screen rows are frame memory columns, screen columns are memory rows */
    gpio_clearPin(LCD_CS);
    setWindow(startRow, endRow - 1, 0, MEM_ROWS - 1);

    for (uint16_t x = 0; x < CONFIG_SCREEN_WIDTH; x++) {
        for (uint16_t y = startRow; y < endRow; y++) {
            uint16_t pixel = pix[x + (y * CONFIG_SCREEN_WIDTH)];
            writeByte(pixel >> 8);
            writeByte(pixel & 0xFF);
        }
    }

    gpio_setPin(LCD_CS);
}

void display_render(void *fb)
{
    display_renderRows(0, CONFIG_SCREEN_HEIGHT, fb);
}

void display_setContrast(uint8_t contrast)
{
    /* This controller does not support contrast regulation */
    (void)contrast;
}
