/**
 * Sesame Robot Face + WiFi TCP Bridge + UART to Nano
 *
 * Based on yoyogochi display init code.
 * Displays a face on the SPD2010 round LCD.
 * Bridges WiFi TCP <-> UART to Arduino Nano.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "lwip/sockets.h"
#include "sensecap-watcher.h"
#include "driver/i2s_std.h"
#include "esp_codec_dev.h"
#include "driver/usb_serial_jtag.h"
#include "freertos/semphr.h"
#include <math.h>

static const char *TAG = "SesameRobot";

// ---- Camera (SSCMA / HX6538) ----
static sscma_client_handle_t sscma_client = NULL;
static uint8_t *cam_frame_buf = NULL;
static int cam_frame_size = 0;
static SemaphoreHandle_t cam_mutex = NULL;
static volatile bool cam_frame_ready = false;
static volatile bool cam_snap_requested = false;

// Eye micro-animation offsets (set by face_anim_task, read by draw_face)
static volatile int face_eye_dx = 0;
static volatile int face_eye_dy = 0;
#define CAM_PORT 8082
static int cam_server_fd = -1;
static int cam_client_fd = -1;

// Base64 decode table
static const uint8_t b64dec[256] = {
    64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,
    64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,
    64,64,64,64,64,64,64,64,64,64,64,62,64,64,64,63,
    52,53,54,55,56,57,58,59,60,61,64,64,64,64,64,64,
    64, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
    15,16,17,18,19,20,21,22,23,24,25,64,64,64,64,64,
    64,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
    41,42,43,44,45,46,47,48,49,50,51,64,64,64,64,64,
    64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,
    64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,
    64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,
    64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,
    64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,
    64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,
    64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,
    64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64
};

static int base64_decode(const char *input, int input_len, uint8_t *output) {
    int i, j = 0;
    for (i = 0; i < input_len;) {
        uint32_t a = input[i] == '=' ? 0 & i++ : b64dec[(uint8_t)input[i++]];
        uint32_t b = input[i] == '=' ? 0 & i++ : b64dec[(uint8_t)input[i++]];
        uint32_t c = input[i] == '=' ? 0 & i++ : b64dec[(uint8_t)input[i++]];
        uint32_t d = input[i] == '=' ? 0 & i++ : b64dec[(uint8_t)input[i++]];
        uint32_t triple = (a << 18) + (b << 12) + (c << 6) + d;
        if (j < input_len) output[j++] = (triple >> 16) & 0xFF;
        if (j < input_len) output[j++] = (triple >> 8) & 0xFF;
        if (j < input_len) output[j++] = triple & 0xFF;
    }
    if (input_len > 0 && input[input_len - 1] == '=') j--;
    if (input_len > 1 && input[input_len - 2] == '=') j--;
    return j;
}

// WiFi — multi-network fallback (tries in order, first match wins)
#define WIFI_NUM 5
static const char *wifi_ssids[WIFI_NUM] = {"octo-net", "TP-Link_C084", "Td5", "TD5", "Td5"};
static const char *wifi_passes[WIFI_NUM] = {"octoboss1234", "22236371", "farafir!", "farafir!", "farafir1!"};
#define TCP_PORT 8080
#define AUDIO_PORT 8081
static EventGroupHandle_t wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0

// UART to Nano
#define NANO_UART_NUM UART_NUM_2
#define NANO_UART_TX  GPIO_NUM_20
#define NANO_UART_RX  GPIO_NUM_19
#define NANO_UART_BAUD 9600

// Display
#define SCR_W 412
#define SCR_H 412

// Face state
typedef enum {
    FACE_IDLE = 0,
    FACE_HAPPY,
    FACE_TALK,
    FACE_SAD,
    FACE_ANGRY,
    FACE_EXCITED,
} face_state_t;

static face_state_t current_face = FACE_IDLE;
static lv_obj_t *left_eye = NULL;
static lv_obj_t *right_eye = NULL;
static lv_obj_t *left_lid = NULL;   // eyelid overlay for expression
static lv_obj_t *right_lid = NULL;
static lv_obj_t *mouth_obj = NULL;
static lv_obj_t *face_bg = NULL;
static bool talk_animating = false;

// TCP server
static int server_fd = -1;
static int client_fd = -1;

// Audio TCP server
static int audio_server_fd = -1;
static int audio_client_fd = -1;
static bool audio_recording = false;
static bool audio_playing = false;
static int32_t audio_play_remaining = -1;  // bytes left to play, -1 = unlimited

// Interrupt detection during playback
#define INTERRUPT_MIC_SAMPLES 256
#define INTERRUPT_THRESHOLD_MULT 5
#define INTERRUPT_MIN_BASELINE 2000     // minimum baseline — speaker feedback is loud
#define INTERRUPT_BASELINE_COUNT 50     // ~2.5s of calibration before detection activates
#define INTERRUPT_CONFIRM_COUNT 5       // consecutive loud readings needed
static int32_t playback_mic_baseline = 0;
static int playback_baseline_samples = 0;
static int interrupt_loud_count = 0;

// Forward declarations
static void phone_home(void);

// ---- WiFi ----
static int current_wifi_idx = 0;
static bool wifi_trying = false;

static void wifi_event_handler_multi(void *arg, esp_event_base_t base, int32_t id, void *data) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (wifi_trying) {
            // Try next network
            current_wifi_idx = (current_wifi_idx + 1) % WIFI_NUM;
            ESP_LOGI(TAG, "WiFi failed, trying %s...", wifi_ssids[current_wifi_idx]);
            wifi_config_t cfg = {};
            memcpy(cfg.sta.ssid, wifi_ssids[current_wifi_idx], strlen(wifi_ssids[current_wifi_idx]));
            memcpy(cfg.sta.password, wifi_passes[current_wifi_idx], strlen(wifi_passes[current_wifi_idx]));
            esp_wifi_set_config(WIFI_IF_STA, &cfg);
        }
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "Connected to %s, IP: " IPSTR, wifi_ssids[current_wifi_idx], IP2STR(&event->ip_info.ip));
        wifi_trying = false;
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static void wifi_init(void) {
    wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler_multi, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler_multi, NULL, NULL));

    // Start with first network
    wifi_trying = true;
    current_wifi_idx = 0;
    ESP_LOGI(TAG, "Trying WiFi: %s", wifi_ssids[current_wifi_idx]);

    wifi_config_t wifi_config = {};
    memcpy(wifi_config.sta.ssid, wifi_ssids[0], strlen(wifi_ssids[0]));
    memcpy(wifi_config.sta.password, wifi_passes[0], strlen(wifi_passes[0]));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, portMAX_DELAY);

    // Announce our IP to home PC
    phone_home();
}

// ---- Phone Home (announce IP to PC via Tailscale) ----
#define HOME_IP "100.92.93.7"
#define HOME_PORT 9999

static void phone_home(void) {
    // Get our IP
    esp_netif_ip_info_t ip_info;
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!netif || esp_netif_get_ip_info(netif, &ip_info) != ESP_OK) return;

    char my_ip[20];
    snprintf(my_ip, sizeof(my_ip), IPSTR, IP2STR(&ip_info.ip));

    // Phone home via TCP — more reliable than UDP through NAT/Tailscale
    // Try a few times (PC listener might not be up yet)
    for (int attempt = 0; attempt < 5; attempt++) {
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) continue;

        struct sockaddr_in dest = {
            .sin_family = AF_INET,
            .sin_port = htons(HOME_PORT),
        };
        inet_aton(HOME_IP, &dest.sin_addr);

        // Set connect timeout
        struct timeval tv = { .tv_sec = 3, .tv_usec = 0 };
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

        if (connect(sock, (struct sockaddr *)&dest, sizeof(dest)) == 0) {
            char msg[48];
            snprintf(msg, sizeof(msg), "OCTO %s\n", my_ip);
            send(sock, msg, strlen(msg), 0);
            close(sock);
            ESP_LOGW(TAG, "Phone home: sent IP %s to %s:%d (attempt %d)", my_ip, HOME_IP, HOME_PORT, attempt + 1);
            return;
        }
        close(sock);
        ESP_LOGW(TAG, "Phone home: attempt %d failed, retrying...", attempt + 1);
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
    ESP_LOGE(TAG, "Phone home: all attempts failed — PC may be unreachable");
}

// ---- UART to Nano ----
static void uart_nano_init(void) {
    // Disconnect USB PHY from GPIO 19/20
    // USB_SERIAL_JTAG peripheral has a pad enable bit that must be cleared
    // REG: USB_SERIAL_JTAG_CONF0_REG (0x60038018)
    // Bit 13: USB pad enable (dp_pullup)
    // Bit 14: USB pad enable
    // Clear bits to release GPIO 19/20 from USB PHY
    uint32_t *usb_conf0 = (uint32_t *)0x60038018;
    *usb_conf0 &= ~((1 << 13) | (1 << 14));

    // Also disable USB_SERIAL_JTAG clock
    // SYSTEM_PERIP_CLK_EN0_REG bit 24 = USB_DEVICE_CLK_EN
    // SYSTEM_PERIP_RST_EN0_REG bit 24 = USB_DEVICE_RST
    // uint32_t *clk_en = (uint32_t *)0x600C0018;
    // *clk_en &= ~(1 << 24);

    gpio_reset_pin(NANO_UART_TX);
    gpio_reset_pin(NANO_UART_RX);

    uart_config_t uart_config = {
        .baud_rate = NANO_UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    };
    ESP_ERROR_CHECK(uart_param_config(NANO_UART_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(NANO_UART_NUM, NANO_UART_TX, NANO_UART_RX, -1, -1));
    ESP_ERROR_CHECK(uart_driver_install(NANO_UART_NUM, 1024, 1024, 0, NULL, 0));
    ESP_LOGI(TAG, "UART to Nano ready (TX=%d, RX=%d)", NANO_UART_TX, NANO_UART_RX);
}

// ---- TCP Server ----
static void tcp_server_init(void) {
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(TCP_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    bind(server_fd, (struct sockaddr *)&addr, sizeof(addr));
    listen(server_fd, 1);

    // Non-blocking
    int flags = fcntl(server_fd, F_GETFL, 0);
    fcntl(server_fd, F_SETFL, flags | O_NONBLOCK);

    ESP_LOGI(TAG, "TCP server on port %d", TCP_PORT);
}

// ---- Audio Init ----
static void audio_init(void) {
    i2s_std_config_t i2s_config = BSP_I2S_DUPLEX_MONO_CFG(16000);
    bsp_audio_init(&i2s_config);
    bsp_codec_init();

    // Re-open mic with correct channel: RIGHT channel (channel_mask bit 1)
    esp_codec_dev_handle_t mic = bsp_codec_microphone_get();
    esp_codec_dev_handle_t spk = bsp_codec_speaker_get();

    // Close both first
    esp_codec_dev_close(spk);
    esp_codec_dev_close(mic);

    // Re-open speaker
    esp_codec_dev_sample_info_t spk_fs = {
        .bits_per_sample = 16,
        .channel = 1,
        .sample_rate = 16000,
    };
    esp_codec_dev_open(spk, &spk_fs);

    // Re-open mic on RIGHT channel
    esp_codec_dev_sample_info_t mic_fs = {
        .bits_per_sample = 16,
        .channel = 2,
        .channel_mask = ESP_CODEC_DEV_MAKE_CHANNEL_MASK(1),  // RIGHT channel
        .sample_rate = 16000,
    };
    esp_codec_dev_open(mic, &mic_fs);
    esp_codec_dev_set_in_gain(mic, 30.0);

    bsp_codec_volume_set(100, NULL);

    ESP_LOGI(TAG, "Audio ready (16kHz/16bit, mic=RIGHT channel)");
}

// ---- Audio TCP Server ----
static void audio_server_init(void) {
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(AUDIO_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    audio_server_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(audio_server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    bind(audio_server_fd, (struct sockaddr *)&addr, sizeof(addr));
    listen(audio_server_fd, 1);
    int flags = fcntl(audio_server_fd, F_GETFL, 0);
    fcntl(audio_server_fd, F_SETFL, flags | O_NONBLOCK);
    ESP_LOGI(TAG, "Audio TCP server on port %d", AUDIO_PORT);
}

// ---- Audio streaming task ----
// Protocol:
//   PC sends "record\n" -> Watcher streams raw PCM (16kHz 16bit mono) until PC sends "stop\n"
//   PC sends "play\n" then raw PCM data -> Watcher plays on speaker
//   PC sends "stop\n" -> stops record or play
static void audio_task(void *arg) {
    uint8_t *audio_buf = heap_caps_malloc(1024, MALLOC_CAP_DEFAULT);
    char cmd_buf[32];
    int cmd_pos = 0;

    while (1) {
        // Always try to accept new client (replace old one)
        {
            struct sockaddr_in ca;
            socklen_t al = sizeof(ca);
            int fd = accept(audio_server_fd, (struct sockaddr *)&ca, &al);
            if (fd >= 0) {
                if (audio_client_fd >= 0) close(audio_client_fd);
                audio_client_fd = fd;
                audio_recording = false;
                audio_playing = false;
                cmd_pos = 0;
                ESP_LOGI(TAG, "Audio client connected (reset state)");
            }
        }

        if (audio_client_fd >= 0) {
            if (audio_recording) {
                size_t bytes_read = 0;
                esp_err_t ret = bsp_i2s_read(audio_buf, 1024, &bytes_read, 50);
                if (ret == ESP_OK && bytes_read > 0) {
                    int sent = send(audio_client_fd, audio_buf, bytes_read, MSG_NOSIGNAL);
                    if (sent < 0) {
                        audio_recording = false;
                        close(audio_client_fd);
                        audio_client_fd = -1;
                    }
                }
                // Check for stop while recording
                uint8_t peek[8];
                int plen = recv(audio_client_fd, peek, sizeof(peek), MSG_DONTWAIT);
                if (plen > 0) {
                    peek[plen < 7 ? plen : 7] = '\0';
                    if (strstr((char*)peek, "stop") != NULL) {
                        audio_recording = false;
                        ESP_LOGI(TAG, "Recording stopped");
                    }
                }
            } else if (audio_playing) {
                // Stream audio directly from TCP to I2S
                // Length-prefixed mode: count bytes, auto-stop when done
                uint8_t tmp[1024];
                struct timeval tv = { .tv_sec = 0, .tv_usec = 500000 };
                setsockopt(audio_client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

                int max_recv = sizeof(tmp);
                if (audio_play_remaining >= 0) {
                    if (audio_play_remaining == 0) {
                        audio_playing = false;
                        audio_play_remaining = -1;
                        playback_baseline_samples = 0;
                        ESP_LOGI(TAG, "Playback complete");
                        continue;
                    }
                    if (audio_play_remaining < max_recv) {
                        max_recv = audio_play_remaining;
                    }
                }

                int len = recv(audio_client_fd, tmp, max_recv, 0);
                if (len > 0) {
                    size_t written = 0;
                    bsp_i2s_write(tmp, len, &written, portMAX_DELAY);
                    if (audio_play_remaining > 0) {
                        audio_play_remaining -= len;
                    }
                } else if (len == 0) {
                    close(audio_client_fd);
                    audio_client_fd = -1;
                    audio_playing = false;
                    playback_baseline_samples = 0;
                    audio_play_remaining = -1;
                }
            } else {
                // Idle: waiting for commands
                uint8_t tmp[64];
                int len = recv(audio_client_fd, tmp, sizeof(tmp), MSG_DONTWAIT);
                if (len > 0) {
                    for (int i = 0; i < len; i++) {
                        if (tmp[i] == '\n') {
                            cmd_buf[cmd_pos] = '\0';
                            if (strcmp(cmd_buf, "record") == 0) {
                                audio_recording = true;
                                audio_playing = false;
                                ESP_LOGI(TAG, "Recording started");
                            } else if (strncmp(cmd_buf, "play", 4) == 0) {
                                audio_playing = true;
                                audio_recording = false;
                                playback_baseline_samples = 0;
                                playback_mic_baseline = 0;
                                interrupt_loud_count = 0;
                                // Parse optional length: "play 48000"
                                if (cmd_buf[4] == ' ' && cmd_buf[5] != '\0') {
                                    audio_play_remaining = atoi(cmd_buf + 5);
                                    ESP_LOGI(TAG, "Playback started (%ld bytes)", audio_play_remaining);
                                } else {
                                    audio_play_remaining = -1;
                                    ESP_LOGI(TAG, "Playback started (unlimited)");
                                }
                                // Feed any remaining bytes after \n as audio data
                                int leftover = len - i - 1;
                                if (leftover > 0) {
                                    size_t written = 0;
                                    bsp_i2s_write(tmp + i + 1, leftover, &written, portMAX_DELAY);
                                    if (audio_play_remaining > 0) {
                                        audio_play_remaining -= leftover;
                                    }
                                }
                                cmd_pos = 0;
                                break;  // exit for loop — remaining bytes are audio, not commands
                            } else if (strcmp(cmd_buf, "stop") == 0) {
                                audio_recording = false;
                                audio_playing = false;
                            }
                            cmd_pos = 0;
                        } else if (cmd_pos < 31) {
                            cmd_buf[cmd_pos++] = tmp[i];
                        }
                    }
                } else if (len == 0) {
                    close(audio_client_fd);
                    audio_client_fd = -1;
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(5));
    }
    free(audio_buf);
}

// ---- Face Drawing with LVGL ----
// Half-circle eyes: use a tall rectangle with large radius, clip bottom half
// by placing it so only the top half is visible inside a container

static lv_obj_t *left_eye_clip = NULL;
static lv_obj_t *right_eye_clip = NULL;

static void draw_face(void) {
    lvgl_port_lock(0);

    // Background - black
    if (face_bg == NULL) {
        face_bg = lv_obj_create(lv_scr_act());
        lv_obj_set_size(face_bg, SCR_W, SCR_H);
        lv_obj_set_pos(face_bg, 0, 0);
        lv_obj_set_style_bg_color(face_bg, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(face_bg, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(face_bg, 0, 0);
        lv_obj_set_style_border_width(face_bg, 0, 0);
        lv_obj_set_style_pad_all(face_bg, 0, 0);
        lv_obj_clear_flag(face_bg, LV_OBJ_FLAG_SCROLLABLE);
    }

    int eye_w = 140;
    int eye_h = 70;   // half-circle clip height
    int eye_full_h = eye_w;
    int eye_y = SCR_H/2 - 40;
    int spacing = 160;
    lv_color_t eye_color = lv_color_white();
    int mouth_w = 60, mouth_curve = 20;
    int mouth_y_off = 100;
    bool show_mouth = false;
    bool eyes_bottom_half = false;  // false=top half (normal), true=bottom half (V angry)

    // Eyelid parameters
    int left_lid_angle = 0;
    int right_lid_angle = 0;
    int lid_h = 0;

    // Eye position offset for micro-animations (set by face_anim_task)
    int eye_dx = face_eye_dx;
    int eye_dy = face_eye_dy;

    switch (current_face) {
        case FACE_HAPPY:
            eye_w = 140;
            eye_h = 50;
            eye_full_h = eye_w;
            eye_y = SCR_H/2 - 30;
            eye_color = lv_color_make(0, 255, 65);     // electric neon green
            lid_h = 20;
            left_lid_angle = -50;
            right_lid_angle = 50;
            break;
        case FACE_SAD:
            eye_w = 120;
            eye_h = 50;
            eye_full_h = eye_w;
            eye_y = SCR_H/2 - 15;
            eye_color = lv_color_make(0, 100, 255);    // electric neon blue
            lid_h = 25;
            left_lid_angle = -120;
            right_lid_angle = 120;
            break;
        case FACE_ANGRY:
            eye_w = 140;
            eye_h = 55;
            eye_full_h = eye_w;
            eye_y = SCR_H/2 - 35;
            eye_color = lv_color_make(255, 0, 0);      // pure neon red
            eyes_bottom_half = true;  // V-shape: use bottom half, angled inward
            lid_h = 0;
            // Tilt eyes inward to form V
            left_lid_angle = 200;     // strong V tilt
            right_lid_angle = -200;
            lid_h = 28;
            break;
        case FACE_EXCITED:
            eye_w = 160;
            eye_h = 80;
            eye_full_h = eye_w;
            eye_y = SCR_H/2 - 50;
            eye_color = lv_color_make(255, 255, 0);    // pure neon yellow
            lid_h = 0;
            break;
        case FACE_TALK:
            eye_w = 140;
            eye_h = 60;
            eye_full_h = eye_w;
            eye_y = SCR_H/2 - 35;
            eye_color = lv_color_make(0, 255, 255);    // pure neon cyan
            lid_h = 0;
            break;
        default: // IDLE
            eye_w = 140;
            eye_h = 65;
            eye_full_h = eye_w;
            eye_y = SCR_H/2 - 35;
            eye_color = lv_color_make(180, 180, 255);  // cool lavender white
            lid_h = 10;
            left_lid_angle = -20;
            right_lid_angle = 20;
            break;
    }

    // Apply eye position offset
    eye_y += eye_dy;

    // Left eye clip container
    if (left_eye_clip == NULL) {
        left_eye_clip = lv_obj_create(face_bg);
        lv_obj_set_style_bg_opa(left_eye_clip, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(left_eye_clip, 0, 0);
        lv_obj_set_style_pad_all(left_eye_clip, 0, 0);
        lv_obj_set_style_clip_corner(left_eye_clip, true, 0);
        lv_obj_clear_flag(left_eye_clip, LV_OBJ_FLAG_SCROLLABLE);
    }
    lv_obj_set_size(left_eye_clip, eye_w, eye_h);
    // For angry: shift eyes closer together (V-shape inward)
    int left_x = SCR_W/2 - spacing/2 - eye_w/2 + eye_dx;
    int right_x = SCR_W/2 + spacing/2 - eye_w/2 + eye_dx;
    if (eyes_bottom_half) {
        left_x += 10;   // move slightly inward for V
        right_x -= 10;
    }
    lv_obj_set_pos(left_eye_clip, left_x, eye_y);

    // Left eye (full circle)
    if (left_eye == NULL) {
        left_eye = lv_obj_create(left_eye_clip);
        lv_obj_set_style_border_width(left_eye, 0, 0);
        lv_obj_clear_flag(left_eye, LV_OBJ_FLAG_SCROLLABLE);
    }
    lv_obj_set_size(left_eye, eye_w, eye_full_h);
    // Top half: circle at y=0 (bottom clipped). Bottom half: circle shifted up (top clipped)
    lv_obj_set_pos(left_eye, 0, eyes_bottom_half ? -(eye_full_h - eye_h) : 0);
    lv_obj_set_style_bg_color(left_eye, eye_color, 0);
    lv_obj_set_style_bg_opa(left_eye, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(left_eye, eye_w / 2, 0);

    // Right eye clip container
    if (right_eye_clip == NULL) {
        right_eye_clip = lv_obj_create(face_bg);
        lv_obj_set_style_bg_opa(right_eye_clip, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(right_eye_clip, 0, 0);
        lv_obj_set_style_pad_all(right_eye_clip, 0, 0);
        lv_obj_set_style_clip_corner(right_eye_clip, true, 0);
        lv_obj_clear_flag(right_eye_clip, LV_OBJ_FLAG_SCROLLABLE);
    }
    lv_obj_set_size(right_eye_clip, eye_w, eye_h);
    lv_obj_set_pos(right_eye_clip, right_x, eye_y);

    // Right eye
    if (right_eye == NULL) {
        right_eye = lv_obj_create(right_eye_clip);
        lv_obj_set_style_border_width(right_eye, 0, 0);
        lv_obj_clear_flag(right_eye, LV_OBJ_FLAG_SCROLLABLE);
    }
    lv_obj_set_size(right_eye, eye_w, eye_full_h);
    lv_obj_set_pos(right_eye, 0, eyes_bottom_half ? -(eye_full_h - eye_h) : 0);
    lv_obj_set_style_bg_color(right_eye, eye_color, 0);
    lv_obj_set_style_bg_opa(right_eye, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(right_eye, eye_w / 2, 0);

    // Eyelids — angled black overlays on top of eyes for expression
    if (left_lid == NULL) {
        left_lid = lv_obj_create(face_bg);
        lv_obj_set_style_border_width(left_lid, 0, 0);
        lv_obj_set_style_bg_color(left_lid, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(left_lid, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(left_lid, 4, 0);
        lv_obj_clear_flag(left_lid, LV_OBJ_FLAG_SCROLLABLE);
    }
    if (lid_h > 0) {
        lv_obj_set_size(left_lid, eye_w + 20, lid_h);
        lv_obj_set_pos(left_lid,
                       SCR_W/2 - spacing/2 - eye_w/2 - 10,
                       eye_y - lid_h/2);
        lv_obj_set_style_transform_angle(left_lid, left_lid_angle, 0);
        lv_obj_set_style_transform_pivot_x(left_lid, (eye_w + 20) / 2, 0);
        lv_obj_set_style_transform_pivot_y(left_lid, lid_h, 0);
        lv_obj_clear_flag(left_lid, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(left_lid, LV_OBJ_FLAG_HIDDEN);
    }

    if (right_lid == NULL) {
        right_lid = lv_obj_create(face_bg);
        lv_obj_set_style_border_width(right_lid, 0, 0);
        lv_obj_set_style_bg_color(right_lid, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(right_lid, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(right_lid, 4, 0);
        lv_obj_clear_flag(right_lid, LV_OBJ_FLAG_SCROLLABLE);
    }
    if (lid_h > 0) {
        lv_obj_set_size(right_lid, eye_w + 20, lid_h);
        lv_obj_set_pos(right_lid,
                       SCR_W/2 + spacing/2 - eye_w/2 - 10,
                       eye_y - lid_h/2);
        lv_obj_set_style_transform_angle(right_lid, right_lid_angle, 0);
        lv_obj_set_style_transform_pivot_x(right_lid, (eye_w + 20) / 2, 0);
        lv_obj_set_style_transform_pivot_y(right_lid, lid_h, 0);
        lv_obj_clear_flag(right_lid, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(right_lid, LV_OBJ_FLAG_HIDDEN);
    }

    // Mouth — simple curved line using a small obj
    if (mouth_obj == NULL) {
        mouth_obj = lv_obj_create(face_bg);
        lv_obj_set_style_border_width(mouth_obj, 0, 0);
        lv_obj_set_style_bg_color(mouth_obj, lv_color_white(), 0);
        lv_obj_set_style_bg_opa(mouth_obj, LV_OPA_80, 0);
        lv_obj_clear_flag(mouth_obj, LV_OBJ_FLAG_SCROLLABLE);
    }
    if (show_mouth) {
        int mh = (mouth_curve > 0) ? 8 + mouth_curve / 10 : 8 - mouth_curve / 10;
        if (mh < 6) mh = 6;
        if (mh > 30) mh = 30;
        lv_obj_set_size(mouth_obj, mouth_w, mh);
        lv_obj_set_pos(mouth_obj, SCR_W/2 - mouth_w/2, eye_y + mouth_y_off);
        lv_obj_set_style_radius(mouth_obj, mh / 2, 0);
        lv_obj_clear_flag(mouth_obj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(mouth_obj, LV_OBJ_FLAG_HIDDEN);
    }

    lvgl_port_unlock();
}

// ---- TCP + UART Bridge Task ----
static void bridge_task(void *arg) {
    char rx_buf[128];

    while (1) {
        // Always try to accept new client (replace old one)
        {
            struct sockaddr_in client_addr;
            socklen_t addr_len = sizeof(client_addr);
            int new_fd = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len);
            if (new_fd >= 0) {
                if (client_fd >= 0) close(client_fd);
                client_fd = new_fd;
                ESP_LOGI(TAG, "TCP client connected");
            }
        }

        // TCP -> UART (and face commands)
        if (client_fd >= 0) {
            int len = recv(client_fd, rx_buf, sizeof(rx_buf) - 1, MSG_DONTWAIT);
            if (len > 0) {
                rx_buf[len] = '\0';
                // Process each line
                char *saveptr = NULL;
                char *line = strtok_r(rx_buf, "\n", &saveptr);
                while (line) {
                    // Trim \r
                    char *cr = strchr(line, '\r');
                    if (cr) *cr = '\0';

                    if (strlen(line) > 0) {
                        if (strncmp(line, "face ", 5) == 0) {
                            char *face = line + 5;
                            if (strcmp(face, "idle") == 0) current_face = FACE_IDLE;
                            else if (strcmp(face, "happy") == 0) current_face = FACE_HAPPY;
                            else if (strcmp(face, "talk") == 0) current_face = FACE_TALK;
                            else if (strcmp(face, "sad") == 0) current_face = FACE_SAD;
                            else if (strcmp(face, "angry") == 0) current_face = FACE_ANGRY;
                            else if (strcmp(face, "excited") == 0) current_face = FACE_EXCITED;
                            draw_face();
                        } else {
                            ESP_LOGI(TAG, "FWD[%d]: '%s'", strlen(line), line);
                            uart_write_bytes(NANO_UART_NUM, line, strlen(line));
                            uart_write_bytes(NANO_UART_NUM, "\n", 1);
                        }
                    }
                    line = strtok_r(NULL, "\n", &saveptr);
                }
            } else if (len == 0) {
                close(client_fd);
                client_fd = -1;
                ESP_LOGI(TAG, "TCP client disconnected");
            }
            // len == -1 with EAGAIN = no data, just continue
        }

        // UART -> TCP
        int uart_len = 0;
        ESP_ERROR_CHECK(uart_get_buffered_data_len(NANO_UART_NUM, (size_t *)&uart_len));
        if (uart_len > 0) {
            int rlen = uart_read_bytes(NANO_UART_NUM, rx_buf, sizeof(rx_buf) - 1, 10 / portTICK_PERIOD_MS);
            if (rlen > 0 && client_fd >= 0) {
                send(client_fd, rx_buf, rlen, 0);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// ---- Blink + Talk + Eye movement animation task ----
static void face_anim_task(void *arg) {
    uint32_t next_blink = 2000 + (esp_random() % 4000);
    uint32_t next_eye_move = 1500 + (esp_random() % 3000);
    uint32_t tick = 0;
    bool mouth_open = false;

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(100));
        tick += 100;

        // Talk face: eyes glance around subtly while speaking
        if (current_face == FACE_TALK) {
            if (!talk_animating) {
                talk_animating = true;
                mouth_open = false;
            }

            // Random eye micro-movements during speech (every ~400ms)
            if (tick % 400 < 100) {
                int r = esp_random() % 100;
                if (r < 30) {
                    // Glance slightly to one side
                    face_eye_dx = -8 + (esp_random() % 17);  // -8 to +8
                    face_eye_dy = -5 + (esp_random() % 11);  // -5 to +5
                } else if (r < 50) {
                    // Look down slightly (thinking)
                    face_eye_dx = 0;
                    face_eye_dy = 3 + (esp_random() % 5);
                } else if (r < 60) {
                    // Center
                    face_eye_dx = 0;
                    face_eye_dy = 0;
                }
                draw_face();
            }
        } else if (talk_animating) {
            talk_animating = false;
            face_eye_dx = 0;
            face_eye_dy = 0;
            draw_face();
        }

        // Idle/other: occasional eye drift
        if (current_face != FACE_TALK && tick >= next_eye_move) {
            next_eye_move = tick + 2000 + (esp_random() % 4000);
            int r = esp_random() % 100;
            if (r < 40) {
                // Random glance
                face_eye_dx = -6 + (esp_random() % 13);
                face_eye_dy = -4 + (esp_random() % 9);
            } else {
                // Return to center
                face_eye_dx = 0;
                face_eye_dy = 0;
            }
            draw_face();
        }

        // Blink
        if (tick >= next_blink) {
            next_blink = tick + 2000 + (esp_random() % 4000);

            lvgl_port_lock(0);
            int w = (current_face == FACE_EXCITED) ? 160 :
                    (current_face == FACE_ANGRY) ? 140 :
                    (current_face == FACE_SAD) ? 120 : 140;
            lv_obj_set_size(left_eye_clip, w, 6);
            lv_obj_set_size(right_eye_clip, w, 6);
            lvgl_port_unlock();

            vTaskDelay(pdMS_TO_TICKS(120));
            draw_face();
        }
    }
}

// ---- Camera (SSCMA) callbacks ----
static void cam_on_event(sscma_client_handle_t client, const sscma_client_reply_t *reply, void *user_ctx) {
    char *image_b64 = NULL;
    int image_b64_size = 0;
    if (sscma_utils_fetch_image_from_reply(reply, &image_b64, &image_b64_size) == ESP_OK
        && image_b64 && image_b64_size > 0) {
        if (xSemaphoreTake(cam_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            // Store raw base64 string — decode on PC side for accuracy
            if (image_b64_size < 100 * 1024) {
                memcpy(cam_frame_buf, image_b64, image_b64_size);
                cam_frame_size = image_b64_size;
                cam_frame_ready = true;
            }
            xSemaphoreGive(cam_mutex);
        }
    }
}

static void cam_on_connect(sscma_client_handle_t client, const sscma_client_reply_t *reply, void *user_ctx) {
    ESP_LOGI(TAG, "SSCMA connected to HX6538");
}

// cam_init removed — all camera init happens inside cam_task

// Camera TCP server
static void cam_server_init(void) {
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(CAM_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    cam_server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (cam_server_fd < 0) { ESP_LOGE(TAG, "Camera socket failed"); return; }
    int opt = 1;
    setsockopt(cam_server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    if (bind(cam_server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "Camera bind failed"); close(cam_server_fd); cam_server_fd = -1; return;
    }
    listen(cam_server_fd, 1);
    int flags = fcntl(cam_server_fd, F_GETFL, 0);
    fcntl(cam_server_fd, F_SETFL, flags | O_NONBLOCK);
    ESP_LOGI(TAG, "Camera TCP server on port %d", CAM_PORT);
}

// Camera task — handles snap requests from TCP
static volatile bool cam_ready = false;

static void cam_task(void *arg) {
    vTaskDelay(pdMS_TO_TICKS(3000));

    // Do ALL camera init here — after display/wifi/audio are done
    cam_mutex = xSemaphoreCreateMutex();
    cam_frame_buf = heap_caps_malloc(100 * 1024, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!cam_frame_buf) cam_frame_buf = malloc(100 * 1024);
    if (!cam_frame_buf) {
        ESP_LOGE(TAG, "Camera: no frame buffer");
        vTaskDelete(NULL);
        return;
    }

    bsp_spi_bus_init();
    gpio_set_direction(GPIO_NUM_46, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_NUM_46, 1);

    sscma_client = bsp_sscma_client_init();
    if (!sscma_client) {
        ESP_LOGE(TAG, "Camera: SSCMA client failed");
        vTaskDelete(NULL);
        return;
    }

    // Init first (resets HX6538), then register callbacks
    sscma_client_init(sscma_client);

    const sscma_client_callback_t cbs = {
        .on_connect = cam_on_connect,
        .on_event = cam_on_event,
    };
    sscma_client_register_callback(sscma_client, &cbs, NULL);

    // Enable camera at 240x240 (reliable via SPI, 416x416 overflows RX buffer)
    sscma_client_set_sensor(sscma_client, 0, 0, true);
    vTaskDelay(pdMS_TO_TICKS(2000));
    ESP_LOGW(TAG, "Camera: enabled at 240x240");
    // On-demand invoke per snap request
    cam_ready = true;
    ESP_LOGW(TAG, "Camera ready — on-demand mode");

    char cmd_buf[32];
    int cmd_pos = 0;

    while (1) {
        // Accept camera TCP clients
        {
            struct sockaddr_in ca;
            socklen_t al = sizeof(ca);
            int fd = accept(cam_server_fd, (struct sockaddr *)&ca, &al);
            if (fd >= 0) {
                if (cam_client_fd >= 0) close(cam_client_fd);
                cam_client_fd = fd;
                ESP_LOGI(TAG, "Camera client connected");
            }
        }

        // Handle commands from camera client
        if (cam_client_fd >= 0) {
            uint8_t tmp[64];
            int len = recv(cam_client_fd, tmp, sizeof(tmp), MSG_DONTWAIT);
            if (len > 0) {
                for (int i = 0; i < len; i++) {
                    if (tmp[i] == '\n') {
                        cmd_buf[cmd_pos] = '\0';
                        if (strcmp(cmd_buf, "snap") == 0) {
                            if (!cam_ready) {
                                send(cam_client_fd, "ERROR not_ready\n", 16, 0);
                                cmd_pos = 0;
                                continue;
                            }

                            // Fresh single invoke
                            cam_frame_ready = false;
                            sscma_client_invoke(sscma_client, 1, false, true);

                            int wait = 0;
                            while (!cam_frame_ready && wait < 300) {
                                vTaskDelay(pdMS_TO_TICKS(10));
                                wait++;
                            }

                            if (cam_frame_ready && xSemaphoreTake(cam_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
                                // Send: "B64 <size>\n" then base64 string
                                char hdr[32];
                                int hdr_len = snprintf(hdr, sizeof(hdr), "B64 %d\n", cam_frame_size);
                                send(cam_client_fd, hdr, hdr_len, 0);
                                send(cam_client_fd, cam_frame_buf, cam_frame_size, 0);
                                ESP_LOGI(TAG, "Sent snapshot: %d bytes b64", cam_frame_size);
                                xSemaphoreGive(cam_mutex);
                            } else {
                                send(cam_client_fd, "ERROR timeout\n", 14, 0);
                            }
                        }
                        cmd_pos = 0;
                    } else if (cmd_pos < 31) {
                        cmd_buf[cmd_pos++] = tmp[i];
                    }
                }
            } else if (len == 0) {
                close(cam_client_fd);
                cam_client_fd = -1;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "Sesame Robot Starting...");

    // NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    // Hardware init
    bsp_i2c_bus_init();
    bsp_io_expander_init();
    vTaskDelay(pdMS_TO_TICKS(500));

    // Display
    bsp_lcd_brightness_set(100);
    bsp_lvgl_init();
    ESP_LOGI(TAG, "Display ready");

    // Draw face
    draw_face();

    // WiFi
    wifi_init();

    // Audio
    audio_init();

    // UART to Nano LAST
    uart_nano_init();

    // Remove GPIO test - just use UART

    // TCP servers
    tcp_server_init();
    audio_server_init();
    cam_server_init();

    // Start tasks
    xTaskCreate(bridge_task, "bridge", 4096, NULL, 5, NULL);
    xTaskCreate(audio_task, "audio", 4096, NULL, 5, NULL);
    xTaskCreate(face_anim_task, "face_anim", 4096, NULL, 3, NULL);
    xTaskCreate(cam_task, "camera", 8192, NULL, 5, NULL);

    ESP_LOGI(TAG, "Ready!");
}
