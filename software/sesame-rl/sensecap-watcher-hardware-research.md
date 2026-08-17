# SenseCAP Watcher: Display and Audio Hardware Research

## Executive Summary

The SenseCAP Watcher uses a **Solomon Systech SPD2010** circular LCD controller (1.45-inch, 412x412 pixels) connected via **QSPI (Quad-SPI)** on SPI3 host, and an **ES8311** audio DAC (speaker) paired with an **ES7243E** (or ES7243) ADC (microphone) connected via I2S. All GPIO pin assignments were confirmed from the factory firmware header file at `components/sensecap-watcher/include/sensecap-watcher.h`. Standard Arduino libraries (TFT_eSPI, LovyanGFX) do not natively support the SPD2010 in QSPI mode; the **ESP32_Display_Panel** Arduino library and the Espressif **esp_lcd_spd2010** IDF component are the recommended paths.

---

## Table of Contents

1. [Device Overview](#1-device-overview)
2. [Display Hardware](#2-display-hardware)
3. [Audio Hardware](#3-audio-hardware)
4. [Complete GPIO Pin Table](#4-complete-gpio-pin-table)
5. [Display Initialization Details](#5-display-initialization-details)
6. [Audio Initialization Details](#6-audio-initialization-details)
7. [IO Expander (Power Control)](#7-io-expander-power-control)
8. [Arduino Library Compatibility](#8-arduino-library-compatibility)
9. [Resources and Sources](#9-resources-and-sources)

---

## 1. Device Overview

| Property | Value |
|---|---|
| Main MCU | ESP32-S3 |
| AI Coprocessor | Himax WiseEye2 HX6538 (Arm Cortex-M55 + Ethos-U55) |
| Display | 1.45-inch circular, 412x412 pixels |
| Display Controller | Solomon Systech SPD2010 (TDDI - touch + display in one IC) |
| Display Interface | QSPI (Quad SPI) on SPI3 host |
| Audio DAC / Amplifier codec | Everest Semi ES8311 |
| Audio ADC / Microphone codec | Everest Semi ES7243E (or ES7243, detected at runtime via I2C) |
| Audio Interface | I2S (I2S0), duplex mono |
| IO Expander | PCA9535 / TCA9555 16-bit I2C expander (for power control, SD detect, knob button) |
| Framework (factory firmware) | ESP-IDF v5.2.1 |

---

## 2. Display Hardware

### Controller Chip

**Solomon Systech SPD2010** - a TDDI (Touch and Display Driver Integration) IC that combines the LCD panel driver and capacitive touch controller in one chip. The LCD driver is accessed over QSPI; the touch controller is accessed over a separate I2C bus.

- Manufacturer product page: https://www.solomon-systech.com/solomon-systech-launches-spd2010-for-1st-full-color-tddi-in-wearable-display
- Datasheet (Espressif mirror): https://dl.espressif.com/AE/esp-iot-solution/SPD2010(L-WEA2010)_0.50.pdf

### Display Specifications

| Property | Value |
|---|---|
| Resolution | 412 x 412 pixels |
| Color depth | 16 bits per pixel (RGB565) |
| Interface to ESP32-S3 | QSPI (Quad SPI), SPI3 host, `spi_mode = 3` |
| Pixel clock | 40 MHz (`DRV_LCD_PIXEL_CLK_HZ = 40 * 1000 * 1000`) |
| LCD command bits | 32 |
| LCD param bits | 8 |
| Backlight control | GPIO 8, LEDC PWM channel 1, 10-bit duty, active HIGH |
| Chip Select (CS) | GPIO 45 |
| Reset (RST) | Not connected (`GPIO_NUM_NC`) |
| DC pin | GPIO 1 (also doubles as QSPI DATA1 - this is a QSPI detail, not a separate DC line; `dc_gpio_num = -1` in QSPI mode) |

**Note on alignment**: The SPD2010 requires that both `x_start` and `x_end` coordinates are divisible by 4. In LVGL this is handled via a `rounder_cb` callback.

### QSPI Bus Pins (SPI3 Host)

| Signal | GPIO |
|---|---|
| PCLK (Clock) | GPIO 7 |
| DATA0 (MOSI / D0) | GPIO 9 |
| DATA1 (D1) | GPIO 1 |
| DATA2 (D2) | GPIO 14 |
| DATA3 (D3) | GPIO 13 |
| CS | GPIO 45 |

### Touch Controller (SPD2010 integrated, I2C)

The touch function of the SPD2010 is on a dedicated I2C bus (separate from the general I2C bus):

| Signal | GPIO |
|---|---|
| SDA | GPIO 39 |
| SCL | GPIO 38 |
| Interrupt | IO Expander pin 5 |
| I2C Address | 0x53 |

---

## 3. Audio Hardware

### Audio Codec Chips

| Role | Chip | I2C Address | Notes |
|---|---|---|---|
| DAC (speaker output) | Everest Semi ES8311 | 0x30 | Single-supply mono codec |
| ADC (microphone input) | Everest Semi ES7243E | 0x14 | Preferred; detected at boot |
| ADC (microphone input, fallback) | Everest Semi ES7243 | 0x13 | Used if ES7243E not found |

Both codecs are controlled via the **General I2C Bus** (GPIO 47 SDA / GPIO 48 SCL). Audio data flows over the shared **I2S0** bus.

### I2S Pin Assignments

| Signal | GPIO | Direction |
|---|---|---|
| MCLK (Master Clock) | GPIO 10 | Output from ESP32-S3 |
| BCLK (Bit Clock / SCLK) | GPIO 11 | Output (master mode) |
| LRCK (Word Select / LR Clock) | GPIO 12 | Output (master mode) |
| DSIN (Data In - from mic/ADC) | GPIO 15 | Input to ESP32-S3 |
| DOUT (Data Out - to speaker/DAC) | GPIO 16 | Output from ESP32-S3 |

### Audio Configuration Constants

| Parameter | Value |
|---|---|
| I2S peripheral | I2S0 (`BSP_AUDIO_I2S_NUM = 0`) |
| Sample rate | 16,000 Hz |
| Bit depth | 16-bit |
| Channels | Mono (1 channel) |
| Mic gain | 27.0 dB |
| Codec supply voltage | 3.3V (codec), 5.0V (power amplifier) |
| ES8311 mode | Slave mode (ESP32-S3 is I2S master) |
| MCLK | Enabled (not digital mic) |

### Audio Codec I2C Control Bus

| Signal | GPIO |
|---|---|
| SDA | GPIO 47 |
| SCL | GPIO 48 |

---

## 4. Complete GPIO Pin Table

Sourced from `components/sensecap-watcher/include/sensecap-watcher.h` in the factory firmware.

```c
// ---- QSPI Bus (SPI3 Host) - LCD Display ----
#define BSP_SPI3_HOST_PCLK   (GPIO_NUM_7)
#define BSP_SPI3_HOST_DATA0  (GPIO_NUM_9)
#define BSP_SPI3_HOST_DATA1  (GPIO_NUM_1)
#define BSP_SPI3_HOST_DATA2  (GPIO_NUM_14)
#define BSP_SPI3_HOST_DATA3  (GPIO_NUM_13)

// ---- LCD Panel ----
#define BSP_LCD_SPI_CS       (GPIO_NUM_45)
#define BSP_LCD_GPIO_RST     (GPIO_NUM_NC)   // Not connected
#define BSP_LCD_GPIO_DC      (GPIO_NUM_1)    // Same as DATA1 in QSPI; dc_gpio_num = -1 in QSPI mode
#define BSP_LCD_GPIO_BL      (GPIO_NUM_8)    // Backlight PWM

// ---- Touch Panel I2C (dedicated bus) ----
#define BSP_TOUCH_I2C_SDA    (GPIO_NUM_39)
#define BSP_TOUCH_I2C_SCL    (GPIO_NUM_38)
// Touch interrupt: IO Expander pin 5
// Touch I2C address: 0x53

// ---- General I2C Bus (codecs, expander) ----
#define BSP_GENERAL_I2C_SDA  (GPIO_NUM_47)
#define BSP_GENERAL_I2C_SCL  (GPIO_NUM_48)

// ---- Audio I2S (I2S0) ----
#define BSP_AUDIO_I2S_MCLK   (GPIO_NUM_10)
#define BSP_AUDIO_I2S_SCLK   (GPIO_NUM_11)   // BCLK
#define BSP_AUDIO_I2S_LRCK   (GPIO_NUM_12)   // WS
#define BSP_AUDIO_I2S_DSIN   (GPIO_NUM_15)   // Data In (mic)
#define BSP_AUDIO_I2S_DOUT   (GPIO_NUM_16)   // Data Out (speaker)

// ---- SPI2 Host (SD Card, Camera) ----
#define BSP_SPI2_HOST_SCLK   (GPIO_NUM_4)
#define BSP_SPI2_HOST_MOSI   (GPIO_NUM_5)
#define BSP_SPI2_HOST_MISO   (GPIO_NUM_6)
#define BSP_SD_SPI_CS        (GPIO_NUM_46)
// SD detect: IO Expander pin 4

// ---- User Input ----
#define BSP_KNOB_A           (GPIO_NUM_41)   // Rotary encoder A
#define BSP_KNOB_B           (GPIO_NUM_42)   // Rotary encoder B
// Knob button: IO Expander pin 3

// ---- RGB LED ----
#define BSP_RGB_CTRL         (GPIO_NUM_40)

// ---- IO Expander ----
#define BSP_IO_EXPANDER_INT  (GPIO_NUM_2)

// ---- SSCMA Client (Himax AI chip SPI) ----
#define BSP_SSCMA_CLIENT_SPI_CS   (GPIO_NUM_21)
#define BSP_SSCMA_FLASHER_UART_TX (GPIO_NUM_17)
#define BSP_SSCMA_FLASHER_UART_RX (GPIO_NUM_18)
```

---

## 5. Display Initialization Details

### QSPI Bus Configuration (from sensecap-watcher.c)

```c
const spi_bus_config_t qspi_cfg = {
    .sclk_io_num     = BSP_SPI3_HOST_PCLK,   // GPIO 7
    .data0_io_num    = BSP_SPI3_HOST_DATA0,  // GPIO 9
    .data1_io_num    = BSP_SPI3_HOST_DATA1,  // GPIO 1
    .data2_io_num    = BSP_SPI3_HOST_DATA2,  // GPIO 14
    .data3_io_num    = BSP_SPI3_HOST_DATA3,  // GPIO 13
    .max_transfer_sz = DRV_LCD_H_RES * DRV_LCD_V_RES *
                       DRV_LCD_BITS_PER_PIXEL / 8 /
                       CONFIG_BSP_LCD_SPI_DMA_SIZE_DIV,
};
spi_bus_initialize(SPI3_HOST, &qspi_cfg, SPI_DMA_CH_AUTO);
```

### Panel IO Configuration

```c
const esp_lcd_panel_io_spi_config_t io_config = {
    .cs_gpio_num      = BSP_LCD_SPI_CS,     // GPIO 45
    .dc_gpio_num      = -1,                 // Not used in QSPI mode
    .spi_mode         = 3,
    .pclk_hz          = DRV_LCD_PIXEL_CLK_HZ, // 40,000,000
    .trans_queue_depth= CONFIG_BSP_LCD_PANEL_SPI_TRANS_Q_DEPTH,
    .lcd_cmd_bits     = DRV_LCD_CMD_BITS,   // 32
    .lcd_param_bits   = DRV_LCD_PARAM_BITS, // 8
    .flags = {
        .quad_mode = true,
    },
};
```

### SPD2010 Vendor Configuration

```c
spd2010_vendor_config_t vendor_config = {
    .flags = {
        .use_qspi_interface = 1,
    },
};

esp_lcd_panel_dev_config_t panel_config = {
    .reset_gpio_num = BSP_LCD_GPIO_RST,     // GPIO_NUM_NC
    .rgb_ele_order  = LCD_RGB_ELEMENT_ORDER_RGB,
    .bits_per_pixel = DRV_LCD_BITS_PER_PIXEL, // 16
    .vendor_config  = &vendor_config,
};

esp_lcd_new_panel_spd2010(io_handle, &panel_config, &panel_handle);
esp_lcd_panel_reset(panel_handle);
esp_lcd_panel_init(panel_handle);
esp_lcd_panel_disp_on_off(panel_handle, true);
```

### Backlight Configuration

```c
// LEDC PWM backlight
// Channel: DRV_LCD_LEDC_CH = 1
// Timer resolution: LEDC_TIMER_10_BIT
// Frequency: 5000 Hz
// Active HIGH: DRV_LCD_BL_ON_LEVEL = 1
// GPIO: BSP_LCD_GPIO_BL = GPIO 8
```

### Espressif IDF Component Reference

The SPD2010 QSPI macros from Espressif's `esp_lcd_spd2010` component show the default QSPI clock is **20 MHz** in the generic component, but the SenseCAP Watcher firmware overrides this to **40 MHz** via `DRV_LCD_PIXEL_CLK_HZ`.

```c
// Generic Espressif component defaults (for reference):
#define SPD2010_PANEL_BUS_QSPI_CONFIG(sclk, d0, d1, d2, d3, max_trans_sz) \
    {                                          \
        .sclk_io_num      = sclk,              \
        .data0_io_num     = d0,                \
        .data1_io_num     = d1,                \
        .data2_io_num     = d2,                \
        .data3_io_num     = d3,                \
        .max_transfer_sz  = max_trans_sz,      \
    }

#define SPD2010_PANEL_IO_QSPI_CONFIG(cs, cb, cb_ctx)     \
    {                                          \
        .cs_gpio_num      = cs,                \
        .dc_gpio_num      = -1,                \
        .spi_mode         = 3,                 \
        .pclk_hz          = 20 * 1000 * 1000, \  // Watcher uses 40 MHz
        .trans_queue_depth= 10,                \
        .lcd_cmd_bits     = 32,                \
        .lcd_param_bits   = 8,                 \
        .flags = { .quad_mode = true },        \
    }
```

---

## 6. Audio Initialization Details

### I2S Duplex Mono Configuration

The firmware initializes I2S0 in duplex (simultaneous TX + RX) mono mode. Both channels share the same I2S peripheral. The RX channel uses `I2S_STD_SLOT_RIGHT` to receive from the microphone.

```c
// I2S channel creation
i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
chan_cfg.auto_clear    = true;
chan_cfg.intr_priority = 4;

// Standard I2S clock config:
// sample_rate_hz = 16000
// clk_src = I2S_CLK_SRC_DEFAULT
// mclk_multiple = I2S_MCLK_MULTIPLE_256

// Standard I2S slot config (16-bit mono):
// data_bit_width = I2S_DATA_BIT_WIDTH_16BIT
// slot_bit_width = I2S_SLOT_BIT_WIDTH_16BIT
// slot_mode = I2S_SLOT_MODE_MONO

// GPIO config for I2S:
// mclk = GPIO 10
// bclk = GPIO 11
// ws   = GPIO 12
// dout = GPIO 16   (to ES8311 DAC)
// din  = GPIO 15   (from ES7243/ES7243E ADC)
```

### ES8311 Speaker Codec

```c
es8311_codec_cfg_t es8311_cfg = {
    .ctrl_if       = i2c_ctrl_if,       // I2C on GPIO 47/48
    .codec_mode    = ESP_CODEC_DEV_WORK_MODE_DAC,
    .pa_pin        = GPIO_NUM_NC,        // Power amp pin (controlled via IO expander)
    .pa_reverted   = false,
    .master_mode   = false,              // Slave mode (ESP32-S3 is I2S master)
    .use_mclk      = true,
    .digital_mic   = false,
    .invert_mclk   = false,
    .invert_sclk   = false,
};
// I2C address: 0x30
// Supply: 3.3V codec, 5.0V PA
```

### ES7243E / ES7243 Microphone Codec

```c
// Detection logic: try ES7243 at 0x13 first, fall back to ES7243E at 0x14
// Both use I2C control on GPIO 47/48

es7243e_codec_cfg_t es7243e_cfg = {
    .ctrl_if   = i2c_ctrl_if,           // I2C on GPIO 47/48
    .codec_mode = ESP_CODEC_DEV_WORK_MODE_ADC,
    .master_mode = false,
    .use_mclk    = true,
};
// ES7243  I2C address: 0x13
// ES7243E I2C address: 0x14
```

### Setting Audio Parameters

```c
bsp_codec_set_fs(
    /* sample_rate */ 16000,
    /* bits        */ 16,
    /* channels    */ 1
);
// Mic gain: 27.0 dB
```

---

## 7. IO Expander (Power Control)

The device uses a **PCA9535 / TCA9555** 16-bit I2C GPIO expander on the General I2C bus (GPIO 47 SDA / GPIO 48 SCL). This expander controls power domains and receives interrupt status.

```
BSP_IO_EXPANDER_INT: GPIO 2  (expander interrupt to ESP32-S3)
DRV_IO_EXP_INPUT_MASK:  0x20FF  (P0.0-P0.7 and P1.3 as inputs)
DRV_IO_EXP_OUTPUT_MASK: 0xDF00  (P1.0-P1.7 except P1.3 as outputs)
```

Known expander pin assignments (from firmware analysis):

| Expander Pin | Function |
|---|---|
| Pin 3 | Knob button input |
| Pin 4 | SD card detect input |
| Pin 5 | Touch panel interrupt input |
| Power pins | LCD power, system power, AI chip power, codec power, SD card power |

Note: The power amplifier for the speaker is enabled via an IO expander output pin (not a direct GPIO), which is why `pa_pin = GPIO_NUM_NC` in the ES8311 config above.

---

## 8. Arduino Library Compatibility

### TFT_eSPI

**Not compatible** with the SPD2010 in QSPI mode. TFT_eSPI does not support Quad-SPI display controllers. It supports standard SPI and parallel interfaces only.

### LovyanGFX

**Partially compatible.** QSPI support landed in the LovyanGFX `develop` branch (renamed from `Bus_QSPI` to `Bus_SPI` with quad-mode config), but it relies on direct esp-idf calls that bypass the Arduino SPI library. This causes known limitations and tech debt. The SPD2010 is not listed as a supported QSPI panel in LovyanGFX as of early 2026.

Supported QSPI panels in LovyanGFX: SH8601Z, SH8501, RM67162, RM690B0, NV3041A.

### Arduino_GFX

**Potentially compatible.** Arduino_GFX supports Quad-SPI via the `Arduino_ESP32QSPI` bus class using esp-idf functions directly. The library lists SPD2010 in its driver collection. This is the most promising Arduino-ecosystem option, but a fully tested SenseCAP Watcher example has not been confirmed in public documentation.

### ESP32_Display_Panel (Espressif Arduino library)

**Best Arduino option.** This library (https://github.com/esp-arduino-libs/ESP32_Display_Panel) is Espressif's official Arduino display abstraction layer. It explicitly supports:
- **LCD driver**: SPD2010 (Solomon Systech)
- **Touch driver**: SPD2010 (Solomon Systech)
- ESP32-S3 QSPI bus type (`ESP_PANEL_BUS_TYPE_QSPI`)

This wraps the `esp_lcd_spd2010` component and is compatible with ESP-IDF Arduino core.

### Espressif esp_lcd_spd2010 (IDF component, recommended for ESP-IDF projects)

The authoritative driver. Install via:

```
idf.py add-dependency "espressif/esp_lcd_spd2010^2.0.0"
```

Or create a working QSPI example project:

```
idf.py create-project-from-example "espressif/esp_lcd_spd2010=1.0.2:qspi_with_ram"
```

### Audio Libraries for Arduino

| Library | ES8311 | ES7243/ES7243E | Notes |
|---|---|---|---|
| pschatzmann/arduino-audio-driver | Yes (`AudioDriverES8311`) | Yes (`AudioDriverES7243`, `AudioDriverES7243e`) | Most complete option |
| pschatzmann/arduino-audiokit | Yes | Yes | Heavier, board-oriented |
| ESPHome `es8311` component | Yes | No | ESPHome only |
| Raw Arduino I2S + I2C | Manual | Manual | Full control, no abstraction |

---

## 9. Resources and Sources

### Primary Source Files

- Factory firmware header (all GPIO pin defs): https://raw.githubusercontent.com/Seeed-Studio/SenseCAP-Watcher-Firmware/main/components/sensecap-watcher/include/sensecap-watcher.h
- Factory firmware implementation: https://raw.githubusercontent.com/Seeed-Studio/SenseCAP-Watcher-Firmware/main/components/sensecap-watcher/sensecap-watcher.c
- Component manifest (dependency versions): https://raw.githubusercontent.com/Seeed-Studio/SenseCAP-Watcher-Firmware/main/components/sensecap-watcher/idf_component.yml
- Agent firmware reference (additional context): https://files.seeedstudio.com/wiki/Watcher_Agent/Grove/sensecap_watcher.cc

### Espressif Drivers

- esp_lcd_spd2010 component registry: https://components.espressif.com/components/espressif/esp_lcd_spd2010
- esp_lcd_touch_spd2010 component: https://components.espressif.com/components/espressif/esp_lcd_touch_spd2010
- SPD2010 header in esp-iot-solution: https://github.com/espressif/esp-iot-solution/blob/master/components/display/lcd/esp_lcd_spd2010/include/esp_lcd_spd2010.h
- SPD2010 touch header in esp-iot-solution: https://github.com/espressif/esp-iot-solution/blob/master/components/display/lcd_touch/esp_lcd_touch_spd2010/include/esp_lcd_touch_spd2010.h
- ESP32_Display_Panel Arduino library: https://github.com/esp-arduino-libs/ESP32_Display_Panel

### Hardware Documentation

- OSHW repository (schematic PDF): https://github.com/Seeed-Studio/OSHW-SenseCAP-Watcher
- Schematic PDF: https://github.com/Seeed-Studio/OSHW-SenseCAP-Watcher/blob/main/Hardware/SenseCAP_Watcher_v1.0_SCH.pdf
- SPD2010 datasheet (Espressif mirror): https://dl.espressif.com/AE/esp-iot-solution/SPD2010(L-WEA2010)_0.50.pdf
- Solomon Systech product announcement: https://www.solomon-systech.com/solomon-systech-launches-spd2010-for-1st-full-color-tddi-in-wearable-display

### Firmware Repository

- Main SDK: https://github.com/Seeed-Studio/SenseCAP-Watcher-Firmware
- Hardware wiki: https://wiki.seeedstudio.com/watcher_hardware_overview/
- Firmware architecture: https://wiki.seeedstudio.com/watcher_firmware_architecture_main_page/

### Arduino Audio Libraries

- arduino-audio-driver (ES8311, ES7243e): https://github.com/pschatzmann/arduino-audio-driver
- arduino-audiokit: https://github.com/pschatzmann/arduino-audiokit
- Arduino SPD2010 touch driver: https://github.com/mathcampbell/SPD_2010T

### LovyanGFX QSPI Discussion

- QSPI support status: https://github.com/lovyan03/LovyanGFX/discussions/663

---

## Appendix: Quick Reference for Arduino Firmware

### Minimum pin set for display (QSPI)

```cpp
// QSPI bus on SPI3
#define LCD_PCLK  7
#define LCD_D0    9
#define LCD_D1    1
#define LCD_D2    14
#define LCD_D3    13
#define LCD_CS    45
#define LCD_RST   -1   // Not connected
#define LCD_BL    8    // Backlight, PWM, active HIGH

// Display resolution
#define LCD_WIDTH  412
#define LCD_HEIGHT 412
// Clock: 40 MHz (try 20 MHz if stability issues)
// SPI mode: 3
// cmd_bits: 32, param_bits: 8
```

### Minimum pin set for audio

```cpp
// I2S0 pins
#define I2S_MCLK  10
#define I2S_BCLK  11
#define I2S_WS    12   // LRCK
#define I2S_DOUT  16   // To ES8311 (speaker)
#define I2S_DIN   15   // From ES7243E (mic)

// I2C control bus for codecs
#define I2C_SDA   47
#define I2C_SCL   48

// Codec I2C addresses
#define ES8311_ADDR   0x30
#define ES7243E_ADDR  0x14
#define ES7243_ADDR   0x13   // Fallback

// Sample rate: 16000 Hz, 16-bit mono
```

### Touch controller

```cpp
// Separate I2C bus for touch
#define TOUCH_SDA  39
#define TOUCH_SCL  38
#define TOUCH_ADDR 0x53   // SPD2010 touch I2C address (fixed)
// Touch interrupt: IO Expander pin 5 (not a direct GPIO)
```

### IO Expander

```cpp
// PCA9535 / TCA9555 on General I2C bus
#define IO_EXP_INT  2    // Interrupt to ESP32-S3
// I2C SDA: GPIO 47, SCL: GPIO 48 (shared with codecs)
```
