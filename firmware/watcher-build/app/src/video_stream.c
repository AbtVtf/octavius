/**
 * Video Stream App
 *
 * Streams video from the SenseCAP Watcher's camera to a local machine via HTTP.
 *
 * Endpoints:
 *   - GET /stream   - MJPEG video stream
 *   - GET /snapshot - Single JPEG frame
 *   - GET /         - Simple status page
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "driver/uart.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_timer.h"
#include "esp_http_server.h"
#include "nvs_flash.h"
#include "sensecap-watcher.h"
#include "wifi_config.h"

static const char *TAG = "VideoStream";

// WiFi event group
static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

// SSCMA client handle
static sscma_client_handle_t sscma_client = NULL;

// Frame buffer (stores latest JPEG frame)
static uint8_t *frame_buffer = NULL;
static int frame_size = 0;
static SemaphoreHandle_t frame_mutex = NULL;
static bool frame_ready = false;

// Base64 decoding table
static const uint8_t base64_decode_table[256] = {
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 62, 64, 64, 64, 63,
    52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 64, 64, 64, 64, 64, 64,
    64,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
    15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 64, 64, 64, 64, 64,
    64, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
    41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64
};

// Decode base64 to binary
static int base64_decode(const char *input, int input_len, uint8_t *output) {
    int i, j;
    uint32_t sextet_a, sextet_b, sextet_c, sextet_d, triple;
    int output_len = 0;

    for (i = 0, j = 0; i < input_len;) {
        sextet_a = input[i] == '=' ? 0 & i++ : base64_decode_table[(uint8_t)input[i++]];
        sextet_b = input[i] == '=' ? 0 & i++ : base64_decode_table[(uint8_t)input[i++]];
        sextet_c = input[i] == '=' ? 0 & i++ : base64_decode_table[(uint8_t)input[i++]];
        sextet_d = input[i] == '=' ? 0 & i++ : base64_decode_table[(uint8_t)input[i++]];

        triple = (sextet_a << 18) + (sextet_b << 12) + (sextet_c << 6) + sextet_d;

        if (j < input_len) output[j++] = (triple >> 16) & 0xFF;
        if (j < input_len) output[j++] = (triple >> 8) & 0xFF;
        if (j < input_len) output[j++] = triple & 0xFF;
    }

    output_len = j;

    // Adjust for padding
    if (input_len > 0 && input[input_len - 1] == '=') output_len--;
    if (input_len > 1 && input[input_len - 2] == '=') output_len--;

    return output_len;
}

// SSCMA callbacks
static void on_event(sscma_client_handle_t client, const sscma_client_reply_t *reply, void *user_ctx) {
    // Extract image from reply
    char *image_b64 = NULL;
    int image_b64_size = 0;

    esp_err_t ret = sscma_utils_fetch_image_from_reply(reply, &image_b64, &image_b64_size);
    if (ret == ESP_OK && image_b64 != NULL && image_b64_size > 0) {
        // Decode base64 to JPEG
        if (xSemaphoreTake(frame_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            frame_size = base64_decode(image_b64, image_b64_size, frame_buffer);
            frame_ready = true;
            xSemaphoreGive(frame_mutex);
            ESP_LOGD(TAG, "Frame captured: %d bytes", frame_size);
        }
    }
}

static void on_connect(sscma_client_handle_t client, const sscma_client_reply_t *reply, void *user_ctx) {
    ESP_LOGI(TAG, "SSCMA client connected");
}

static void on_log(sscma_client_handle_t client, const sscma_client_reply_t *reply, void *user_ctx) {
    ESP_LOGD(TAG, "SSCMA log: %s", reply->data);
}

// WiFi event handler
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "WiFi disconnected, reconnecting...");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static void wifi_init_sta(void) {
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi init done, waiting for connection...");
}

// HTTP Handlers
static const char *STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=frame";
static const char *STREAM_BOUNDARY = "\r\n--frame\r\n";
static const char *STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %d\r\n\r\n";

// MJPEG stream handler
static esp_err_t stream_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "Stream client connected");

    esp_err_t res = httpd_resp_set_type(req, STREAM_CONTENT_TYPE);
    if (res != ESP_OK) return res;

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");

    char part_buf[64];

    while (true) {
        // Wait for a frame
        int wait_count = 0;
        while (!frame_ready && wait_count < 100) {
            vTaskDelay(pdMS_TO_TICKS(10));
            wait_count++;
        }

        if (xSemaphoreTake(frame_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            if (frame_ready && frame_size > 0) {
                // Send boundary
                res = httpd_resp_send_chunk(req, STREAM_BOUNDARY, strlen(STREAM_BOUNDARY));
                if (res != ESP_OK) {
                    xSemaphoreGive(frame_mutex);
                    break;
                }

                // Send content type and length
                int part_len = snprintf(part_buf, sizeof(part_buf), STREAM_PART, frame_size);
                res = httpd_resp_send_chunk(req, part_buf, part_len);
                if (res != ESP_OK) {
                    xSemaphoreGive(frame_mutex);
                    break;
                }

                // Send JPEG data
                res = httpd_resp_send_chunk(req, (const char *)frame_buffer, frame_size);
                if (res != ESP_OK) {
                    xSemaphoreGive(frame_mutex);
                    break;
                }

                frame_ready = false;
            }
            xSemaphoreGive(frame_mutex);
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }

    ESP_LOGI(TAG, "Stream client disconnected");
    return ESP_OK;
}

// Single snapshot handler
static esp_err_t snapshot_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "Snapshot requested");

    // Wait for a fresh frame
    frame_ready = false;
    int wait_count = 0;
    while (!frame_ready && wait_count < 200) {
        vTaskDelay(pdMS_TO_TICKS(10));
        wait_count++;
    }

    if (xSemaphoreTake(frame_mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
        if (frame_ready && frame_size > 0) {
            httpd_resp_set_type(req, "image/jpeg");
            httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
            httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
            httpd_resp_send(req, (const char *)frame_buffer, frame_size);
            xSemaphoreGive(frame_mutex);
            return ESP_OK;
        }
        xSemaphoreGive(frame_mutex);
    }

    httpd_resp_send_500(req);
    return ESP_FAIL;
}

// Status page handler
static esp_err_t index_handler(httpd_req_t *req) {
    const char *html =
        "<!DOCTYPE html><html><head><title>Video Stream</title></head><body>"
        "<h1>SenseCAP Watcher Video Stream</h1>"
        "<p><a href=\"/stream\">MJPEG Stream</a></p>"
        "<p><a href=\"/snapshot\">Single Snapshot</a></p>"
        "<h2>Live Preview:</h2>"
        "<img src=\"/stream\" style=\"max-width:100%%;\">"
        "</body></html>";

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html, strlen(html));
    return ESP_OK;
}

// Start HTTP server
static httpd_handle_t start_webserver(void) {
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192;
    config.max_uri_handlers = 8;

    ESP_LOGI(TAG, "Starting HTTP server on port %d", config.server_port);

    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t index_uri = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = index_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &index_uri);

        httpd_uri_t stream_uri = {
            .uri = "/stream",
            .method = HTTP_GET,
            .handler = stream_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &stream_uri);

        httpd_uri_t snapshot_uri = {
            .uri = "/snapshot",
            .method = HTTP_GET,
            .handler = snapshot_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &snapshot_uri);

        return server;
    }

    ESP_LOGE(TAG, "Failed to start HTTP server");
    return NULL;
}

// Camera capture task - continuously captures frames
static void capture_task(void *pvParameters) {
    ESP_LOGI(TAG, "Capture task started, waiting for SSCMA init...");

    // Wait for SSCMA to be ready
    vTaskDelay(pdMS_TO_TICKS(2000));

    // Initialize SSCMA client
    ESP_LOGI(TAG, "Initializing SSCMA client...");
    sscma_client_init(sscma_client);

    // Get device info
    sscma_client_info_t *info = NULL;
    if (sscma_client_get_info(sscma_client, &info, false) == ESP_OK && info != NULL) {
        ESP_LOGI(TAG, "SSCMA Device Info:");
        ESP_LOGI(TAG, "  ID: %s", info->id ? info->id : "N/A");
        ESP_LOGI(TAG, "  Name: %s", info->name ? info->name : "N/A");
        ESP_LOGI(TAG, "  HW Ver: %s", info->hw_ver ? info->hw_ver : "N/A");
        ESP_LOGI(TAG, "  SW Ver: %s", info->sw_ver ? info->sw_ver : "N/A");
        ESP_LOGI(TAG, "  FW Ver: %s", info->fw_ver ? info->fw_ver : "N/A");
    }

    // Enable sensor (camera) - sensor 0, option 0, enable
    ESP_LOGI(TAG, "Enabling camera sensor...");
    esp_err_t ret = sscma_client_set_sensor(sscma_client, 0, 0, true);
    ESP_LOGI(TAG, "Set sensor result: %s", esp_err_to_name(ret));

    // Start continuous sampling with image output
    // invoke(times=-1, filter=false, show=true) - continuous mode with images
    ESP_LOGI(TAG, "Starting continuous capture...");
    ret = sscma_client_invoke(sscma_client, -1, false, true);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start invoke: %s", esp_err_to_name(ret));
    }

    // The capture runs in the background via callbacks
    // This task just monitors and can restart if needed
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        ESP_LOGI(TAG, "Capture task heartbeat - frames captured: %s", frame_ready ? "yes" : "no");
    }
}

void video_stream_main(void) {
    uart_set_baudrate(UART_NUM_0, 115200);
    vTaskDelay(pdMS_TO_TICKS(100));

    ESP_LOGI(TAG, "Video Stream App Starting...");

    // Initialize frame buffer and mutex
    frame_mutex = xSemaphoreCreateMutex();
    frame_buffer = (uint8_t *)heap_caps_malloc(100 * 1024, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!frame_buffer) {
        frame_buffer = (uint8_t *)malloc(100 * 1024);
    }
    if (!frame_buffer) {
        ESP_LOGE(TAG, "Failed to allocate frame buffer!");
        return;
    }
    ESP_LOGI(TAG, "Frame buffer allocated");

    // 1. Init Hardware BSP
    ESP_LOGI(TAG, "Init I2C bus...");
    bsp_i2c_bus_init();
    ESP_LOGI(TAG, "Init SPI bus...");
    bsp_spi_bus_init();
    ESP_LOGI(TAG, "Init IO expander...");
    bsp_io_expander_init();

    vTaskDelay(pdMS_TO_TICKS(500));

    // 2. Init SSCMA client (camera)
    ESP_LOGI(TAG, "Init SSCMA client...");
    sscma_client = bsp_sscma_client_init();
    if (sscma_client == NULL) {
        ESP_LOGE(TAG, "Failed to initialize SSCMA client!");
        return;
    }

    // Register callbacks
    const sscma_client_callback_t callbacks = {
        .on_connect = on_connect,
        .on_event = on_event,
        .on_log = on_log,
    };
    sscma_client_register_callback(sscma_client, &callbacks, NULL);

    // 3. Init NVS (required for WiFi)
    ESP_LOGI(TAG, "Init NVS...");
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 4. Init WiFi
    ESP_LOGI(TAG, "Init WiFi...");
    wifi_init_sta();

    // Wait for WiFi connection
    xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdFALSE, portMAX_DELAY);
    ESP_LOGI(TAG, "WiFi connected!");

    // 5. Start HTTP server
    httpd_handle_t server = start_webserver();
    if (server == NULL) {
        ESP_LOGE(TAG, "Failed to start web server!");
        return;
    }

    // Get our IP for logging
    esp_netif_ip_info_t ip_info;
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
        ESP_LOGI(TAG, "========================================");
        ESP_LOGI(TAG, "Video stream available at:");
        ESP_LOGI(TAG, "  http://" IPSTR "/", IP2STR(&ip_info.ip));
        ESP_LOGI(TAG, "  http://" IPSTR "/stream", IP2STR(&ip_info.ip));
        ESP_LOGI(TAG, "  http://" IPSTR "/snapshot", IP2STR(&ip_info.ip));
        ESP_LOGI(TAG, "========================================");
    }

    // 6. Start capture task
    xTaskCreatePinnedToCore(capture_task, "capture", 8192, NULL, 5, NULL, 0);

    ESP_LOGI(TAG, "Video Stream App initialized!");
}
