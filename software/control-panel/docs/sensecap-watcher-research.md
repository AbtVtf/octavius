# SenseCAP Watcher ES7243E Microphone / Audio Recording: Comprehensive Research Document

## Executive Summary

The `memovai/mimiclaw` repository contains **no audio recording code whatsoever**. It is a text-only AI agent system (Telegram/Feishu chatbot on ESP32-S3) with no microphone, I2S, or codec drivers anywhere in its codebase.

The correct source for SenseCAP Watcher audio code is the `78/xiaozhi-esp32` project, which has a dedicated `main/boards/sensecap-watcher/` directory. That project implements a complete ES8311 (DAC/output) + ES7243E (ADC/microphone input) dual-codec audio pipeline using Espressif's `esp_codec_dev` framework over I2S in standard mode and I2C for codec control.

All code snippets below are verbatim from that repository and from the Espressif ADF `esp_codec_dev` component.

---

## Table of Contents

1. [Repository Findings: mimiclaw](#1-repository-findings-mimiclaw)
2. [Correct Source: xiaozhi-esp32 SenseCAP Watcher board](#2-correct-source-xiaozhi-esp32-sensecap-watcher-board)
3. [Hardware Configuration (Pin Assignments, Addresses)](#3-hardware-configuration-pin-assignments-addresses)
4. [Codec Initialization: Full Constructor](#4-codec-initialization-full-constructor)
5. [I2S Setup: CreateDuplexChannels](#5-i2s-setup-createduplexchannels)
6. [Enabling the Microphone: esp_codec_dev_open and Gain](#6-enabling-the-microphone-esp_codec_dev_open-and-gain)
7. [Reading Audio Data: esp_codec_dev_read](#7-reading-audio-data-esp_codec_dev_read)
8. [ES7243E Register Initialization Sequence](#8-es7243e-register-initialization-sequence)
9. [ES7243E Enable/Disable Sequence](#9-es7243e-enabledisable-sequence)
10. [ES7243E Gain Control](#10-es7243e-gain-control)
11. [Class Interface (Header)](#11-class-interface-header)
12. [Board Entrypoint: GetAudioCodec](#12-board-entrypoint-getaudiocodec)
13. [Key Design Notes and Pitfalls](#13-key-design-notes-and-pitfalls)
14. [Source Files](#14-source-files)

---

## 1. Repository Findings: mimiclaw

**Repository:** `https://github.com/memovai/mimiclaw/tree/main/main`

After exhaustively checking every directory and branch:

- The `main/CMakeLists.txt` `SRCS` list contains 24 C files. None relate to audio, I2S, codec, or microphone.
- The `idf_component.yml` only adds `espressif/esp_websocket_client`. No codec components.
- The `sdkconfig.defaults.esp32s3` has no `CONFIG_I2S_*`, `CONFIG_AUDIO_*`, or `CONFIG_ES72*` entries.
- A recursive tree walk across all 19 branches found zero files matching: audio, mic, i2s, codec, es72, record, capture, voice, sound, adc.
- The project is a pure text-based AI assistant (Telegram/Feishu bot). It has no voice or microphone support.

---

## 2. Correct Source: xiaozhi-esp32 SenseCAP Watcher board

**Repository:** `https://github.com/78/xiaozhi-esp32`
**Board directory:** `main/boards/sensecap-watcher/`

Key audio files:

| File | Purpose |
|------|---------|
| `config.h` | GPIO pin defines, I2C addresses, sample rates |
| `sensecap_audio_codec.h` | Class declaration |
| `sensecap_audio_codec.cc` | Full codec driver implementation |
| `sensecap_watcher.cc` | Board class, `GetAudioCodec()` factory |

The codec driver for the ES7243E comes from the Espressif ADF component at:
`esp-adf/components/esp_codec_dev/device/es7243e/es7243e.c`

---

## 3. Hardware Configuration (Pin Assignments, Addresses)

From `main/boards/sensecap-watcher/config.h`:

```c
/* General I2C (used for codec control) */
#define BSP_GENERAL_I2C_NUM   (I2C_NUM_0)
#define BSP_GENERAL_I2C_SDA   (GPIO_NUM_47)
#define BSP_GENERAL_I2C_SCL   (GPIO_NUM_48)

/* Audio sample rates */
#define AUDIO_INPUT_SAMPLE_RATE    24000   // 24 kHz microphone input
#define AUDIO_OUTPUT_SAMPLE_RATE   24000   // 24 kHz speaker output
#define AUDIO_INPUT_REFERENCE      false   // no echo cancellation reference channel

/* I2S GPIO pins */
#define AUDIO_I2S_GPIO_MCLK   GPIO_NUM_10  // Master clock
#define AUDIO_I2S_GPIO_WS     GPIO_NUM_12  // Word select (LR clock)
#define AUDIO_I2S_GPIO_BCLK   GPIO_NUM_11  // Bit clock
#define AUDIO_I2S_GPIO_DIN    GPIO_NUM_15  // Data IN  (microphone -> ESP32)
#define AUDIO_I2S_GPIO_DOUT   GPIO_NUM_16  // Data OUT (ESP32 -> speaker)
#define AUDIO_CODEC_PA_PIN    GPIO_NUM_NC  // Power amp: not directly wired (via IO expander)

/* Codec I2C addresses */
#define AUDIO_CODEC_ES8311_ADDR    ES8311_CODEC_DEFAULT_ADDR  // 0x18 (output DAC)
#define AUDIO_CODEC_ES7243E_ADDR   (0x14)                     // 0x14 (input ADC/microphone)
```

The PA (power amplifier for the speaker) is controlled through an IO expander pin `BSP_PWR_CODEC_PA` (`IO_EXPANDER_PIN_NUM_12`), not directly via GPIO, which is why `AUDIO_CODEC_PA_PIN` is `GPIO_NUM_NC`.

---

## 4. Codec Initialization: Full Constructor

From `main/boards/sensecap-watcher/sensecap_audio_codec.cc`:

```cpp
SensecapAudioCodec::SensecapAudioCodec(
    void* i2c_master_handle,
    int input_sample_rate,
    int output_sample_rate,
    gpio_num_t mclk, gpio_num_t bclk, gpio_num_t ws,
    gpio_num_t dout, gpio_num_t din,
    gpio_num_t pa_pin,
    uint8_t es8311_addr,
    uint8_t es7243e_addr,
    bool input_reference)
{
    duplex_           = true;
    input_reference_  = input_reference;
    input_channels_   = input_reference_ ? 2 : 1;
    input_sample_rate_  = input_sample_rate;
    output_sample_rate_ = output_sample_rate;

    // Step 1: Initialize I2S duplex (both TX for speaker and RX for mic)
    CreateDuplexChannels(mclk, bclk, ws, dout, din);

    // Step 2: Create shared I2S data interface
    audio_codec_i2s_cfg_t i2s_cfg = {
        .port      = I2S_NUM_0,
        .rx_handle = rx_handle_,
        .tx_handle = tx_handle_,
    };
    data_if_ = audio_codec_new_i2s_data(&i2s_cfg);
    assert(data_if_ != NULL);

    // Step 3: Set up ES8311 (output/speaker side)
    audio_codec_i2c_cfg_t i2c_cfg = {
        .port       = (i2c_port_t)0,
        .addr       = es8311_addr,
        .bus_handle = i2c_master_handle,
    };
    out_ctrl_if_ = audio_codec_new_i2c_ctrl(&i2c_cfg);
    assert(out_ctrl_if_ != NULL);

    gpio_if_ = audio_codec_new_gpio();
    assert(gpio_if_ != NULL);

    es8311_codec_cfg_t es8311_cfg = {};
    es8311_cfg.ctrl_if              = out_ctrl_if_;
    es8311_cfg.gpio_if              = gpio_if_;
    es8311_cfg.codec_mode           = ESP_CODEC_DEV_WORK_MODE_DAC;
    es8311_cfg.pa_pin               = pa_pin;
    es8311_cfg.use_mclk             = true;
    es8311_cfg.hw_gain.pa_voltage   = 5.0;
    es8311_cfg.hw_gain.codec_dac_voltage = 3.3;
    out_codec_if_ = es8311_codec_new(&es8311_cfg);
    assert(out_codec_if_ != NULL);

    esp_codec_dev_cfg_t dev_cfg = {
        .dev_type  = ESP_CODEC_DEV_TYPE_OUT,
        .codec_if  = out_codec_if_,
        .data_if   = data_if_,
    };
    output_dev_ = esp_codec_dev_new(&dev_cfg);
    assert(output_dev_ != NULL);

    // Step 4: Set up ES7243E (input/microphone side)
    // IMPORTANT: address is left-shifted by 1 here
    i2c_cfg.addr = es7243e_addr << 1;
    in_ctrl_if_ = audio_codec_new_i2c_ctrl(&i2c_cfg);
    assert(in_ctrl_if_ != NULL);

    es7243e_codec_cfg_t es7243e_cfg = {};
    es7243e_cfg.ctrl_if = in_ctrl_if_;
    in_codec_if_ = es7243e_codec_new(&es7243e_cfg);
    assert(in_codec_if_ != NULL);

    dev_cfg.dev_type  = ESP_CODEC_DEV_TYPE_IN;
    dev_cfg.codec_if  = in_codec_if_;
    input_dev_ = esp_codec_dev_new(&dev_cfg);
    assert(input_dev_ != NULL);

    // Step 5: Keep codecs alive even when their dev is "closed"
    esp_codec_set_disable_when_closed(output_dev_, false);
    esp_codec_set_disable_when_closed(input_dev_, false);

    ESP_LOGI(TAG, "SensecapAudioDevice initialized");
}
```

**Critical detail:** The ES7243E I2C address `0x14` is shifted left by 1 (`es7243e_addr << 1`) before being passed to `audio_codec_new_i2c_ctrl`. This is because the `esp_codec_dev` I2C control layer expects a 7-bit address in the upper 7 bits (i.e., the 8-bit write address). Using the raw `0x14` without the shift will cause all I2C transactions to the ES7243E to fail silently, producing only silence.

---

## 5. I2S Setup: CreateDuplexChannels

From `sensecap_audio_codec.cc`:

```cpp
void SensecapAudioCodec::CreateDuplexChannels(
    gpio_num_t mclk, gpio_num_t bclk, gpio_num_t ws,
    gpio_num_t dout, gpio_num_t din)
{
    // Input and output sample rates must match for duplex
    assert(input_sample_rate_ == output_sample_rate_);

    // Create a single I2S channel pair (TX + RX) on I2S_NUM_0
    i2s_chan_config_t chan_cfg = {
        .id                  = I2S_NUM_0,
        .role                = I2S_ROLE_MASTER,       // ESP32-S3 is the I2S master
        .dma_desc_num        = AUDIO_CODEC_DMA_DESC_NUM,
        .dma_frame_num       = AUDIO_CODEC_DMA_FRAME_NUM,
        .auto_clear_after_cb = true,
        .auto_clear_before_cb = false,
        .intr_priority       = 0,
    };
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_handle_, &rx_handle_));

    // Configure I2S standard mode (Philips/I2S format)
    i2s_std_config_t std_cfg = {
        .clk_cfg = {
            .sample_rate_hz = (uint32_t)output_sample_rate_, // 24000 Hz
            .clk_src        = I2S_CLK_SRC_DEFAULT,
            .ext_clk_freq_hz = 0,
            .mclk_multiple  = I2S_MCLK_MULTIPLE_256,  // MCLK = 24000 * 256 = 6.144 MHz
        },
        .slot_cfg = {
            .data_bit_width  = I2S_DATA_BIT_WIDTH_16BIT,
            .slot_bit_width  = I2S_SLOT_BIT_WIDTH_AUTO,
            .slot_mode       = I2S_SLOT_MODE_MONO,
            .slot_mask       = I2S_STD_SLOT_BOTH,     // TX: use both slots
            .ws_width        = I2S_DATA_BIT_WIDTH_16BIT,
            .ws_pol          = false,
            .bit_shift       = true,                   // Standard I2S: 1-bit shift
            .left_align      = true,
            .big_endian      = false,
            .bit_order_lsb   = false,
        },
        .gpio_cfg = {
            .mclk = mclk,   // GPIO_NUM_10
            .bclk = bclk,   // GPIO_NUM_11
            .ws   = ws,     // GPIO_NUM_12
            .dout = dout,   // GPIO_NUM_16
            .din  = din,    // GPIO_NUM_15
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };

    // Initialize TX channel (speaker output)
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_handle_, &std_cfg));

    // Initialize RX channel (microphone input) - use RIGHT slot only
    std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_RIGHT;
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_handle_, &std_cfg));

    // Enable both channels
    ESP_ERROR_CHECK(i2s_channel_enable(tx_handle_));
    ESP_ERROR_CHECK(i2s_channel_enable(rx_handle_));

    ESP_LOGI(TAG, "Duplex channels created");
}
```

**Key settings summary:**

| Parameter | Value | Notes |
|-----------|-------|-------|
| Sample rate | 24000 Hz | Both input and output must match |
| MCLK multiplier | 256x | MCLK = 6.144 MHz |
| Bit width | 16-bit | Both data and slot |
| Slot mode | Mono | Single channel per direction |
| TX slot mask | `I2S_STD_SLOT_BOTH` | Speaker |
| RX slot mask | `I2S_STD_SLOT_RIGHT` | Microphone reads RIGHT channel |
| Bit shift | true | Standard I2S Philips format |
| Role | Master | ESP32-S3 generates all clocks |

---

## 6. Enabling the Microphone: esp_codec_dev_open and Gain

From `sensecap_audio_codec.cc`:

```cpp
void SensecapAudioCodec::EnableInput(bool enable) {
    if (enable == input_enabled_) {
        return;
    }
    if (enable) {
        esp_codec_dev_sample_info_t fs = {
            .bits_per_sample = 16,
            .channel         = 2,                                    // 2 channels in the frame...
            .channel_mask    = ESP_CODEC_DEV_MAKE_CHANNEL_MASK(1),   // ...but only channel 1 active
            .sample_rate     = (uint32_t)output_sample_rate_,        // 24000
            .mclk_multiple   = 0,
        };
        ESP_ERROR_CHECK(esp_codec_dev_open(input_dev_, &fs));

        // Set microphone PGA gain to 27 dB
        ESP_ERROR_CHECK(esp_codec_dev_set_in_gain(input_dev_, 27.0));
    } else {
        ESP_ERROR_CHECK(esp_codec_dev_close(input_dev_));
    }
    AudioCodec::EnableInput(enable);
}
```

**Why `channel = 2` with mask = channel 1 only:** The ES7243E outputs a stereo I2S frame even for mono capture. The `channel_mask` selects which physical channel carries real microphone data. Using `ESP_CODEC_DEV_MAKE_CHANNEL_MASK(1)` picks channel index 1 (right channel), matching the `I2S_STD_SLOT_RIGHT` RX slot mask configured in `CreateDuplexChannels`.

**Gain setting:** `esp_codec_dev_set_in_gain(input_dev_, 27.0)` applies 27 dB of PGA gain. This is the value that makes the mic capture audible audio rather than near-silence. Without explicit gain setting, the ES7243E defaults to a very low gain and audio will be extremely quiet.

---

## 7. Reading Audio Data: esp_codec_dev_read

From `sensecap_audio_codec.cc`:

```cpp
int SensecapAudioCodec::Read(int16_t* dest, int samples) {
    if (input_enabled_) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(
            esp_codec_dev_read(input_dev_, (void*)dest, samples * sizeof(int16_t))
        );
    }
    return samples;
}
```

- `dest`: buffer of `int16_t` samples
- `samples`: number of samples to read
- Byte count passed to `esp_codec_dev_read` is `samples * 2` (16-bit = 2 bytes per sample)
- Uses `ESP_ERROR_CHECK_WITHOUT_ABORT` so a read error logs but does not crash

---

## 8. ES7243E Register Initialization Sequence

This is the exact register write sequence executed by `es7243e_open()` inside `esp_codec_dev` when `es7243e_codec_new()` is called. Source: `esp-adf/.../es7243e.c`:

```c
// Phase 1: Reset and initial power-up
es7243e_write_reg(codec, 0x01, 0x3A);  // Power up clock
es7243e_write_reg(codec, 0x00, 0x80);  // System reset
es7243e_write_reg(codec, 0xF9, 0x00);  // Enable reg page 0
es7243e_write_reg(codec, 0x04, 0x02);  // Clock control
es7243e_write_reg(codec, 0x04, 0x01);
es7243e_write_reg(codec, 0xF9, 0x01);

// Phase 2: Configure system
es7243e_write_reg(codec, 0x00, 0x1E);
es7243e_write_reg(codec, 0x01, 0x00);
es7243e_write_reg(codec, 0x02, 0x00);
es7243e_write_reg(codec, 0x03, 0x20);
es7243e_write_reg(codec, 0x04, 0x01);
es7243e_write_reg(codec, 0x0D, 0x00);

// Phase 3: Clock dividers
es7243e_write_reg(codec, 0x05, 0x00);
es7243e_write_reg(codec, 0x06, 0x03);  // SCLK = MCLK/4
es7243e_write_reg(codec, 0x07, 0x00);  // LRCK = MCLK/256
es7243e_write_reg(codec, 0x08, 0xFF);  // LRCK = MCLK/256

// Phase 4: Audio format / serial interface
es7243e_write_reg(codec, 0x09, 0xCA);
es7243e_write_reg(codec, 0x0A, 0x85);
es7243e_write_reg(codec, 0x0B, 0x00);  // Unmuted

// Phase 5: ADC/analog configuration
es7243e_write_reg(codec, 0x0E, 0xBF);
es7243e_write_reg(codec, 0x0F, 0x80);
es7243e_write_reg(codec, 0x14, 0x0C);
es7243e_write_reg(codec, 0x15, 0x0C);
es7243e_write_reg(codec, 0x17, 0x02);
es7243e_write_reg(codec, 0x18, 0x26);
es7243e_write_reg(codec, 0x19, 0x77);
es7243e_write_reg(codec, 0x1A, 0xF4);
es7243e_write_reg(codec, 0x1B, 0x66);
es7243e_write_reg(codec, 0x1C, 0x44);
es7243e_write_reg(codec, 0x1E, 0x00);
es7243e_write_reg(codec, 0x1F, 0x0C);

// Phase 6: PGA gain - +30 dB default at init
es7243e_write_reg(codec, 0x20, 0x1A);  // Left channel:  PGA = +30 dB
es7243e_write_reg(codec, 0x21, 0x1A);  // Right channel: PGA = +30 dB

// Phase 7: Enable slave mode and start ADC
es7243e_write_reg(codec, 0x00, 0x80);  // Slave mode
es7243e_write_reg(codec, 0x01, 0x3A);
es7243e_write_reg(codec, 0x16, 0x3F);  // Mute (temporary)
es7243e_write_reg(codec, 0x16, 0x00);  // Unmute -> ADC active

// Then calls es7243e_adc_enable(codec, true) for final activation
```

Then `es7243e_adc_enable(true)` is called:

```c
// Enable sequence (called at end of open, and by enable())
es7243e_write_reg(codec, 0xF9, 0x00);
es7243e_write_reg(codec, 0x04, 0x01);
es7243e_write_reg(codec, 0x17, 0x01);
es7243e_write_reg(codec, 0x20, 0x10);  // PGA gain register (re-applied)
es7243e_write_reg(codec, 0x21, 0x10);
es7243e_write_reg(codec, 0x00, 0x80);  // Slave mode
es7243e_write_reg(codec, 0x01, 0x3A);
es7243e_write_reg(codec, 0x16, 0x3F);  // Mute
es7243e_write_reg(codec, 0x16, 0x00);  // Unmute -> capturing
```

---

## 9. ES7243E Enable/Disable Sequence

**Disable sequence** (mutes and powers down the ADC):

```c
es7243e_write_reg(codec, 0x04, 0x02);
es7243e_write_reg(codec, 0x04, 0x01);
es7243e_write_reg(codec, 0xF7, 0x30);
es7243e_write_reg(codec, 0xF9, 0x01);
es7243e_write_reg(codec, 0x16, 0xFF);  // Full mute
es7243e_write_reg(codec, 0x17, 0x00);
es7243e_write_reg(codec, 0x01, 0x38);
es7243e_write_reg(codec, 0x20, 0x00);
es7243e_write_reg(codec, 0x21, 0x00);
es7243e_write_reg(codec, 0x00, 0x00);
es7243e_write_reg(codec, 0x00, 0x1E);
es7243e_write_reg(codec, 0x01, 0x30);
es7243e_write_reg(codec, 0x01, 0x00);
```

**Mute/unmute** (for use during operation):

```c
// Mute:
es7243e_write_reg(codec, 0x0B, 0xC0);

// Unmute:
es7243e_write_reg(codec, 0x0B, 0x00);
```

---

## 10. ES7243E Gain Control

The `es7243e_set_gain()` function, called when `esp_codec_dev_set_in_gain()` is invoked:

```c
static uint8_t get_db_reg(float db) {
    // Converts dB value to ES7243E PGA register value
    // Register 0x20/0x21: upper nibble 0x10 = enable, lower nibble = gain step
    // Each step = 3 dB. Range: 0-33 dB (steps 0-11), then 34.5/36/37.5 dB
    db += 0.5;
    if (db <= 33.0) {
        return (uint8_t) db / 3;
    }
    if (db < 36.0) { return 12; }
    if (db < 37.0) { return 13; }
    return 14;
}

static int es7243e_set_gain(const audio_codec_if_t *h, float db_value) {
    uint8_t reg = get_db_reg(db_value);
    ret |= es7243e_write_reg(codec, 0x20, 0x10 | reg);  // Left channel
    ret |= es7243e_write_reg(codec, 0x21, 0x10 | reg);  // Right channel
    return ret;
}
```

**Gain table for register values:**

| `esp_codec_dev_set_in_gain()` call | Register value | PGA gain |
|-------------------------------------|----------------|----------|
| `27.0` (used in watcher) | `0x19` | +27 dB |
| `30.0` (default at init) | `0x1A` | +30 dB |
| `0.0` | `0x10` | 0 dB |
| `3.0` | `0x11` | +3 dB |
| `33.0` | `0x1B` | +33 dB |
| `34.5` | `0x1C` | +34.5 dB |

The SenseCAP Watcher overrides the init default of 30 dB with 27 dB via `esp_codec_dev_set_in_gain(input_dev_, 27.0)` in `EnableInput()`.

---

## 11. Class Interface (Header)

From `main/boards/sensecap-watcher/sensecap_audio_codec.h`:

```cpp
#ifndef _SENSECAP_AUDIO_CODEC_H
#define _SENSECAP_AUDIO_CODEC_H

#include "audio_codec.h"
#include <esp_codec_dev.h>
#include <esp_codec_dev_defaults.h>

class SensecapAudioCodec : public AudioCodec {
private:
    const audio_codec_data_if_t*  data_if_      = nullptr;
    const audio_codec_ctrl_if_t*  out_ctrl_if_  = nullptr;
    const audio_codec_if_t*       out_codec_if_ = nullptr;
    const audio_codec_ctrl_if_t*  in_ctrl_if_   = nullptr;
    const audio_codec_if_t*       in_codec_if_  = nullptr;
    const audio_codec_gpio_if_t*  gpio_if_      = nullptr;

    esp_codec_dev_handle_t output_dev_ = nullptr;
    esp_codec_dev_handle_t input_dev_  = nullptr;
    gpio_num_t pa_pin_ = GPIO_NUM_NC;

    void CreateDuplexChannels(gpio_num_t mclk, gpio_num_t bclk, gpio_num_t ws,
                               gpio_num_t dout, gpio_num_t din);
    virtual int Read(int16_t* dest, int samples) override;
    virtual int Write(const int16_t* data, int samples) override;

public:
    SensecapAudioCodec(void* i2c_master_handle,
                       int input_sample_rate, int output_sample_rate,
                       gpio_num_t mclk, gpio_num_t bclk, gpio_num_t ws,
                       gpio_num_t dout, gpio_num_t din,
                       gpio_num_t pa_pin,
                       uint8_t es8311_addr,
                       uint8_t es7210_addr,    // note: parameter name says es7210 but is ES7243E
                       bool input_reference);
    virtual ~SensecapAudioCodec();

    virtual void SetOutputVolume(int volume) override;
    virtual void EnableInput(bool enable) override;
    virtual void EnableOutput(bool enable) override;
};

#endif
```

Note: The constructor parameter is named `es7210_addr` (likely a copy-paste from another board), but it is used as the ES7243E address throughout the `.cc` file.

---

## 12. Board Entrypoint: GetAudioCodec

From `main/boards/sensecap-watcher/sensecap_watcher.cc`:

```cpp
virtual AudioCodec* GetAudioCodec() override {
    static SensecapAudioCodec audio_codec(
        i2c_bus_,
        AUDIO_INPUT_SAMPLE_RATE,        // 24000
        AUDIO_OUTPUT_SAMPLE_RATE,       // 24000
        AUDIO_I2S_GPIO_MCLK,            // GPIO_NUM_10
        AUDIO_I2S_GPIO_BCLK,            // GPIO_NUM_11
        AUDIO_I2S_GPIO_WS,              // GPIO_NUM_12
        AUDIO_I2S_GPIO_DOUT,            // GPIO_NUM_16
        AUDIO_I2S_GPIO_DIN,             // GPIO_NUM_15
        AUDIO_CODEC_PA_PIN,             // GPIO_NUM_NC
        AUDIO_CODEC_ES8311_ADDR,        // ES8311_CODEC_DEFAULT_ADDR (0x18)
        AUDIO_CODEC_ES7243E_ADDR,       // 0x14
        AUDIO_INPUT_REFERENCE);         // false
    return &audio_codec;
}
```

This is a static local — the codec is initialized once on first call and reused thereafter.

---

## 13. Key Design Notes and Pitfalls

### I2C Address Left-Shift
The ES7243E address `0x14` is shifted left by 1 (`<< 1`) before passing to `audio_codec_new_i2c_ctrl`. If you use the raw `0x14` without the shift, all I2C register writes fail and the mic produces only silence.

```cpp
// CORRECT:
i2c_cfg.addr = es7243e_addr << 1;  // 0x14 << 1 = 0x28

// WRONG (silence):
i2c_cfg.addr = es7243e_addr;       // 0x14
```

### RX Slot Must Be RIGHT, Not BOTH
The TX (speaker output) uses `I2S_STD_SLOT_BOTH` but the RX (mic input) is reconfigured to `I2S_STD_SLOT_RIGHT`. Using `BOTH` for RX causes interleaved stereo data that doesn't match the mono mic signal and produces garbled audio.

### `esp_codec_set_disable_when_closed(dev, false)`
Both `output_dev_` and `input_dev_` are configured with `disable_when_closed = false`. This means closing one device does not power down the shared I2S/codec hardware. Without this, closing the output (e.g., when not playing audio) would also kill the microphone clock, stopping recording.

### Gain Must Be Set After `esp_codec_dev_open`
`esp_codec_dev_set_in_gain()` must be called after `esp_codec_dev_open()`. Calling it before `open` returns `ESP_CODEC_DEV_WRONG_STATE` and the gain is not applied, leaving the mic at the codec's init-time 30 dB default.

### Sample Rates Must Match for Duplex
The `CreateDuplexChannels` function asserts `input_sample_rate_ == output_sample_rate_`. Using different rates requires separate I2S channel pairs.

### Power Amp Is IO-Expander Controlled
The SenseCAP Watcher's speaker PA is on `BSP_PWR_CODEC_PA` (IO expander pin 12), not a GPIO. `AUDIO_CODEC_PA_PIN` is `GPIO_NUM_NC`. The PA is powered in `InitializeExpander()` as part of `BSP_PWR_START_UP`.

### MCLK Frequency
At 24 kHz sample rate with 256x multiplier: MCLK = 24000 * 256 = 6.144 MHz. The ES7243E requires MCLK to be a supported multiple of its sample rate; 256x is in the supported list.

---

## 14. Source Files

All code is from these exact public URLs (MIT licensed):

- `sensecap_audio_codec.cc`: https://raw.githubusercontent.com/78/xiaozhi-esp32/main/main/boards/sensecap-watcher/sensecap_audio_codec.cc
- `sensecap_audio_codec.h`: https://raw.githubusercontent.com/78/xiaozhi-esp32/main/main/boards/sensecap-watcher/sensecap_audio_codec.h
- `sensecap_watcher.cc`: https://raw.githubusercontent.com/78/xiaozhi-esp32/main/main/boards/sensecap-watcher/sensecap_watcher.cc
- `config.h`: https://raw.githubusercontent.com/78/xiaozhi-esp32/main/main/boards/sensecap-watcher/config.h
- `es7243e.c` (Espressif ADF): https://raw.githubusercontent.com/espressif/esp-adf/release/v2.x/components/esp_codec_dev/device/es7243e/es7243e.c
- `mimiclaw/main` (reference, no audio code): https://github.com/memovai/mimiclaw/tree/main/main

Sources:
- [GitHub - memovai/mimiclaw](https://github.com/memovai/mimiclaw)
- [GitHub - 78/xiaozhi-esp32](https://github.com/78/xiaozhi-esp32)
- [SenseCAP Watcher board directory](https://github.com/78/xiaozhi-esp32/tree/main/main/boards/sensecap-watcher)
- [ES7243E - ESPHome documentation](https://esphome.io/components/audio_adc/es7243e/)
- [espressif/esp_codec_dev on ESP Component Registry](https://components.espressif.com/components/espressif/esp_codec_dev)
- [SenseCAP Watcher Hardware Overview](https://wiki.seeedstudio.com/watcher_hardware_overview/)
