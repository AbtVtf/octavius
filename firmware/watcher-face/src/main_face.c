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

static const char *TAG = "SesameRobot";

// WiFi
#define WIFI_SSID "TP-Link_C084"
#define WIFI_PASS "22236371"
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
static lv_obj_t *mouth_obj = NULL;
static lv_obj_t *face_bg = NULL;

// TCP server
static int server_fd = -1;
static int client_fd = -1;

// Audio TCP server
static int audio_server_fd = -1;
static int audio_client_fd = -1;
static bool audio_recording = false;
static bool audio_playing = false;

// ---- WiFi ----
static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "IP: " IPSTR, IP2STR(&event->ip_info.ip));
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
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, portMAX_DELAY);
    ESP_LOGI(TAG, "WiFi connected");
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
                // Blocking recv — paces to I2S write speed naturally
                uint8_t tmp[512];
                struct timeval tv = { .tv_sec = 0, .tv_usec = 200000 }; // 200ms timeout
                setsockopt(audio_client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
                int len = recv(audio_client_fd, tmp, sizeof(tmp), 0);
                if (len > 0) {
                    if (len >= 4 && memcmp(tmp, "stop", 4) == 0) {
                        audio_playing = false;
                        ESP_LOGI(TAG, "Playback stopped");
                    } else {
                        size_t written = 0;
                        bsp_i2s_write(tmp, len, &written, 200);
                    }
                } else if (len == 0) {
                    close(audio_client_fd);
                    audio_client_fd = -1;
                    audio_playing = false;
                }
                // timeout (len == -1) is OK, just loop
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
                            } else if (strcmp(cmd_buf, "play") == 0) {
                                audio_playing = true;
                                audio_recording = false;
                                ESP_LOGI(TAG, "Playback started");
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

    int eye_w = 100;
    int eye_h = 50;   // half-circle clip height
    int eye_full_h = eye_w;
    int eye_y = SCR_H/2 - 30;
    int spacing = 130;
    lv_color_t eye_color = lv_color_white();

    switch (current_face) {
        case FACE_HAPPY:
            eye_h = 35;  // squinty happy eyes
            eye_y = SCR_H/2 - 20;
            break;
        case FACE_SAD:
            eye_w = 80;
            eye_h = 30;
            eye_full_h = eye_w;
            eye_y = SCR_H/2 - 10;
            break;
        case FACE_ANGRY:
            eye_w = 110;
            eye_h = 25;  // narrow angry slits
            eye_full_h = eye_w;
            eye_y = SCR_H/2 - 40;
            eye_color = lv_color_make(255, 80, 80);  // reddish
            break;
        case FACE_EXCITED:
            eye_w = 120;
            eye_h = 60;  // big round eyes
            eye_full_h = eye_w;
            eye_y = SCR_H/2 - 40;
            break;
        case FACE_TALK:
            eye_h = 40;
            break;
        default: // IDLE
            break;
    }

    // Left eye clip container (shows top half only)
    if (left_eye_clip == NULL) {
        left_eye_clip = lv_obj_create(face_bg);
        lv_obj_set_style_bg_opa(left_eye_clip, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(left_eye_clip, 0, 0);
        lv_obj_set_style_pad_all(left_eye_clip, 0, 0);
        lv_obj_set_style_clip_corner(left_eye_clip, true, 0);
        lv_obj_clear_flag(left_eye_clip, LV_OBJ_FLAG_SCROLLABLE);
    }
    lv_obj_set_size(left_eye_clip, eye_w, eye_h);
    lv_obj_set_pos(left_eye_clip, SCR_W/2 - spacing/2 - eye_w/2, eye_y);

    // Left eye (full circle, positioned so bottom half is clipped)
    if (left_eye == NULL) {
        left_eye = lv_obj_create(left_eye_clip);
        lv_obj_set_style_border_width(left_eye, 0, 0);
        lv_obj_clear_flag(left_eye, LV_OBJ_FLAG_SCROLLABLE);
    }
    lv_obj_set_size(left_eye, eye_w, eye_full_h);
    lv_obj_set_pos(left_eye, 0, 0);
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
    lv_obj_set_pos(right_eye_clip, SCR_W/2 + spacing/2 - eye_w/2, eye_y);

    // Right eye
    if (right_eye == NULL) {
        right_eye = lv_obj_create(right_eye_clip);
        lv_obj_set_style_border_width(right_eye, 0, 0);
        lv_obj_clear_flag(right_eye, LV_OBJ_FLAG_SCROLLABLE);
    }
    lv_obj_set_size(right_eye, eye_w, eye_full_h);
    lv_obj_set_pos(right_eye, 0, 0);
    lv_obj_set_style_bg_color(right_eye, eye_color, 0);
    lv_obj_set_style_bg_opa(right_eye, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(right_eye, eye_w / 2, 0);

    // No mouth

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

// ---- Blink task ----
static void blink_task(void *arg) {
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(2000 + (esp_random() % 4000)));

        // Close eyes
        lvgl_port_lock(0);
        int w = (current_face == FACE_EXCITED) ? 120 :
                (current_face == FACE_ANGRY) ? 110 :
                (current_face == FACE_SAD) ? 80 : 100;
        lv_obj_set_size(left_eye_clip, w, 6);
        lv_obj_set_size(right_eye_clip, w, 6);
        lvgl_port_unlock();

        vTaskDelay(pdMS_TO_TICKS(150));

        // Reopen — just redraw the face to get correct sizes
        draw_face();
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

    // Start tasks
    xTaskCreate(bridge_task, "bridge", 4096, NULL, 5, NULL);
    xTaskCreate(audio_task, "audio", 4096, NULL, 5, NULL);
    xTaskCreate(blink_task, "blink", 2048, NULL, 3, NULL);

    ESP_LOGI(TAG, "Ready!");
}
