# SPD2010 LCD Initialization: Comprehensive Research Document

## Executive Summary

The SPD2010 is a round AMOLED display controller with integrated GRAM, manufactured by Solomon Systech. It is the display controller inside the SenseCAP Watcher's 412x412 circular touch display. The controller communicates exclusively over Quad-SPI (QSPI) in the Watcher's factory firmware, using SPI Mode 3, a 40 MHz pixel clock, 32-bit command words, and 8-bit parameter words.

Initialization requires sending a 291-entry vendor-specific command table to the chip. This table is built into Espressif's official `esp_lcd_spd2010` component and is sent automatically by `esp_lcd_panel_init()`. The sleep-out command (0x11) is the last entry in that table (with a mandatory 120 ms delay). The display-on command (0x29) is sent separately by calling `esp_lcd_panel_disp_on_off(panel, true)` after `esp_lcd_panel_init()` completes.

---

## Table of Contents

1. [Hardware Overview](#1-hardware-overview)
2. [SPI Interface Configuration](#2-spi-interface-configuration)
3. [Factory Firmware GPIO Pin Assignments](#3-factory-firmware-gpio-pin-assignments)
4. [Initialization Call Sequence](#4-initialization-call-sequence)
5. [The Complete Vendor Init Command Table](#5-the-complete-vendor-init-command-table)
6. [Sleep-Out (0x11) and Display-On (0x29) Handling](#6-sleep-out-0x11-and-display-on-0x29-handling)
7. [QSPI Wire Encoding](#7-qspi-wire-encoding)
8. [The panel_spd2010_init() and panel_spd2010_reset() Functions](#8-the-panel_spd2010_init-and-panel_spd2010_reset-functions)
9. [Arduino / Non-IDF Usage](#9-arduino--non-idf-usage)
10. [Common Pitfalls](#10-common-pitfalls)
11. [Sources](#11-sources)

---

## 1. Hardware Overview

| Property | Value |
|---|---|
| Controller | Solomon Systech SPD2010 |
| Display type | Round AMOLED with integrated GRAM |
| Resolution | 412 x 412 pixels |
| Color depth | 16-bit RGB565 (0x55 COLMOD value) |
| Interface | Quad-SPI (QSPI) |
| Touch | Capacitive touch is built into the same IC; uses I2C for touch readout |

The SPD2010 is unusual because its driving method is similar to SPI/I80 LCDs despite being an AMOLED. It has an internal frame buffer (GRAM), so you write pixel data once and the panel refreshes itself - no continuous DMA stream required. The driver chip and touch controller share the same physical package and reset line in the Watcher hardware.

---

## 2. SPI Interface Configuration

These values come directly from the Seeed factory firmware BSP header (`sensecap-watcher.h`) and the `bsp_lcd_pannel_init()` function in `sensecap-watcher.c`.

| Parameter | Value | Notes |
|---|---|---|
| SPI host | SPI3_HOST | Also called HSPI on ESP32-S3 |
| SPI mode | 3 | CPOL=1, CPHA=1 |
| Pixel clock | 40 MHz | `DRV_LCD_PIXEL_CLK_HZ = 40 * 1000 * 1000` |
| Command bit width | 32 bits | `DRV_LCD_CMD_BITS = 32` |
| Parameter bit width | 8 bits | `DRV_LCD_PARAM_BITS = 8` |
| Quad mode | enabled | `flags.quad_mode = true` |
| DC GPIO | -1 (not used) | QSPI encodes command vs data in the opcode byte |
| Max transfer size | H_RES * V_RES * 2 bytes / DMA_DIV | Calculated at runtime |

The Espressif component registry README states defaults of 20 MHz for QSPI. The factory firmware overrides this to 40 MHz. Both are valid; 80 MHz SPI-only mode is the maximum documented value.

---

## 3. Factory Firmware GPIO Pin Assignments

From `sensecap-watcher.h` in the Seeed SenseCAP-Watcher-Firmware repository:

```c
// QSPI bus
#define BSP_SPI3_HOST_PCLK   GPIO_NUM_7
#define BSP_SPI3_HOST_DATA0  GPIO_NUM_9
#define BSP_SPI3_HOST_DATA1  GPIO_NUM_1
#define BSP_SPI3_HOST_DATA2  GPIO_NUM_14
#define BSP_SPI3_HOST_DATA3  GPIO_NUM_13

// LCD control
#define BSP_LCD_SPI_NUM      SPI3_HOST
#define BSP_LCD_SPI_CS       GPIO_NUM_45
#define BSP_LCD_GPIO_RST     GPIO_NUM_NC   // no hardware reset GPIO; uses software reset
#define BSP_LCD_GPIO_DC      GPIO_NUM_1    // overlaps DATA1 - only used in SPI (non-QSPI) mode
#define BSP_LCD_GPIO_BL      GPIO_NUM_8    // backlight via LEDC PWM

// Display dimensions
#define DRV_LCD_H_RES        412
#define DRV_LCD_V_RES        412
#define DRV_LCD_BITS_PER_PIXEL 16

// Backlight
#define DRV_LCD_BL_ON_LEVEL  1
#define DRV_LCD_LEDC_DUTY_RES LEDC_TIMER_10_BIT
#define DRV_LCD_LEDC_CH      1
```

Note: `BSP_LCD_GPIO_RST` is `GPIO_NUM_NC` (not connected), meaning the Watcher hardware has no dedicated reset pin wired to the ESP32-S3 for the display. The driver falls back to a software reset (command 0x01, `LCD_CMD_SWRESET`) when no reset GPIO is configured.

---

## 4. Initialization Call Sequence

This is the exact sequence used in `bsp_lcd_pannel_init()` from the factory firmware:

```c
// 1. Initialize the QSPI bus
spi_bus_config_t qspi_cfg = {
    .sclk_io_num  = BSP_SPI3_HOST_PCLK,   // GPIO 7
    .data0_io_num = BSP_SPI3_HOST_DATA0,   // GPIO 9
    .data1_io_num = BSP_SPI3_HOST_DATA1,   // GPIO 1
    .data2_io_num = BSP_SPI3_HOST_DATA2,   // GPIO 14
    .data3_io_num = BSP_SPI3_HOST_DATA3,   // GPIO 13
    .max_transfer_sz = 412 * 412 * 2,
};
spi_bus_initialize(SPI3_HOST, &qspi_cfg, SPI_DMA_CH_AUTO);

// 2. Create panel IO handle
esp_lcd_panel_io_spi_config_t io_config = {
    .cs_gpio_num      = BSP_LCD_SPI_CS,     // GPIO 45
    .dc_gpio_num      = -1,                 // not used in QSPI mode
    .spi_mode         = 3,                  // CPOL=1, CPHA=1
    .pclk_hz          = 40 * 1000 * 1000,  // 40 MHz
    .trans_queue_depth = 10,
    .lcd_cmd_bits     = 32,
    .lcd_param_bits   = 8,
    .flags.quad_mode  = true,
};
esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI3_HOST, &io_config, &io_handle);

// 3. Configure SPD2010 vendor settings
spd2010_vendor_config_t vendor_config = {
    .flags.use_qspi_interface = 1,
};

// 4. Create the panel driver
esp_lcd_panel_dev_config_t panel_config = {
    .reset_gpio_num = GPIO_NUM_NC,          // no hardware reset pin
    .rgb_ele_order  = LCD_RGB_ELEMENT_ORDER_RGB,
    .bits_per_pixel = 16,
    .vendor_config  = &vendor_config,
};
esp_lcd_new_panel_spd2010(io_handle, &panel_config, &panel_handle);

// 5. Reset (software reset since no reset GPIO)
esp_lcd_panel_reset(panel_handle);

// 6. Send the full 291-command vendor init table + sleep-out (0x11)
esp_lcd_panel_init(panel_handle);

// 7. Optional: apply mirroring
esp_lcd_panel_mirror(panel_handle, mirror_x, mirror_y);

// 8. Send display-on command (0x29)
esp_lcd_panel_disp_on_off(panel_handle, true);

// 9. Set backlight brightness via LEDC PWM on GPIO 8
bsp_lcd_brightness_set(default_brightness);
```

---

## 5. The Complete Vendor Init Command Table

This is the `vendor_specific_init_default[]` array from `esp_lcd_spd2010.c` in the Espressif esp-iot-solution repository. It contains 291 entries. The format is `{cmd, {data bytes}, data_bytes_count, delay_ms}`.

The command byte 0xFF with 3-byte payload selects which internal register bank subsequent commands target. The three payload bytes are always `{0x20, 0x10, PAGE}` where PAGE selects the register page.

```c
static const spd2010_lcd_init_cmd_t vendor_specific_init_default[] = {
    // --- Register page 0x10: Power / VCOM settings ---
    {0xFF, (uint8_t []){0x20, 0x10, 0x10}, 3, 0},
    {0x0C, (uint8_t []){0x11}, 1, 0},
    {0x10, (uint8_t []){0x02}, 1, 0},
    {0x11, (uint8_t []){0x11}, 1, 0},
    {0x15, (uint8_t []){0x42}, 1, 0},
    {0x16, (uint8_t []){0x11}, 1, 0},
    {0x1A, (uint8_t []){0x02}, 1, 0},
    {0x1B, (uint8_t []){0x11}, 1, 0},
    {0x61, (uint8_t []){0x80}, 1, 0},
    {0x62, (uint8_t []){0x80}, 1, 0},
    {0x54, (uint8_t []){0x44}, 1, 0},
    {0x58, (uint8_t []){0x88}, 1, 0},
    {0x5C, (uint8_t []){0xcc}, 1, 0},

    // --- Register page 0x10: Gate output mapping (first pass) ---
    {0xFF, (uint8_t []){0x20, 0x10, 0x10}, 3, 0},
    {0x20, (uint8_t []){0x80}, 1, 0},
    {0x21, (uint8_t []){0x81}, 1, 0},
    {0x22, (uint8_t []){0x31}, 1, 0},
    {0x23, (uint8_t []){0x20}, 1, 0},
    {0x24, (uint8_t []){0x11}, 1, 0},
    {0x25, (uint8_t []){0x11}, 1, 0},
    {0x26, (uint8_t []){0x12}, 1, 0},
    {0x27, (uint8_t []){0x12}, 1, 0},
    {0x30, (uint8_t []){0x80}, 1, 0},
    {0x31, (uint8_t []){0x81}, 1, 0},
    {0x32, (uint8_t []){0x31}, 1, 0},
    {0x33, (uint8_t []){0x20}, 1, 0},
    {0x34, (uint8_t []){0x11}, 1, 0},
    {0x35, (uint8_t []){0x11}, 1, 0},
    {0x36, (uint8_t []){0x12}, 1, 0},
    {0x37, (uint8_t []){0x12}, 1, 0},

    // --- Register page 0x10: Source control ---
    {0xFF, (uint8_t []){0x20, 0x10, 0x10}, 3, 0},
    {0x41, (uint8_t []){0x11}, 1, 0},
    {0x42, (uint8_t []){0x22}, 1, 0},
    {0x43, (uint8_t []){0x33}, 1, 0},
    {0x49, (uint8_t []){0x11}, 1, 0},
    {0x4A, (uint8_t []){0x22}, 1, 0},
    {0x4B, (uint8_t []){0x33}, 1, 0},

    // --- Register page 0x15: GIP forward scan mapping ---
    {0xFF, (uint8_t []){0x20, 0x10, 0x15}, 3, 0},
    {0x00, (uint8_t []){0x00}, 1, 0},
    {0x01, (uint8_t []){0x00}, 1, 0},
    {0x02, (uint8_t []){0x00}, 1, 0},
    {0x03, (uint8_t []){0x00}, 1, 0},
    {0x04, (uint8_t []){0x10}, 1, 0},
    {0x05, (uint8_t []){0x0C}, 1, 0},
    {0x06, (uint8_t []){0x23}, 1, 0},
    {0x07, (uint8_t []){0x22}, 1, 0},
    {0x08, (uint8_t []){0x21}, 1, 0},
    {0x09, (uint8_t []){0x20}, 1, 0},
    {0x0A, (uint8_t []){0x33}, 1, 0},
    {0x0B, (uint8_t []){0x32}, 1, 0},
    {0x0C, (uint8_t []){0x34}, 1, 0},
    {0x0D, (uint8_t []){0x35}, 1, 0},
    {0x0E, (uint8_t []){0x01}, 1, 0},
    {0x0F, (uint8_t []){0x01}, 1, 0},
    {0x20, (uint8_t []){0x00}, 1, 0},
    {0x21, (uint8_t []){0x00}, 1, 0},
    {0x22, (uint8_t []){0x00}, 1, 0},
    {0x23, (uint8_t []){0x00}, 1, 0},
    {0x24, (uint8_t []){0x0C}, 1, 0},
    {0x25, (uint8_t []){0x10}, 1, 0},
    {0x26, (uint8_t []){0x20}, 1, 0},
    {0x27, (uint8_t []){0x21}, 1, 0},
    {0x28, (uint8_t []){0x22}, 1, 0},
    {0x29, (uint8_t []){0x23}, 1, 0},
    {0x2A, (uint8_t []){0x33}, 1, 0},
    {0x2B, (uint8_t []){0x32}, 1, 0},
    {0x2C, (uint8_t []){0x34}, 1, 0},
    {0x2D, (uint8_t []){0x35}, 1, 0},
    {0x2E, (uint8_t []){0x01}, 1, 0},
    {0x2F, (uint8_t []){0x01}, 1, 0},

    // --- Register page 0x16: GIP backward scan mapping ---
    {0xFF, (uint8_t []){0x20, 0x10, 0x16}, 3, 0},
    {0x00, (uint8_t []){0x00}, 1, 0},
    {0x01, (uint8_t []){0x00}, 1, 0},
    {0x02, (uint8_t []){0x00}, 1, 0},
    {0x03, (uint8_t []){0x00}, 1, 0},
    {0x04, (uint8_t []){0x08}, 1, 0},
    {0x05, (uint8_t []){0x04}, 1, 0},
    {0x06, (uint8_t []){0x19}, 1, 0},
    {0x07, (uint8_t []){0x18}, 1, 0},
    {0x08, (uint8_t []){0x17}, 1, 0},
    {0x09, (uint8_t []){0x16}, 1, 0},
    {0x0A, (uint8_t []){0x33}, 1, 0},
    {0x0B, (uint8_t []){0x32}, 1, 0},
    {0x0C, (uint8_t []){0x34}, 1, 0},
    {0x0D, (uint8_t []){0x35}, 1, 0},
    {0x0E, (uint8_t []){0x01}, 1, 0},
    {0x0F, (uint8_t []){0x01}, 1, 0},
    {0x20, (uint8_t []){0x00}, 1, 0},
    {0x21, (uint8_t []){0x00}, 1, 0},
    {0x22, (uint8_t []){0x00}, 1, 0},
    {0x23, (uint8_t []){0x00}, 1, 0},
    {0x24, (uint8_t []){0x04}, 1, 0},
    {0x25, (uint8_t []){0x08}, 1, 0},
    {0x26, (uint8_t []){0x16}, 1, 0},
    {0x27, (uint8_t []){0x17}, 1, 0},
    {0x28, (uint8_t []){0x18}, 1, 0},
    {0x29, (uint8_t []){0x19}, 1, 0},
    {0x2A, (uint8_t []){0x33}, 1, 0},
    {0x2B, (uint8_t []){0x32}, 1, 0},
    {0x2C, (uint8_t []){0x34}, 1, 0},
    {0x2D, (uint8_t []){0x35}, 1, 0},
    {0x2E, (uint8_t []){0x01}, 1, 0},
    {0x2F, (uint8_t []){0x01}, 1, 0},

    // --- Register page 0x12: Timing/bias control ---
    {0xFF, (uint8_t []){0x20, 0x10, 0x12}, 3, 0},
    {0x00, (uint8_t []){0x99}, 1, 0},
    {0x2A, (uint8_t []){0x28}, 1, 0},
    {0x2B, (uint8_t []){0x0f}, 1, 0},
    {0x2C, (uint8_t []){0x16}, 1, 0},
    {0x2D, (uint8_t []){0x28}, 1, 0},
    {0x2E, (uint8_t []){0x0f}, 1, 0},

    // --- Register page 0xA0 ---
    {0xFF, (uint8_t []){0x20, 0x10, 0xA0}, 3, 0},
    {0x08, (uint8_t []){0xdc}, 1, 0},

    // --- Register page 0x45: VGLO control ---
    {0xFF, (uint8_t []){0x20, 0x10, 0x45}, 3, 0},
    {0x01, (uint8_t []){0x9C}, 1, 0},
    {0x03, (uint8_t []){0x9C}, 1, 0},

    // --- Register page 0x42 ---
    {0xFF, (uint8_t []){0x20, 0x10, 0x42}, 3, 0},
    {0x05, (uint8_t []){0x2c}, 1, 0},

    // --- Register page 0x11: Panel timing ---
    {0xFF, (uint8_t []){0x20, 0x10, 0x11}, 3, 0},
    {0x50, (uint8_t []){0x01}, 1, 0},

    // --- Register page 0x00: Standard MIPI/user commands ---
    {0xFF, (uint8_t []){0x20, 0x10, 0x00}, 3, 0},
    {0x2A, (uint8_t []){0x00, 0x00, 0x01, 0x9B}, 4, 0},  // CASET: 0..411
    {0x2B, (uint8_t []){0x00, 0x00, 0x01, 0x9B}, 4, 0},  // RASET: 0..411

    // --- Register page 0x40 ---
    {0xFF, (uint8_t []){0x20, 0x10, 0x40}, 3, 0},
    {0x86, (uint8_t []){0x00}, 1, 0},

    // --- Back to page 0x00, then switch to 0x12 ---
    {0xFF, (uint8_t []){0x20, 0x10, 0x00}, 3, 0},
    {0xFF, (uint8_t []){0x20, 0x10, 0x12}, 3, 0},
    {0x0D, (uint8_t []){0x66}, 1, 0},

    // --- Register page 0x17: ELVSS/VGSP control ---
    {0xFF, (uint8_t []){0x20, 0x10, 0x17}, 3, 0},
    {0x39, (uint8_t []){0x3c}, 1, 0},

    // --- Register page 0x31: Gamma curve (positive, red/blue 16-step pairs) ---
    {0xff, (uint8_t []){0x20, 0x10, 0x31}, 3, 0},
    {0x38, (uint8_t []){0x03}, 1, 0},
    {0x39, (uint8_t []){0xf0}, 1, 0},
    {0x36, (uint8_t []){0x03}, 1, 0},
    {0x37, (uint8_t []){0xe8}, 1, 0},
    {0x34, (uint8_t []){0x03}, 1, 0},
    {0x35, (uint8_t []){0xCF}, 1, 0},
    {0x32, (uint8_t []){0x03}, 1, 0},
    {0x33, (uint8_t []){0xBA}, 1, 0},
    {0x30, (uint8_t []){0x03}, 1, 0},
    {0x31, (uint8_t []){0xA2}, 1, 0},
    {0x2e, (uint8_t []){0x03}, 1, 0},
    {0x2f, (uint8_t []){0x95}, 1, 0},
    {0x2c, (uint8_t []){0x03}, 1, 0},
    {0x2d, (uint8_t []){0x7e}, 1, 0},
    {0x2a, (uint8_t []){0x03}, 1, 0},
    {0x2b, (uint8_t []){0x62}, 1, 0},
    {0x28, (uint8_t []){0x03}, 1, 0},
    {0x29, (uint8_t []){0x44}, 1, 0},
    {0x26, (uint8_t []){0x02}, 1, 0},
    {0x27, (uint8_t []){0xfc}, 1, 0},
    {0x24, (uint8_t []){0x02}, 1, 0},
    {0x25, (uint8_t []){0xd0}, 1, 0},
    {0x22, (uint8_t []){0x02}, 1, 0},
    {0x23, (uint8_t []){0x98}, 1, 0},
    {0x20, (uint8_t []){0x02}, 1, 0},
    {0x21, (uint8_t []){0x6f}, 1, 0},
    {0x1e, (uint8_t []){0x02}, 1, 0},
    {0x1f, (uint8_t []){0x32}, 1, 0},
    {0x1c, (uint8_t []){0x01}, 1, 0},
    {0x1d, (uint8_t []){0xf6}, 1, 0},
    {0x1a, (uint8_t []){0x01}, 1, 0},
    {0x1b, (uint8_t []){0xb8}, 1, 0},
    {0x18, (uint8_t []){0x01}, 1, 0},
    {0x19, (uint8_t []){0x6E}, 1, 0},
    {0x16, (uint8_t []){0x01}, 1, 0},
    {0x17, (uint8_t []){0x41}, 1, 0},
    {0x14, (uint8_t []){0x00}, 1, 0},
    {0x15, (uint8_t []){0xfd}, 1, 0},
    {0x12, (uint8_t []){0x00}, 1, 0},
    {0x13, (uint8_t []){0xCf}, 1, 0},
    {0x10, (uint8_t []){0x00}, 1, 0},
    {0x11, (uint8_t []){0x98}, 1, 0},
    {0x0e, (uint8_t []){0x00}, 1, 0},
    {0x0f, (uint8_t []){0x89}, 1, 0},
    {0x0c, (uint8_t []){0x00}, 1, 0},
    {0x0d, (uint8_t []){0x79}, 1, 0},
    {0x0a, (uint8_t []){0x00}, 1, 0},
    {0x0b, (uint8_t []){0x67}, 1, 0},
    {0x08, (uint8_t []){0x00}, 1, 0},
    {0x09, (uint8_t []){0x55}, 1, 0},
    {0x06, (uint8_t []){0x00}, 1, 0},
    {0x07, (uint8_t []){0x3F}, 1, 0},
    {0x04, (uint8_t []){0x00}, 1, 0},
    {0x05, (uint8_t []){0x28}, 1, 0},
    {0x02, (uint8_t []){0x00}, 1, 0},
    {0x03, (uint8_t []){0x0E}, 1, 0},
    {0xff, (uint8_t []){0x20, 0x10, 0x00}, 3, 0},

    // --- Register page 0x32: Gamma curve (negative) ---
    {0xff, (uint8_t []){0x20, 0x10, 0x32}, 3, 0},
    {0x38, (uint8_t []){0x03}, 1, 0},
    {0x39, (uint8_t []){0xf0}, 1, 0},
    {0x36, (uint8_t []){0x03}, 1, 0},
    {0x37, (uint8_t []){0xe8}, 1, 0},
    {0x34, (uint8_t []){0x03}, 1, 0},
    {0x35, (uint8_t []){0xCF}, 1, 0},
    {0x32, (uint8_t []){0x03}, 1, 0},
    {0x33, (uint8_t []){0xBA}, 1, 0},
    {0x30, (uint8_t []){0x03}, 1, 0},
    {0x31, (uint8_t []){0xA2}, 1, 0},
    {0x2e, (uint8_t []){0x03}, 1, 0},
    {0x2f, (uint8_t []){0x95}, 1, 0},
    {0x2c, (uint8_t []){0x03}, 1, 0},
    {0x2d, (uint8_t []){0x7e}, 1, 0},
    {0x2a, (uint8_t []){0x03}, 1, 0},
    {0x2b, (uint8_t []){0x62}, 1, 0},
    {0x28, (uint8_t []){0x03}, 1, 0},
    {0x29, (uint8_t []){0x44}, 1, 0},
    {0x26, (uint8_t []){0x02}, 1, 0},
    {0x27, (uint8_t []){0xfc}, 1, 0},
    {0x24, (uint8_t []){0x02}, 1, 0},
    {0x25, (uint8_t []){0xd0}, 1, 0},
    {0x22, (uint8_t []){0x02}, 1, 0},
    {0x23, (uint8_t []){0x98}, 1, 0},
    {0x20, (uint8_t []){0x02}, 1, 0},
    {0x21, (uint8_t []){0x6f}, 1, 0},
    {0x1e, (uint8_t []){0x02}, 1, 0},
    {0x1f, (uint8_t []){0x32}, 1, 0},
    {0x1c, (uint8_t []){0x01}, 1, 0},
    {0x1d, (uint8_t []){0xf6}, 1, 0},
    {0x1a, (uint8_t []){0x01}, 1, 0},
    {0x1b, (uint8_t []){0xb8}, 1, 0},
    {0x18, (uint8_t []){0x01}, 1, 0},
    {0x19, (uint8_t []){0x6E}, 1, 0},
    {0x16, (uint8_t []){0x01}, 1, 0},
    {0x17, (uint8_t []){0x41}, 1, 0},
    {0x14, (uint8_t []){0x00}, 1, 0},
    {0x15, (uint8_t []){0xfd}, 1, 0},
    {0x12, (uint8_t []){0x00}, 1, 0},
    {0x13, (uint8_t []){0xCf}, 1, 0},
    {0x10, (uint8_t []){0x00}, 1, 0},
    {0x11, (uint8_t []){0x98}, 1, 0},
    {0x0e, (uint8_t []){0x00}, 1, 0},
    {0x0f, (uint8_t []){0x89}, 1, 0},
    {0x0c, (uint8_t []){0x00}, 1, 0},
    {0x0d, (uint8_t []){0x79}, 1, 0},
    {0x0a, (uint8_t []){0x00}, 1, 0},
    {0x0b, (uint8_t []){0x67}, 1, 0},
    {0x08, (uint8_t []){0x00}, 1, 0},
    {0x09, (uint8_t []){0x55}, 1, 0},
    {0x06, (uint8_t []){0x00}, 1, 0},
    {0x07, (uint8_t []){0x3F}, 1, 0},
    {0x04, (uint8_t []){0x00}, 1, 0},
    {0x05, (uint8_t []){0x28}, 1, 0},
    {0x02, (uint8_t []){0x00}, 1, 0},
    {0x03, (uint8_t []){0x0E}, 1, 0},
    {0xff, (uint8_t []){0x20, 0x10, 0x00}, 3, 0},

    // --- Register page 0x11: Pixel timing (second pass) ---
    {0xFF, (uint8_t []){0x20, 0x10, 0x11}, 3, 0},
    {0x60, (uint8_t []){0x01}, 1, 0},
    {0x65, (uint8_t []){0x03}, 1, 0},
    {0x66, (uint8_t []){0x38}, 1, 0},
    {0x67, (uint8_t []){0x04}, 1, 0},
    {0x68, (uint8_t []){0x34}, 1, 0},
    {0x69, (uint8_t []){0x03}, 1, 0},
    {0x61, (uint8_t []){0x03}, 1, 0},
    {0x62, (uint8_t []){0x38}, 1, 0},
    {0x63, (uint8_t []){0x04}, 1, 0},
    {0x64, (uint8_t []){0x34}, 1, 0},
    {0x0A, (uint8_t []){0x11}, 1, 0},
    {0x0B, (uint8_t []){0x20}, 1, 0},
    {0x0c, (uint8_t []){0x20}, 1, 0},
    {0x55, (uint8_t []){0x06}, 1, 0},

    // --- Register page 0x42 (second pass) ---
    {0xFF, (uint8_t []){0x20, 0x10, 0x42}, 3, 0},
    {0x05, (uint8_t []){0x3D}, 1, 0},
    {0x06, (uint8_t []){0x03}, 1, 0},

    // --- Register page 0x00 then 0x12 ---
    {0xFF, (uint8_t []){0x20, 0x10, 0x00}, 3, 0},
    {0xFF, (uint8_t []){0x20, 0x10, 0x12}, 3, 0},
    {0x1F, (uint8_t []){0xDC}, 1, 0},

    // --- Register page 0x17 (second pass) ---
    {0xff, (uint8_t []){0x20, 0x10, 0x17}, 3, 0},
    {0x11, (uint8_t []){0xAA}, 1, 0},
    {0x16, (uint8_t []){0x12}, 1, 0},
    {0x0B, (uint8_t []){0xC3}, 1, 0},
    {0x10, (uint8_t []){0x0E}, 1, 0},
    {0x14, (uint8_t []){0xAA}, 1, 0},
    {0x18, (uint8_t []){0xA0}, 1, 0},
    {0x1A, (uint8_t []){0x80}, 1, 0},
    {0x1F, (uint8_t []){0x80}, 1, 0},

    // --- Register page 0x11 (third pass) ---
    {0xff, (uint8_t []){0x20, 0x10, 0x11}, 3, 0},
    {0x30, (uint8_t []){0xEE}, 1, 0},

    // --- Register page 0x12 (third pass) ---
    {0xff, (uint8_t []){0x20, 0x10, 0x12}, 3, 0},
    {0x15, (uint8_t []){0x0F}, 1, 0},

    // --- Register page 0x2D ---
    {0xff, (uint8_t []){0x20, 0x10, 0x2D}, 3, 0},
    {0x01, (uint8_t []){0x3E}, 1, 0},

    // --- Register page 0x40 (second pass) ---
    {0xff, (uint8_t []){0x20, 0x10, 0x40}, 3, 0},
    {0x83, (uint8_t []){0xC4}, 1, 0},

    // --- Register page 0x12 (fourth pass) ---
    {0xFF, (uint8_t []){0x20, 0x10, 0x12}, 3, 0},
    {0x00, (uint8_t []){0xCC}, 1, 0},
    {0x36, (uint8_t []){0xA0}, 1, 0},
    {0x2A, (uint8_t []){0x2D}, 1, 0},
    {0x2B, (uint8_t []){0x1e}, 1, 0},
    {0x2C, (uint8_t []){0x26}, 1, 0},
    {0x2D, (uint8_t []){0x2D}, 1, 0},
    {0x2E, (uint8_t []){0x1e}, 1, 0},
    {0x1F, (uint8_t []){0xE6}, 1, 0},

    // --- Register page 0xA0 (second pass) ---
    {0xFF, (uint8_t []){0x20, 0x10, 0xA0}, 3, 0},
    {0x08, (uint8_t []){0xE6}, 1, 0},

    // --- Register page 0x12 (fifth pass) ---
    {0xFF, (uint8_t []){0x20, 0x10, 0x12}, 3, 0},
    {0x10, (uint8_t []){0x0F}, 1, 0},

    // --- Register page 0x18 ---
    {0xFF, (uint8_t []){0x20, 0x10, 0x18}, 3, 0},
    {0x01, (uint8_t []){0x01}, 1, 0},
    {0x00, (uint8_t []){0x1E}, 1, 0},

    // --- Register page 0x43 ---
    {0xFF, (uint8_t []){0x20, 0x10, 0x43}, 3, 0},
    {0x03, (uint8_t []){0x04}, 1, 0},

    // --- Register page 0x18 (second pass) ---
    {0xFF, (uint8_t []){0x20, 0x10, 0x18}, 3, 0},
    {0x3A, (uint8_t []){0x01}, 1, 0},

    // --- Register page 0x50 ---
    {0xFF, (uint8_t []){0x20, 0x10, 0x50}, 3, 0},
    {0x05, (uint8_t []){0x08}, 1, 0},
    {0xFF, (uint8_t []){0x20, 0x10, 0x00}, 3, 0},
    {0xFF, (uint8_t []){0x20, 0x10, 0x50}, 3, 0},
    {0x00, (uint8_t []){0xA6}, 1, 0},
    {0x01, (uint8_t []){0xA6}, 1, 0},
    {0xFF, (uint8_t []){0x20, 0x10, 0x00}, 3, 0},
    {0xFF, (uint8_t []){0x20, 0x10, 0x50}, 3, 0},
    {0x08, (uint8_t []){0x55}, 1, 0},
    {0xFF, (uint8_t []){0x20, 0x10, 0x00}, 3, 0},

    // --- Register page 0x10 (second pass) ---
    {0xFF, (uint8_t []){0x20, 0x10, 0x10}, 3, 0},
    {0x0B, (uint8_t []){0x43}, 1, 0},
    {0x0C, (uint8_t []){0x12}, 1, 0},
    {0x10, (uint8_t []){0x01}, 1, 0},
    {0x11, (uint8_t []){0x12}, 1, 0},
    {0x15, (uint8_t []){0x00}, 1, 0},
    {0x16, (uint8_t []){0x00}, 1, 0},
    {0x1A, (uint8_t []){0x00}, 1, 0},
    {0x1B, (uint8_t []){0x00}, 1, 0},
    {0x61, (uint8_t []){0x00}, 1, 0},
    {0x62, (uint8_t []){0x00}, 1, 0},
    {0x51, (uint8_t []){0x11}, 1, 0},
    {0x55, (uint8_t []){0x55}, 1, 0},
    {0x58, (uint8_t []){0x00}, 1, 0},
    {0x5C, (uint8_t []){0x00}, 1, 0},

    // --- Register page 0x10 (third pass): CLK mapping ---
    {0xFF, (uint8_t []){0x20, 0x10, 0x10}, 3, 0},
    {0x20, (uint8_t []){0x81}, 1, 0},
    {0x21, (uint8_t []){0x82}, 1, 0},
    {0x22, (uint8_t []){0x72}, 1, 0},
    {0x30, (uint8_t []){0x00}, 1, 0},
    {0x31, (uint8_t []){0x00}, 1, 0},
    {0x32, (uint8_t []){0x00}, 1, 0},

    // --- Register page 0x10 (fourth pass): source drive ---
    {0xFF, (uint8_t []){0x20, 0x10, 0x10}, 3, 0},
    {0x44, (uint8_t []){0x44}, 1, 0},
    {0x45, (uint8_t []){0x55}, 1, 0},
    {0x46, (uint8_t []){0x66}, 1, 0},
    {0x47, (uint8_t []){0x77}, 1, 0},
    {0x49, (uint8_t []){0x00}, 1, 0},
    {0x4A, (uint8_t []){0x00}, 1, 0},
    {0x4B, (uint8_t []){0x00}, 1, 0},

    // --- Register page 0x17 (third pass) ---
    {0xFF, (uint8_t []){0x20, 0x10, 0x17}, 3, 0},
    {0x37, (uint8_t []){0x00}, 1, 0},

    // --- Register page 0x15 (second pass): revised GIP forward scan ---
    {0xFF, (uint8_t []){0x20, 0x10, 0x15}, 3, 0},
    {0x04, (uint8_t []){0x08}, 1, 0},
    {0x05, (uint8_t []){0x04}, 1, 0},
    {0x06, (uint8_t []){0x1C}, 1, 0},
    {0x07, (uint8_t []){0x1A}, 1, 0},
    {0x08, (uint8_t []){0x18}, 1, 0},
    {0x09, (uint8_t []){0x16}, 1, 0},
    {0x24, (uint8_t []){0x05}, 1, 0},
    {0x25, (uint8_t []){0x09}, 1, 0},
    {0x26, (uint8_t []){0x17}, 1, 0},
    {0x27, (uint8_t []){0x19}, 1, 0},
    {0x28, (uint8_t []){0x1B}, 1, 0},
    {0x29, (uint8_t []){0x1D}, 1, 0},

    // --- Register page 0x16 (second pass): revised GIP backward scan ---
    {0xFF, (uint8_t []){0x20, 0x10, 0x16}, 3, 0},
    {0x04, (uint8_t []){0x09}, 1, 0},
    {0x05, (uint8_t []){0x05}, 1, 0},
    {0x06, (uint8_t []){0x1D}, 1, 0},
    {0x07, (uint8_t []){0x1B}, 1, 0},
    {0x08, (uint8_t []){0x19}, 1, 0},
    {0x09, (uint8_t []){0x17}, 1, 0},
    {0x24, (uint8_t []){0x04}, 1, 0},
    {0x25, (uint8_t []){0x08}, 1, 0},
    {0x26, (uint8_t []){0x16}, 1, 0},
    {0x27, (uint8_t []){0x18}, 1, 0},
    {0x28, (uint8_t []){0x1A}, 1, 0},
    {0x29, (uint8_t []){0x1C}, 1, 0},

    // --- Register page 0x18 (third pass) ---
    {0xFF, (uint8_t []){0x20, 0x10, 0x18}, 3, 0},
    {0x1F, (uint8_t []){0x02}, 1, 0},

    // --- Register page 0x11 (fourth pass) ---
    {0xFF, (uint8_t []){0x20, 0x10, 0x11}, 3, 0},
    {0x15, (uint8_t []){0x99}, 1, 0},
    {0x16, (uint8_t []){0x99}, 1, 0},
    {0x1C, (uint8_t []){0x88}, 1, 0},
    {0x1D, (uint8_t []){0x88}, 1, 0},
    {0x1E, (uint8_t []){0x88}, 1, 0},
    {0x13, (uint8_t []){0xf0}, 1, 0},
    {0x14, (uint8_t []){0x34}, 1, 0},

    // --- Register page 0x12 (sixth pass) ---
    {0xFF, (uint8_t []){0x20, 0x10, 0x12}, 3, 0},
    {0x12, (uint8_t []){0x89}, 1, 0},
    {0x06, (uint8_t []){0x06}, 1, 0},
    {0x18, (uint8_t []){0x00}, 1, 0},

    // --- Register page 0x11 (fifth pass) ---
    {0xFF, (uint8_t []){0x20, 0x10, 0x11}, 3, 0},
    {0x0A, (uint8_t []){0x00}, 1, 0},
    {0x0B, (uint8_t []){0xF0}, 1, 0},
    {0x0c, (uint8_t []){0xF0}, 1, 0},
    {0x6A, (uint8_t []){0x10}, 1, 0},
    {0xFF, (uint8_t []){0x20, 0x10, 0x00}, 3, 0},

    // --- Register page 0x11 (sixth pass): TE/sync ---
    {0xFF, (uint8_t []){0x20, 0x10, 0x11}, 3, 0},
    {0x08, (uint8_t []){0x70}, 1, 0},
    {0x09, (uint8_t []){0x00}, 1, 0},
    {0xFF, (uint8_t []){0x20, 0x10, 0x00}, 3, 0},

    // --- Standard MIPI TE enable ---
    {0x35, (uint8_t []){0x00}, 1, 0},

    // --- Register page 0x12 (seventh pass) ---
    {0xFF, (uint8_t []){0x20, 0x10, 0x12}, 3, 0},
    {0x21, (uint8_t []){0x70}, 1, 0},

    // --- Register page 0x2D (second pass) ---
    {0xFF, (uint8_t []){0x20, 0x10, 0x2D}, 3, 0},
    {0x02, (uint8_t []){0x00}, 1, 0},
    {0xFF, (uint8_t []){0x20, 0x10, 0x00}, 3, 0},

    // --- SLEEP OUT: mandatory last entry; library waits 120 ms after sending ---
    {0x11, (uint8_t []){0x00}, 0, 120},
};
```

### What `panel_spd2010_init()` sends before the table

Before iterating through `vendor_specific_init_default`, the init function sends three additional commands using the active `madctl_val` and `colmod_val` from the panel config:

```c
// 1. Select user command page
tx_param(spd2010, io, 0xFF, (uint8_t[]){0x20, 0x10, 0x00}, 3);

// 2. Memory access control (rotation/flip) - value depends on esp_lcd_panel_mirror() settings
tx_param(spd2010, io, 0x36, (uint8_t[]){madctl_val}, 1);
// Default: 0x00 (no mirror, no swap)

// 3. Pixel format - value depends on bits_per_pixel in panel_config
tx_param(spd2010, io, 0x3A, (uint8_t[]){colmod_val}, 1);
// 16-bit color: 0x55
// 18-bit color: 0x66
// 24-bit color: 0x77
```

---

## 6. Sleep-Out (0x11) and Display-On (0x29) Handling

### Sleep-Out (0x11)
YES, the SPD2010 requires a sleep-out command. It is the final entry in the `vendor_specific_init_default` table:

```c
{0x11, (uint8_t []){0x00}, 0, 120},
```

Note that `data_bytes = 0` - the data array pointer is present but the size is 0, meaning the command is sent with no data payload. The `delay_ms = 120` causes a mandatory 120 ms wait after transmitting the command, which is required by the SPD2010 for the oscillator and power circuits to stabilize before accepting further commands.

This command is sent automatically by `esp_lcd_panel_init()` as the last step of the vendor init table. You do not need to send it manually.

### Display-On (0x29)
YES, the SPD2010 also requires a display-on command, but it is NOT part of the vendor init table. It is sent separately through `panel_spd2010_disp_on_off()`:

```c
static esp_err_t panel_spd2010_disp_on_off(esp_lcd_panel_t *panel, bool on_off)
{
    int command = on_off ? LCD_CMD_DISPON : LCD_CMD_DISPOFF;
    // LCD_CMD_DISPON = 0x29
    // LCD_CMD_DISPOFF = 0x28
    tx_param(spd2010, io, command, NULL, 0);
    return ESP_OK;
}
```

You trigger this by calling `esp_lcd_panel_disp_on_off(panel_handle, true)` after `esp_lcd_panel_init()` returns.

### Complete power-on sequence summary

```
1. esp_lcd_panel_reset()
     - If reset GPIO assigned: pulse it low (10 ms), then high (120 ms)
     - If no reset GPIO: send 0x01 (SWRESET), wait 20 ms

2. esp_lcd_panel_init()
     - Sends 0xFF {0x20, 0x10, 0x00}   (select user page)
     - Sends 0x36 {madctl_val}          (memory access control)
     - Sends 0x3A {colmod_val}          (pixel format, e.g. 0x55 for RGB565)
     - Sends all 291 vendor_specific_init_default[] entries
     - Last entry: 0x11 {no data} + 120 ms delay  (SLEEP OUT)

3. esp_lcd_panel_mirror()   (optional rotation/flip)
     - Sends 0x36 with updated MADCTL byte

4. esp_lcd_panel_disp_on_off(panel, true)
     - Sends 0x29 (DISPLAY ON)

5. Set backlight via GPIO/LEDC PWM
```

---

## 7. QSPI Wire Encoding

The SPD2010 uses a non-standard QSPI command encoding. The ESP-IDF `spi_lcd` layer encodes the 32-bit command word as follows:

```c
#define LCD_OPCODE_WRITE_CMD    (0x02ULL)  // single-wire command write
#define LCD_OPCODE_READ_CMD     (0x0BULL)  // single-wire command read
#define LCD_OPCODE_WRITE_COLOR  (0x32ULL)  // quad-wire pixel data write
```

For a command write the 32-bit word sent on the SPI bus is:

```
bits[31:24] = 0x02   (write opcode)
bits[23:8]  = 0x0000 (address placeholder)
bits[7:0]   = cmd   (actual command byte, e.g. 0x11, 0x29)
```

The parameter bytes that follow are sent as normal SPI data bytes. This is why `lcd_cmd_bits = 32` and `lcd_param_bits = 8` must be set in the panel IO config when using QSPI mode - the driver SPI layer uses this to know how to package the 32-bit opcode word.

Pixel data writes use opcode 0x32 and are transferred using all four data lines simultaneously (quad mode), giving 4x throughput compared to single-wire MOSI.

---

## 8. The panel_spd2010_init() and panel_spd2010_reset() Functions

### Reset function

```c
static esp_err_t panel_spd2010_reset(esp_lcd_panel_t *panel)
{
    if (spd2010->reset_gpio_num >= 0) {
        gpio_set_level(spd2010->reset_gpio_num, spd2010->flags.reset_level);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level(spd2010->reset_gpio_num, !spd2010->flags.reset_level);
        vTaskDelay(pdMS_TO_TICKS(120));
    } else {
        // Software reset
        tx_param(spd2010, io, LCD_CMD_SWRESET, NULL, 0);  // 0x01
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return ESP_OK;
}
```

The Watcher uses the software reset path since `BSP_LCD_GPIO_RST = GPIO_NUM_NC`.

### Init function (abbreviated)

The init function:
1. Sends the 3-command preamble (0xFF page select, 0x36 MADCTL, 0x3A COLMOD)
2. Iterates through either `spd2010->init_cmds` (if user supplied custom commands via `vendor_config`) or the built-in `vendor_specific_init_default[]`
3. For each entry: sends the command with its data, then calls `vTaskDelay` for `delay_ms` milliseconds
4. Tracks whether the user's custom sequence has overwritten MADCTL or COLMOD and logs a warning if so

---

## 9. Arduino / Non-IDF Usage

The `NickyDark1/lvgl_arduino_only_btn_msg` project demonstrates using the Espressif component from the Arduino framework:

```cpp
// Reset via IO expander (EXIO_PIN2)
void SPD2010_Reset() {
    digitalWrite(EXIO_PIN2, LOW);
    delay(50);
    digitalWrite(EXIO_PIN2, HIGH);
    delay(50);
}

void SPD2010_Init() {
    SPD2010_Reset();

    // QSPI bus: SPI2_HOST, pins DATA0-3 + SCK
    // Frequency: ESP_PANEL_LCD_SPI_CLK_HZ
    spi_bus_initialize(SPI2_HOST, &host_config, SPI_DMA_CH_AUTO);

    // Panel IO: quad_mode=1, no DC pin, mode 3
    // vendor_config: use_qspi_interface = 1
    esp_lcd_new_panel_io_spi(...)
    esp_lcd_new_panel_spd2010(...)
    esp_lcd_panel_reset(panel_handle);
    esp_lcd_panel_init(panel_handle);
    esp_lcd_panel_disp_on_off(panel_handle, true);
}
```

The pattern is identical to the IDF factory firmware. The Arduino layer wraps the same esp-idf SPI and LCD APIs.

---

## 10. Common Pitfalls

### 1. Missing display-on call
`esp_lcd_panel_init()` sends sleep-out (0x11) but NOT display-on (0x29). If you skip `esp_lcd_panel_disp_on_off(panel, true)`, the panel remains in sleep-out state but the display output is off. The backlight will be on but the screen stays dark.

### 2. Wrong command bit width in QSPI mode
Using `lcd_cmd_bits = 8` in QSPI mode will corrupt the 32-bit opcode word. The SPD2010 QSPI interface requires `lcd_cmd_bits = 32`. In regular SPI mode the correct value is 8.

### 3. Wrong SPI mode
SPI mode must be 3 (CPOL=1, CPHA=1). Using mode 0 will result in garbage or no response from the controller.

### 4. 4-pixel draw alignment
When calling `esp_lcd_panel_draw_bitmap()`, both `x_start` and `x_end` must be divisible by 4. The QSPI interface transfers pixels in groups of 4 due to the 4-lane data bus. Drawing an odd-width rectangle will produce display corruption.

### 5. FPC cable integrity
A known hardware issue documented in GitHub issue #312 is that the SPD2010 module's FPC (flex cable) is extremely fragile. Display initializes successfully (you see the log "LCD panel create success") but the screen stays dark or flickers due to poor FPC contact. This is a hardware, not software, problem.

### 6. No DC pin in QSPI mode
Setting `dc_gpio_num` to any real GPIO when using QSPI mode is incorrect. The DC signal is encoded in the 32-bit opcode word, not a physical pin. Set `dc_gpio_num = -1` for QSPI.

### 7. Backlight must be enabled separately
`esp_lcd_panel_disp_on_off()` controls the display output data path, not the physical backlight GPIO. Even after calling `disp_on_off(true)`, you still need to drive GPIO 8 (on the Watcher) high via LEDC PWM or direct GPIO to actually illuminate the panel.

---

## 11. Sources

- [espressif/esp_lcd_spd2010 on ESP Component Registry (v1.0.2)](https://components.espressif.com/components/espressif/esp_lcd_spd2010) - official component with full README and datasheet link
- [esp_lcd_spd2010.c source - espressif/esp-iot-solution on GitHub](https://github.com/espressif/esp-iot-solution/blob/master/components/display/lcd/esp_lcd_spd2010/esp_lcd_spd2010.c) - complete driver source including the 291-entry init table
- [esp_lcd_spd2010.h header - espressif/esp-iot-solution on GitHub](https://github.com/espressif/esp-iot-solution/blob/master/components/display/lcd/esp_lcd_spd2010/include/esp_lcd_spd2010.h) - all macros, structs, and defaults
- [Seeed-Studio/SenseCAP-Watcher-Firmware on GitHub](https://github.com/Seeed-Studio/SenseCAP-Watcher-Firmware) - factory firmware SDK
- [sensecap-watcher.h (factory BSP pins)](https://raw.githubusercontent.com/Seeed-Studio/SenseCAP-Watcher-Firmware/main/components/sensecap-watcher/include/sensecap-watcher.h) - GPIO assignments and clock config
- [sensecap-watcher.c (factory BSP init)](https://raw.githubusercontent.com/Seeed-Studio/SenseCAP-Watcher-Firmware/main/components/sensecap-watcher/sensecap-watcher.c) - bsp_lcd_pannel_init() and bsp_spi_bus_init()
- [NickyDark1/lvgl_arduino_only_btn_msg - Display_SPD2010.cpp](https://github.com/NickyDark1/lvgl_arduino_only_btn_msg/blob/main/Display_SPD2010.cpp) - Arduino framework usage example
- [esp-iot-solution issue #312: spd2010 idf component problem](https://github.com/espressif/esp-iot-solution/issues/312) - FPC cable failure mode
- [espressif/esp_lcd_spd2010 v1.0.1 QSPI example](https://components.espressif.com/components/espressif/esp_lcd_spd2010/versions/1.0.1/examples/qspi_with_ram) - official usage example
