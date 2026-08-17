#include "generated_assets.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ASSETS";

// Global Pointers (Sprites)
uint16_t *fox_w1_data = NULL;
uint16_t *fox_w1m_data = NULL;
uint16_t *fox_w2_data = NULL;
uint16_t *fox_w2m_data = NULL;
uint16_t *fox_w3_data = NULL;
uint16_t *fox_w3m_data = NULL;
uint16_t *fox_w4_data = NULL;
uint16_t *fox_w4m_data = NULL;
uint16_t *fox_w5_data = NULL;
uint16_t *fox_w5m_data = NULL;
uint16_t *fox_w6_data = NULL;
uint16_t *fox_w6m_data = NULL;
uint16_t *fox_w7_data = NULL;
uint16_t *fox_w7m_data = NULL;
uint16_t *fox_w8_data = NULL;
uint16_t *fox_w8m_data = NULL;

// --- CURRENT MAP DATA ---
uint16_t *current_map_image_data = NULL;
uint8_t *current_collision_data = NULL;
ExitPoint exit_points[MAX_EXIT_POINTS];
uint8_t exit_point_count = 0;
int spawn_x = 800;
int spawn_y = 800;

// --- MOB DATA ---
Mob current_mobs[MAX_MOBS];
uint8_t current_mob_count = 0;

// --- SPELL SPRITES ---
// spell_frames[spell_type][frame_index]
// Spell types: 0=M, 1=W, 2=S, 3=N, 4=O, 5=BASIC
uint16_t *spell_frames[SPELL_TYPE_COUNT][SPELL_FRAME_COUNT] = {{NULL}};
bool spells_loaded = false; 

#define SERVER_IP "192.168.0.191"
#define SERVER_PORT 8082

esp_err_t _asset_http_event_handler(esp_http_client_event_t *evt) { return ESP_OK; }

// Helper to fetch collision data
static uint8_t* fetch_collision(const char* url_path) {
    char url[128];
    snprintf(url, sizeof(url), "http://%s:%d/%s", SERVER_IP, SERVER_PORT, url_path);
    
    esp_http_client_config_t config = { .url = url, .event_handler = _asset_http_event_handler, .timeout_ms = 5000 };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    uint8_t* data = NULL;

    if (esp_http_client_open(client, 0) == ESP_OK) {
        int content_length = esp_http_client_fetch_headers(client);
        if (content_length > 0) {
            data = (uint8_t*)malloc(content_length);
            if (data) {
                int total_read = 0;
                while (total_read < content_length) {
                     int r = esp_http_client_read(client, (char*)data + total_read, content_length - total_read);
                     if (r <= 0) break;
                     total_read += r;
                }
            }
        }
    }
    esp_http_client_cleanup(client);
    return data;
}

// Helper to fetch exits
static int fetch_exits(const char* url_path, ExitPoint* target_array) {
    char url[128];
    snprintf(url, sizeof(url), "http://%s:%d/%s", SERVER_IP, SERVER_PORT, url_path);
    
    esp_http_client_config_t config = { .url = url, .event_handler = _asset_http_event_handler, .timeout_ms = 5000 };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    int count = 0;

    if (esp_http_client_open(client, 0) == ESP_OK) {
        int len = esp_http_client_fetch_headers(client);
        if (len > 0 && len < 256) {
            uint8_t buffer[256];
            int r = esp_http_client_read(client, (char*)buffer, len);
            if (r > 0) {
                count = buffer[0];
                if (count > MAX_EXIT_POINTS) count = MAX_EXIT_POINTS;
                for (int i = 0; i < count; i++) {
                    int off = 1 + i * 9;
                    if (off + 8 >= r) break;
                    target_array[i].edge = buffer[off];
                    target_array[i].tile_x = buffer[off+1] | (buffer[off+2] << 8);
                    target_array[i].tile_y = buffer[off+3] | (buffer[off+4] << 8);
                    target_array[i].pixel_x = buffer[off+5] | (buffer[off+6] << 8);
                    target_array[i].pixel_y = buffer[off+7] | (buffer[off+8] << 8);
                }
            }
        }
    }
    esp_http_client_cleanup(client);
    return count;
}

// Helper to fetch map image
static uint16_t* fetch_map_image(const char* url_path) {
    char url[128];
    snprintf(url, sizeof(url), "http://%s:%d/%s", SERVER_IP, SERVER_PORT, url_path);
    
    esp_http_client_config_t config = { .url = url, .event_handler = _asset_http_event_handler, .buffer_size = 8192, .timeout_ms = 10000 };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    uint16_t* data = NULL;

    if (esp_http_client_open(client, 0) == ESP_OK) {
        int len = esp_http_client_fetch_headers(client);
        if (len > 0) {
            data = (uint16_t*)heap_caps_malloc(SIZE_FULL_MAP, MALLOC_CAP_SPIRAM);
            if (data) {
                int total = 0;
                while (total < len) {
                    int r = esp_http_client_read(client, (char*)data + total, len - total);
                    if (r <= 0) break;
                    total += r;
                }
                if (total != len) ESP_LOGW(TAG, "Incomplete map download %d/%d", total, len);
            } else {
                ESP_LOGE(TAG, "OOM: Could not allocate map image!");
            }
        }
    }
    esp_http_client_cleanup(client);
    return data;
}

// --- MAIN ASSET LOGIC ---

void fetch_global_map_data(void) {
    ESP_LOGI(TAG, "Loading Map Data...");

    // 1. Download Collision
    uint8_t* col_data = fetch_collision("get_map_collision");
    if (current_collision_data) free(current_collision_data);
    current_collision_data = col_data;

    // 2. Download Exit Points
    exit_point_count = fetch_exits("get_exit_points", exit_points);
    if (exit_point_count > 0) {
        spawn_x = exit_points[0].pixel_x;
        spawn_y = exit_points[0].pixel_y;
    }

    // 3. Download Map Image
    if (!current_map_image_data) {
        current_map_image_data = (uint16_t*)heap_caps_malloc(SIZE_FULL_MAP, MALLOC_CAP_SPIRAM);
    }
    if (current_map_image_data) {
        char url[128];
        snprintf(url, sizeof(url), "http://%s:%d/get_map_image", SERVER_IP, SERVER_PORT);
        esp_http_client_config_t config = { .url = url, .event_handler = _asset_http_event_handler, .buffer_size = 32768, .timeout_ms = 30000 };
        esp_http_client_handle_t client = esp_http_client_init(&config);
        if (esp_http_client_open(client, 0) == ESP_OK) {
            int len = esp_http_client_fetch_headers(client);
            int total = 0;
            while (total < len) {
                int r = esp_http_client_read(client, (char*)current_map_image_data + total, len - total);
                if (r <= 0) break;
                total += r;
            }
            ESP_LOGI(TAG, "Map image downloaded: %d bytes", total);
        }
        esp_http_client_cleanup(client);
    } else {
        ESP_LOGE(TAG, "OOM: Could not allocate map image buffer!");
    }
}

// Download a single file to PSRAM
static uint16_t* download_asset(const char *filename, size_t expected_pixels) {
    char url[128];
    snprintf(url, sizeof(url), "http://%s:%d/assets/%s", SERVER_IP, SERVER_PORT, filename);
    
    esp_http_client_config_t config = {
        .url = url,
        .event_handler = _asset_http_event_handler,
        .buffer_size = 4096,
        .timeout_ms = 5000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    
    uint16_t *dest_data = NULL;
    
    if (esp_http_client_open(client, 0) == ESP_OK) {
        int content_length = esp_http_client_fetch_headers(client);
        if (content_length > 0) {
            // Alloc in PSRAM
            dest_data = (uint16_t*)heap_caps_malloc(content_length, MALLOC_CAP_SPIRAM);
            if (dest_data) {
                int total_read = 0;
                while (1) {
                    int read_len = esp_http_client_read(client, (char*)dest_data + total_read, content_length - total_read);
                    if (read_len <= 0) break;
                    total_read += read_len;
                }
            }
        }
    }
    esp_http_client_cleanup(client);
    return dest_data;
}

void load_assets_from_sd(void) {
    // Note: Fox assets removed - pet sprites are now loaded dynamically
    // based on the active pet's species/element after fetching pet data
    ESP_LOGI(TAG, "Assets initialization complete (pet sprites loaded dynamically).");
}

// Simple JSON string value parser (finds "key": "value" and extracts value)
// Handles optional space after colon
static int parse_json_string(const char *json, const char *key, char *out, size_t out_size) {
    char search[32];
    snprintf(search, sizeof(search), "\"%s\":", key);
    const char *start = strstr(json, search);
    if (!start) return -1;
    start += strlen(search);
    // Skip whitespace
    while (*start == ' ' || *start == '\t') start++;
    // Expect opening quote
    if (*start != '"') return -1;
    start++;
    const char *end = strchr(start, '"');
    if (!end) return -1;
    size_t len = end - start;
    if (len >= out_size) len = out_size - 1;
    strncpy(out, start, len);
    out[len] = '\0';
    return 0;
}

// Simple JSON integer value parser (finds "key": 123 and extracts value)
// Handles optional space after colon
static int parse_json_int(const char *json, const char *key) {
    char search[32];
    snprintf(search, sizeof(search), "\"%s\":", key);
    const char *start = strstr(json, search);
    if (!start) return 0;
    start += strlen(search);
    // Skip whitespace
    while (*start == ' ' || *start == '\t') start++;
    return atoi(start);
}

void fetch_mobs(void) {
    ESP_LOGI(TAG, "Fetching mobs from server...");

    // Free old mob sprites
    for (int i = 0; i < current_mob_count; i++) {
        if (current_mobs[i].sprite_data) {
            heap_caps_free(current_mobs[i].sprite_data);
            current_mobs[i].sprite_data = NULL;
        }
    }
    current_mob_count = 0;

    // Fetch mob list JSON from server
    char url[128];
    snprintf(url, sizeof(url), "http://%s:%d/get_mobs", SERVER_IP, SERVER_PORT);

    esp_http_client_config_t config = {
        .url = url,
        .event_handler = _asset_http_event_handler,
        .timeout_ms = 5000
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    if (esp_http_client_open(client, 0) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to connect to get_mobs");
        esp_http_client_cleanup(client);
        return;
    }

    int content_length = esp_http_client_fetch_headers(client);
    if (content_length <= 0 || content_length > 4096) {
        ESP_LOGW(TAG, "Invalid mob response length: %d", content_length);
        esp_http_client_cleanup(client);
        return;
    }

    char *json_buf = malloc(content_length + 1);
    if (!json_buf) {
        ESP_LOGE(TAG, "OOM for mob JSON buffer");
        esp_http_client_cleanup(client);
        return;
    }

    int total_read = 0;
    while (total_read < content_length) {
        int r = esp_http_client_read(client, json_buf + total_read, content_length - total_read);
        if (r <= 0) break;
        total_read += r;
    }
    json_buf[total_read] = '\0';
    esp_http_client_cleanup(client);

    ESP_LOGI(TAG, "Mob JSON (%d bytes): %.100s...", total_read, json_buf);

    // Parse JSON array manually (simple parsing for [{...}, {...}, ...])
    // Format: [{"species":"fox","element":"fire","x":123,"y":456,"sprite_key":"fox_fire"}, ...]
    const char *ptr = json_buf;
    int mob_idx = 0;

    while (mob_idx < MAX_MOBS) {
        // Find next object start
        const char *obj_start = strchr(ptr, '{');
        if (!obj_start) break;

        // Find object end
        const char *obj_end = strchr(obj_start, '}');
        if (!obj_end) break;

        // Extract a single object as string
        size_t obj_len = obj_end - obj_start + 1;
        char obj_buf[256];
        if (obj_len >= sizeof(obj_buf)) obj_len = sizeof(obj_buf) - 1;
        strncpy(obj_buf, obj_start, obj_len);
        obj_buf[obj_len] = '\0';

        // Parse fields
        char species[32] = {0};
        char element[16] = {0};
        char sprite_key[32] = {0};

        parse_json_string(obj_buf, "species", species, sizeof(species));
        parse_json_string(obj_buf, "element", element, sizeof(element));
        parse_json_string(obj_buf, "sprite_key", sprite_key, sizeof(sprite_key));
        int x = parse_json_int(obj_buf, "x");
        int y = parse_json_int(obj_buf, "y");

        if (strlen(sprite_key) > 0) {
            // Download the sprite
            char asset_name[48];
            snprintf(asset_name, sizeof(asset_name), "mob_%s.bin", sprite_key);

            uint16_t *sprite = download_asset(asset_name, MOB_SPRITE_SIZE / 2);
            if (sprite) {
                strncpy(current_mobs[mob_idx].species, species, sizeof(current_mobs[mob_idx].species) - 1);
                strncpy(current_mobs[mob_idx].element, element, sizeof(current_mobs[mob_idx].element) - 1);
                current_mobs[mob_idx].x = x;
                current_mobs[mob_idx].y = y;
                current_mobs[mob_idx].sprite_data = sprite;
                mob_idx++;
                ESP_LOGI(TAG, "Loaded mob: %s at (%d, %d)", sprite_key, x, y);
            } else {
                ESP_LOGW(TAG, "Failed to download sprite: %s", asset_name);
            }
        }

        ptr = obj_end + 1;
    }

    current_mob_count = mob_idx;
    free(json_buf);

    ESP_LOGI(TAG, "Loaded %d mobs", current_mob_count);
}

void send_gesture_data(int16_t *points_x, int16_t *points_y, int point_count) {
    if (point_count < 3) {
        ESP_LOGW(TAG, "Gesture too short (%d points), not sending", point_count);
        return;
    }

    ESP_LOGI(TAG, "Sending gesture with %d points to server...", point_count);

    // Build JSON payload: {"points": [[x1,y1], [x2,y2], ...]}
    // Max size: ~10 chars per point * 100 points + overhead = ~1200 bytes
    char *json_buf = malloc(2048);
    if (!json_buf) {
        ESP_LOGE(TAG, "OOM for gesture JSON");
        return;
    }

    int offset = 0;
    offset += snprintf(json_buf + offset, 2048 - offset, "{\"points\":[");

    for (int i = 0; i < point_count; i++) {
        if (i > 0) {
            offset += snprintf(json_buf + offset, 2048 - offset, ",");
        }
        offset += snprintf(json_buf + offset, 2048 - offset, "[%d,%d]", points_x[i], points_y[i]);
    }

    offset += snprintf(json_buf + offset, 2048 - offset, "]}");

    // Send POST request to server
    char url[128];
    snprintf(url, sizeof(url), "http://%s:%d/gesture", SERVER_IP, SERVER_PORT);

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .event_handler = _asset_http_event_handler,
        .timeout_ms = 5000
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, json_buf, strlen(json_buf));

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "Gesture sent, server responded with status %d", status);
    } else {
        ESP_LOGE(TAG, "Failed to send gesture: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    free(json_buf);
}

// Calibration spell type names
static const char *calib_spell_names[] = {"M", "W", "S", "N", "O"};

// Send gesture data for calibration
int send_calibration_gesture(int16_t *points_x, int16_t *points_y, int point_count, int spell_type) {
    if (point_count < 3 || spell_type < 0 || spell_type > 4) {
        ESP_LOGW(TAG, "Invalid calibration gesture (count=%d, spell=%d)", point_count, spell_type);
        return -1;
    }

    ESP_LOGI(TAG, "Sending calibration gesture for '%s' with %d points...", calib_spell_names[spell_type], point_count);

    // Build JSON: {"spell_type": "M", "points": [[x1,y1], ...]}
    char *json_buf = malloc(4096);
    if (!json_buf) {
        ESP_LOGE(TAG, "OOM for calibration JSON");
        return -1;
    }

    int offset = 0;
    offset += snprintf(json_buf + offset, 4096 - offset, "{\"spell_type\":\"%s\",\"points\":[", calib_spell_names[spell_type]);

    for (int i = 0; i < point_count; i++) {
        if (i > 0) {
            offset += snprintf(json_buf + offset, 4096 - offset, ",");
        }
        offset += snprintf(json_buf + offset, 4096 - offset, "[%d,%d]", points_x[i], points_y[i]);
    }

    offset += snprintf(json_buf + offset, 4096 - offset, "]}");

    char url[128];
    snprintf(url, sizeof(url), "http://%s:%d/gesture/calibrate", SERVER_IP, SERVER_PORT);

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .event_handler = _asset_http_event_handler,
        .timeout_ms = 5000
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, json_buf, strlen(json_buf));

    int result = -1;
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "Calibration gesture sent, status %d", status);
        if (status == 200) {
            result = 0;  // Success
        }
    } else {
        ESP_LOGE(TAG, "Failed to send calibration: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    free(json_buf);
    return result;
}

// Send gesture for recording/visualization (simple endpoint)
int send_gesture_record(int16_t *points_x, int16_t *points_y, int point_count) {
    if (point_count < 3) {
        ESP_LOGW(TAG, "Too few points for recording (%d)", point_count);
        return -1;
    }

    ESP_LOGI(TAG, "Sending gesture for recording (%d points)...", point_count);

    char *json_buf = malloc(8192);
    if (!json_buf) {
        ESP_LOGE(TAG, "OOM for record JSON");
        return -1;
    }

    int offset = 0;
    offset += snprintf(json_buf + offset, 8192 - offset, "{\"points\":[");

    for (int i = 0; i < point_count; i++) {
        if (i > 0) {
            offset += snprintf(json_buf + offset, 8192 - offset, ",");
        }
        offset += snprintf(json_buf + offset, 8192 - offset, "[%d,%d]", points_x[i], points_y[i]);
    }

    offset += snprintf(json_buf + offset, 8192 - offset, "]}");

    char url[128];
    snprintf(url, sizeof(url), "http://%s:%d/gesture/record", SERVER_IP, SERVER_PORT);

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .event_handler = _asset_http_event_handler,
        .timeout_ms = 5000
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, json_buf, strlen(json_buf));

    int result = -1;
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "Gesture record sent, status %d", status);
        if (status == 200) {
            result = 0;
        }
    } else {
        ESP_LOGE(TAG, "Failed to send gesture record: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    free(json_buf);
    return result;
}

// Recognize gesture via server
// Returns spell index (0-4) or 5 for BASIC, -1 on error
int recognize_gesture_server(int16_t *points_x, int16_t *points_y, int point_count) {
    if (point_count < 3) {
        return 5;  // BASIC for too-short gestures
    }

    ESP_LOGI(TAG, "Sending gesture for recognition (%d points)...", point_count);

    char *json_buf = malloc(4096);
    if (!json_buf) {
        ESP_LOGE(TAG, "OOM for recognition JSON");
        return 5;
    }

    int offset = 0;
    offset += snprintf(json_buf + offset, 4096 - offset, "{\"points\":[");

    for (int i = 0; i < point_count; i++) {
        if (i > 0) {
            offset += snprintf(json_buf + offset, 4096 - offset, ",");
        }
        offset += snprintf(json_buf + offset, 4096 - offset, "[%d,%d]", points_x[i], points_y[i]);
    }

    offset += snprintf(json_buf + offset, 4096 - offset, "]}");

    char url[128];
    snprintf(url, sizeof(url), "http://%s:%d/gesture/recognize", SERVER_IP, SERVER_PORT);

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .event_handler = _asset_http_event_handler,
        .timeout_ms = 5000
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, json_buf, strlen(json_buf));

    int result = 5;  // Default to BASIC
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        if (status == 200) {
            // Read response
            char resp_buf[256];
            int content_len = esp_http_client_get_content_length(client);
            if (content_len > 0 && content_len < sizeof(resp_buf)) {
                esp_http_client_read(client, resp_buf, content_len);
                resp_buf[content_len] = '\0';

                // Parse spell_index from JSON response
                // Simple parsing - look for "spell_index":X
                char *idx_str = strstr(resp_buf, "\"spell_index\":");
                if (idx_str) {
                    result = atoi(idx_str + 14);
                    if (result < 0 || result > 5) result = 5;
                }
                ESP_LOGI(TAG, "Recognition result: spell_index=%d", result);
            }
        }
    } else {
        ESP_LOGE(TAG, "Failed to recognize gesture: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    free(json_buf);
    return result;
}

// Spell type names for building filenames
static const char *spell_keys[] = {"spell_m", "spell_w", "spell_s", "spell_n", "spell_o", "spell_basic"};

void fetch_spell_sprites(void) {
    ESP_LOGI(TAG, "Loading spell animation sprites...");

    int loaded_count = 0;

    for (int spell = 0; spell < SPELL_TYPE_COUNT; spell++) {
        for (int frame = 0; frame < SPELL_FRAME_COUNT; frame++) {
            // Build filename: e.g., "spell_m_0.bin"
            char filename[32];
            snprintf(filename, sizeof(filename), "%s_%d.bin", spell_keys[spell], frame);

            uint16_t *sprite = download_asset(filename, SPELL_FRAME_SIZE / 2);
            if (sprite) {
                spell_frames[spell][frame] = sprite;
                loaded_count++;
            } else {
                ESP_LOGW(TAG, "Failed to load spell sprite: %s", filename);
                spell_frames[spell][frame] = NULL;
            }
        }
    }

    spells_loaded = (loaded_count > 0);
    ESP_LOGI(TAG, "Spell sprites loaded: %d/%d", loaded_count, SPELL_TYPE_COUNT * SPELL_FRAME_COUNT);
}

// ============== SKILL-BASED CALIBRATION API ==============

// Simple JSON string extractor helper (handles spaces after colon)
static const char* json_find_string(const char *json, const char *key, char *out_buf, int buf_size) {
    char search[64];
    snprintf(search, sizeof(search), "\"%s\":", key);
    const char *start = strstr(json, search);
    if (!start) return NULL;
    start += strlen(search);
    // Skip whitespace
    while (*start == ' ' || *start == '\t') start++;
    // Expect opening quote
    if (*start != '"') return NULL;
    start++;  // Skip the quote
    const char *end = strchr(start, '"');
    if (!end) return NULL;
    int len = end - start;
    if (len >= buf_size) len = buf_size - 1;
    strncpy(out_buf, start, len);
    out_buf[len] = '\0';
    return out_buf;
}

// Simple JSON integer extractor helper
static int json_find_int(const char *json, const char *key) {
    char search[64];
    snprintf(search, sizeof(search), "\"%s\":", key);
    const char *start = strstr(json, search);
    if (!start) return -1;
    start += strlen(search);
    // Skip whitespace
    while (*start == ' ' || *start == '\t') start++;
    return atoi(start);
}

// Simple JSON boolean extractor helper
static bool json_find_bool(const char *json, const char *key) {
    char search[64];
    snprintf(search, sizeof(search), "\"%s\":", key);
    const char *start = strstr(json, search);
    if (!start) return false;
    start += strlen(search);
    while (*start == ' ' || *start == '\t') start++;
    return (strncmp(start, "true", 4) == 0);
}

// Fetch uncalibrated skills from server
int fetch_uncalibrated_skills(uncalibrated_skill_callback_t callback, int *total_skills, int *calibrated_count) {
    ESP_LOGI(TAG, "Fetching uncalibrated skills...");

    char url[128];
    snprintf(url, sizeof(url), "http://%s:%d/gesture/uncalibrated", SERVER_IP, SERVER_PORT);

    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = 10000
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    int result = -1;

    // Use open/fetch_headers/read pattern to handle chunked responses
    if (esp_http_client_open(client, 0) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open connection");
        esp_http_client_cleanup(client);
        return -1;
    }

    int content_len = esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG, "Uncalibrated skills: status=%d, content_len=%d", status, content_len);

    if (status == 200) {
        // Allocate buffer for response (handle chunked: content_len may be -1)
        int buf_size = (content_len > 0) ? content_len + 1 : 32000;
        char *resp_buf = malloc(buf_size);
        if (!resp_buf) {
            ESP_LOGE(TAG, "OOM for uncalibrated skills response");
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return -1;
        }

        // Read response data
        int total_read = 0;
        while (total_read < buf_size - 1) {
            int read_len = esp_http_client_read(client, resp_buf + total_read, buf_size - 1 - total_read);
            if (read_len <= 0) break;
            total_read += read_len;
        }
        resp_buf[total_read] = '\0';
        ESP_LOGI(TAG, "Read %d bytes of response", total_read);

        if (total_read > 0) {
            // Parse total_skills and calibrated_count
            if (total_skills) {
                *total_skills = json_find_int(resp_buf, "total_skills");
            }
            if (calibrated_count) {
                *calibrated_count = json_find_int(resp_buf, "calibrated_count");
            }

            // Parse uncalibrated array
            const char *arr_start = strstr(resp_buf, "\"uncalibrated\"");
            if (arr_start) {
                arr_start = strchr(arr_start, '[');
                if (arr_start) {
                    arr_start++;
                    result = 0;

                    // Parse each skill object
                    const char *ptr = arr_start;
                    while (*ptr && *ptr != ']') {
                        // Find next object
                        const char *obj_start = strchr(ptr, '{');
                        const char *arr_end = strchr(ptr, ']');
                        if (!obj_start || (arr_end && obj_start > arr_end)) break;

                        const char *obj_end = strchr(obj_start, '}');
                        if (!obj_end) break;

                        // Extract skill info
                        char skill_id[32] = {0};
                        char skill_name[48] = {0};

                        // Find "id"
                        const char *id_start = strstr(obj_start, "\"id\"");
                        if (id_start && id_start < obj_end) {
                            id_start = strchr(id_start + 4, '"');
                            if (id_start) {
                                id_start++;
                                const char *id_end = strchr(id_start, '"');
                                if (id_end && (id_end - id_start) < (int)sizeof(skill_id)) {
                                    strncpy(skill_id, id_start, id_end - id_start);
                                }
                            }
                        }

                        // Find "name"
                        const char *name_start = strstr(obj_start, "\"name\"");
                        if (name_start && name_start < obj_end) {
                            name_start = strchr(name_start + 6, '"');
                            if (name_start) {
                                name_start++;
                                const char *name_end = strchr(name_start, '"');
                                if (name_end && (name_end - name_start) < (int)sizeof(skill_name)) {
                                    strncpy(skill_name, name_start, name_end - name_start);
                                }
                            }
                        }

                        if (skill_id[0] && callback) {
                            callback(skill_id, skill_name);
                            result++;
                        }

                        ptr = obj_end + 1;
                    }
                }
            }
        }
        free(resp_buf);
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    ESP_LOGI(TAG, "Loaded %d uncalibrated skills", result);
    return result;
}

// Send calibration sample for a specific skill
int send_skill_calibration(const char *skill_id, int16_t *points_x, int16_t *points_y, int point_count) {
    if (!skill_id || point_count < 3) {
        ESP_LOGW(TAG, "Invalid skill calibration params");
        return -1;
    }

    ESP_LOGI(TAG, "Sending calibration for skill '%s' (%d points)...", skill_id, point_count);

    // Build JSON: {"skill_id": "xxx", "points": [[x,y], ...]}
    char *json_buf = malloc(8192);
    if (!json_buf) {
        ESP_LOGE(TAG, "OOM for calibration JSON");
        return -1;
    }

    int offset = 0;
    offset += snprintf(json_buf + offset, 8192 - offset, "{\"skill_id\":\"%s\",\"points\":[", skill_id);

    for (int i = 0; i < point_count; i++) {
        if (i > 0) {
            offset += snprintf(json_buf + offset, 8192 - offset, ",");
        }
        offset += snprintf(json_buf + offset, 8192 - offset, "{\"x\":%d,\"y\":%d}", points_x[i], points_y[i]);
    }

    offset += snprintf(json_buf + offset, 8192 - offset, "]}");

    char url[128];
    snprintf(url, sizeof(url), "http://%s:%d/gesture/calibrate", SERVER_IP, SERVER_PORT);

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .event_handler = _asset_http_event_handler,
        .timeout_ms = 5000
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, json_buf, strlen(json_buf));

    int result = -1;
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "Skill calibration sent, status %d", status);
        if (status == 200) {
            result = 0;
        }
    } else {
        ESP_LOGE(TAG, "Failed to send skill calibration: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    free(json_buf);
    return result;
}

// Recognize gesture against active skills
int recognize_skill_gesture(int16_t *points_x, int16_t *points_y, int point_count,
                            const char *active_skills[], int active_count,
                            char *out_skill_id, int *out_accuracy, float *out_damage_mult) {
    if (point_count < 3 || active_count < 1) {
        return -1;
    }

    ESP_LOGI(TAG, "Recognizing skill gesture (%d points, %d active skills)...", point_count, active_count);

    // Build JSON: {"points": [...], "active_skills": ["skill1", "skill2", ...]}
    char *json_buf = malloc(8192);
    if (!json_buf) {
        ESP_LOGE(TAG, "OOM for recognition JSON");
        return -1;
    }

    int offset = 0;
    offset += snprintf(json_buf + offset, 8192 - offset, "{\"points\":[");

    for (int i = 0; i < point_count; i++) {
        if (i > 0) {
            offset += snprintf(json_buf + offset, 8192 - offset, ",");
        }
        offset += snprintf(json_buf + offset, 8192 - offset, "{\"x\":%d,\"y\":%d}", points_x[i], points_y[i]);
    }

    offset += snprintf(json_buf + offset, 8192 - offset, "],\"active_skills\":[");

    for (int i = 0; i < active_count; i++) {
        if (i > 0) {
            offset += snprintf(json_buf + offset, 8192 - offset, ",");
        }
        offset += snprintf(json_buf + offset, 8192 - offset, "\"%s\"", active_skills[i]);
    }

    offset += snprintf(json_buf + offset, 8192 - offset, "]}");

    char url[128];
    snprintf(url, sizeof(url), "http://%s:%d/gesture/recognize", SERVER_IP, SERVER_PORT);

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 5000
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    esp_http_client_set_header(client, "Content-Type", "application/json");

    int result = -1;

    // Use open/write/fetch_headers/read pattern for POST with response
    esp_err_t err = esp_http_client_open(client, strlen(json_buf));
    if (err == ESP_OK) {
        // Write the POST body
        int wlen = esp_http_client_write(client, json_buf, strlen(json_buf));
        if (wlen > 0) {
            // Fetch response headers
            int content_len = esp_http_client_fetch_headers(client);
            int status = esp_http_client_get_status_code(client);

            if (status == 200) {
                char resp_buf[512];
                int read_len = 0;

                // Handle both chunked (content_len=-1 or 0) and fixed-length responses
                if (content_len > 0 && content_len < (int)sizeof(resp_buf)) {
                    read_len = esp_http_client_read(client, resp_buf, content_len);
                } else {
                    // Chunked or unknown length - read until done
                    int total = 0;
                    int r;
                    while (total < (int)sizeof(resp_buf) - 1) {
                        r = esp_http_client_read(client, resp_buf + total, sizeof(resp_buf) - 1 - total);
                        if (r <= 0) break;
                        total += r;
                    }
                    read_len = total;
                }

                if (read_len > 0) {
                    resp_buf[read_len] = '\0';

                    ESP_LOGI(TAG, "Recognition response: %s", resp_buf);

                    // Parse skill_id
                    if (out_skill_id) {
                        json_find_string(resp_buf, "skill_id", out_skill_id, 32);
                    }

                    // Parse accuracy
                    if (out_accuracy) {
                        *out_accuracy = json_find_int(resp_buf, "accuracy");
                    }

                    // Parse damage_multiplier
                    if (out_damage_mult) {
                        // Simple float parse
                        const char *dm_str = strstr(resp_buf, "\"damage_multiplier\":");
                        if (dm_str) {
                            *out_damage_mult = atof(dm_str + 20);
                        } else {
                            *out_damage_mult = 1.0f;
                        }
                    }

                    // Return 0 if skill was recognized, -1 if not
                    if (out_skill_id && out_skill_id[0] != '\0' && strcmp(out_skill_id, "null") != 0) {
                        result = 0;
                    }
                } else {
                    ESP_LOGE(TAG, "Recognition failed: no response body read");
                }
            } else {
                ESP_LOGE(TAG, "Recognition failed: status=%d", status);
            }
        }
    } else {
        ESP_LOGE(TAG, "Failed to open connection: %s", esp_err_to_name(err));
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    free(json_buf);
    return result;
}

// ============== RUNE CALIBRATION API ==============

// Helper to find bool in a limited range (for parsing nested JSON)
static bool json_find_bool_in_range(const char *json, int len, const char *key) {
    char search[64];
    snprintf(search, sizeof(search), "\"%s\":", key);
    const char *start = strstr(json, search);
    if (!start || (start - json) > len) return false;
    start += strlen(search);
    while (*start == ' ' || *start == '\t') start++;
    return (strncmp(start, "true", 4) == 0);
}

// Fetch all runes for calibration from server
int fetch_runes_for_calibration(rune_callback_t callback, int *total_runes, int *calibrated_count) {
    ESP_LOGI(TAG, "Fetching runes for calibration...");

    char url[128];
    snprintf(url, sizeof(url), "http://%s:%d/runes/list", SERVER_IP, SERVER_PORT);

    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = 10000
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    int result = -1;

    if (esp_http_client_open(client, 0) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open connection");
        esp_http_client_cleanup(client);
        return -1;
    }

    int content_len = esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG, "Runes list: status=%d, content_len=%d", status, content_len);

    if (status == 200) {
        // Allocate buffer for response
        int buf_size = (content_len > 0) ? content_len + 1 : 64000;
        char *resp_buf = malloc(buf_size);
        if (!resp_buf) {
            ESP_LOGE(TAG, "OOM for runes response");
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return -1;
        }

        // Read response data
        int total_read = 0;
        while (total_read < buf_size - 1) {
            int read_len = esp_http_client_read(client, resp_buf + total_read, buf_size - 1 - total_read);
            if (read_len <= 0) break;
            total_read += read_len;
        }
        resp_buf[total_read] = '\0';
        ESP_LOGI(TAG, "Read %d bytes of runes response", total_read);

        if (total_read > 0) {
            // Parse total and calibrated from summary
            if (total_runes) {
                *total_runes = json_find_int(resp_buf, "total");
            }
            if (calibrated_count) {
                *calibrated_count = json_find_int(resp_buf, "calibrated");
            }

            // Parse runes array
            const char *arr_start = strstr(resp_buf, "\"runes\"");
            if (arr_start) {
                arr_start = strchr(arr_start, '[');
                if (arr_start) {
                    arr_start++;
                    result = 0;

                    const char *ptr = arr_start;
                    while (*ptr && *ptr != ']') {
                        const char *obj_start = strchr(ptr, '{');
                        const char *arr_end = strchr(ptr, ']');
                        if (!obj_start || (arr_end && obj_start > arr_end)) break;

                        const char *obj_end = strchr(obj_start, '}');
                        if (!obj_end) break;

                        // Extract rune info
                        char rune_id[32] = {0};
                        char display_name[48] = {0};
                        int samples = 0;
                        bool calibrated = false;

                        // Find "rune_id"
                        const char *id_start = strstr(obj_start, "\"rune_id\"");
                        if (id_start && id_start < obj_end) {
                            id_start = strchr(id_start + 9, '"');
                            if (id_start) {
                                id_start++;
                                const char *id_end = strchr(id_start, '"');
                                if (id_end && (id_end - id_start) < (int)sizeof(rune_id)) {
                                    strncpy(rune_id, id_start, id_end - id_start);
                                }
                            }
                        }

                        // Find "display_name"
                        const char *name_start = strstr(obj_start, "\"display_name\"");
                        if (name_start && name_start < obj_end) {
                            name_start = strchr(name_start + 14, '"');
                            if (name_start) {
                                name_start++;
                                const char *name_end = strchr(name_start, '"');
                                if (name_end && (name_end - name_start) < (int)sizeof(display_name)) {
                                    strncpy(display_name, name_start, name_end - name_start);
                                }
                            }
                        }

                        // Find "samples"
                        const char *samples_start = strstr(obj_start, "\"samples\"");
                        if (samples_start && samples_start < obj_end) {
                            samples_start += 9;
                            while (*samples_start && (*samples_start == ' ' || *samples_start == ':')) samples_start++;
                            samples = atoi(samples_start);
                        }

                        // Find "calibrated"
                        calibrated = json_find_bool_in_range(obj_start, obj_end - obj_start, "calibrated");

                        if (rune_id[0] && display_name[0] && callback) {
                            callback(rune_id, display_name, samples, calibrated);
                            result++;
                        }

                        ptr = obj_end + 1;
                    }
                }
            }
        }
        free(resp_buf);
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return result;
}

// Send calibration sample for a specific rune
int send_rune_calibration(const char *rune_id, int16_t *points_x, int16_t *points_y, int point_count) {
    if (!rune_id || point_count < 3) {
        ESP_LOGW(TAG, "Invalid rune calibration params");
        return -1;
    }

    ESP_LOGI(TAG, "Sending calibration for rune '%s' (%d points)...", rune_id, point_count);

    // Build JSON: {"rune_id": "xxx", "points": [[x,y], ...]}
    char *json_buf = malloc(8192);
    if (!json_buf) {
        ESP_LOGE(TAG, "OOM for rune calibration JSON");
        return -1;
    }

    int offset = 0;
    offset += snprintf(json_buf + offset, 8192 - offset, "{\"rune_id\":\"%s\",\"points\":[", rune_id);

    for (int i = 0; i < point_count; i++) {
        if (i > 0) {
            offset += snprintf(json_buf + offset, 8192 - offset, ",");
        }
        offset += snprintf(json_buf + offset, 8192 - offset, "[%d,%d]", points_x[i], points_y[i]);
    }

    offset += snprintf(json_buf + offset, 8192 - offset, "]}");

    char url[128];
    snprintf(url, sizeof(url), "http://%s:%d/runes/calibrate", SERVER_IP, SERVER_PORT);

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .event_handler = _asset_http_event_handler,
        .timeout_ms = 5000
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, json_buf, strlen(json_buf));

    int result = -1;
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "Rune calibration sent, status %d", status);
        if (status == 200) {
            result = 0;
        }
    } else {
        ESP_LOGE(TAG, "Failed to send rune calibration: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    free(json_buf);
    return result;
}

// ============== PET SYSTEM API ==============

// Pet sprites (dynamically loaded based on active pet)
uint16_t *pet_walk_sprites[8] = {NULL};   // [0-3] = right, [4-7] = left (mirrored)
uint16_t *pet_attack_sprites[8] = {NULL}; // [0-3] = right, [4-7] = left (mirrored)
bool pet_sprites_loaded = false;

// Download pet sprites for the active pet
int fetch_pet_sprites(const char *species, const char *element) {
    ESP_LOGI(TAG, "Fetching pet sprites for %s_%s...", species, element);

    int walk_loaded = 0;
    int attack_loaded = 0;

    // Free any previously loaded sprites
    for (int i = 0; i < 8; i++) {
        if (pet_walk_sprites[i]) {
            free(pet_walk_sprites[i]);
            pet_walk_sprites[i] = NULL;
        }
        if (pet_attack_sprites[i]) {
            free(pet_attack_sprites[i]);
            pet_attack_sprites[i] = NULL;
        }
    }
    pet_sprites_loaded = false;

    // Download 4 walk sprites (right-facing)
    for (int frame = 0; frame < 4; frame++) {
        char filename[128];
        snprintf(filename, sizeof(filename), "pet_%s_%s_w%d.bin", species, element, frame);

        uint16_t *sprite = download_asset(filename, SIZE_SPRITE / 2);
        if (sprite) {
            pet_walk_sprites[frame] = sprite;
            walk_loaded++;
        } else {
            ESP_LOGW(TAG, "Failed to load walk sprite: %s", filename);
        }
    }

    // Download 4 mirrored walk sprites (left-facing)
    for (int frame = 0; frame < 4; frame++) {
        char filename[128];
        snprintf(filename, sizeof(filename), "pet_%s_%s_w%dm.bin", species, element, frame);

        uint16_t *sprite = download_asset(filename, SIZE_SPRITE / 2);
        if (sprite) {
            pet_walk_sprites[frame + 4] = sprite;
            walk_loaded++;
        } else {
            ESP_LOGW(TAG, "Failed to load walk sprite: %s", filename);
        }
    }

    // Download 4 attack sprites (right-facing)
    for (int frame = 0; frame < 4; frame++) {
        char filename[128];
        snprintf(filename, sizeof(filename), "pet_%s_%s_a%d.bin", species, element, frame);

        uint16_t *sprite = download_asset(filename, SIZE_SPRITE / 2);
        if (sprite) {
            pet_attack_sprites[frame] = sprite;
            attack_loaded++;
        } else {
            ESP_LOGW(TAG, "Failed to load attack sprite: %s", filename);
        }
    }

    // Download 4 mirrored attack sprites (left-facing)
    for (int frame = 0; frame < 4; frame++) {
        char filename[128];
        snprintf(filename, sizeof(filename), "pet_%s_%s_a%dm.bin", species, element, frame);

        uint16_t *sprite = download_asset(filename, SIZE_SPRITE / 2);
        if (sprite) {
            pet_attack_sprites[frame + 4] = sprite;
            attack_loaded++;
        } else {
            ESP_LOGW(TAG, "Failed to load attack sprite: %s", filename);
        }
    }

    pet_sprites_loaded = (walk_loaded == 8);
    ESP_LOGI(TAG, "Pet sprites loaded: walk=%d/8, attack=%d/8", walk_loaded, attack_loaded);

    return pet_sprites_loaded ? 0 : -1;
}

// ============== SKILL SPRITES ==============

// Skill sprites (dynamically loaded for active pet's skills)
uint16_t *skill_sprites[MAX_SKILL_SPRITES][SKILL_SPRITE_FRAMES] = {{NULL}};
char skill_sprite_ids[MAX_SKILL_SPRITES][32] = {{0}};
int skill_sprite_count = 0;

void free_skill_sprites(void) {
    for (int i = 0; i < MAX_SKILL_SPRITES; i++) {
        for (int f = 0; f < SKILL_SPRITE_FRAMES; f++) {
            if (skill_sprites[i][f]) {
                free(skill_sprites[i][f]);
                skill_sprites[i][f] = NULL;
            }
        }
        skill_sprite_ids[i][0] = '\0';
    }
    skill_sprite_count = 0;
}

int fetch_skill_sprites(const char *skill_ids[], int count) {
    ESP_LOGI(TAG, "Fetching skill sprites for %d skills...", count);

    // Free any previously loaded
    free_skill_sprites();

    if (count > MAX_SKILL_SPRITES) {
        count = MAX_SKILL_SPRITES;
    }

    int loaded_skills = 0;

    for (int s = 0; s < count; s++) {
        if (!skill_ids[s] || skill_ids[s][0] == '\0') continue;

        int frames_loaded = 0;

        // Download 4 frames for this skill
        for (int f = 0; f < SKILL_SPRITE_FRAMES; f++) {
            char filename[64];
            snprintf(filename, sizeof(filename), "skill_%s_%d.bin", skill_ids[s], f);

            uint16_t *sprite = download_asset(filename, SPELL_FRAME_SIZE / 2);
            if (sprite) {
                skill_sprites[loaded_skills][f] = sprite;
                frames_loaded++;
            } else {
                ESP_LOGW(TAG, "Failed to load skill sprite: %s", filename);
            }
        }

        if (frames_loaded > 0) {
            strncpy(skill_sprite_ids[loaded_skills], skill_ids[s], 31);
            skill_sprite_ids[loaded_skills][31] = '\0';
            loaded_skills++;
            ESP_LOGI(TAG, "  Loaded %s: %d/4 frames", skill_ids[s], frames_loaded);
        }
    }

    skill_sprite_count = loaded_skills;
    ESP_LOGI(TAG, "Skill sprites loaded: %d/%d skills", loaded_skills, count);

    return loaded_skills;
}

uint16_t *get_skill_sprite(const char *skill_id, int frame) {
    if (!skill_id || frame < 0 || frame >= SKILL_SPRITE_FRAMES) {
        return NULL;
    }

    for (int i = 0; i < skill_sprite_count; i++) {
        if (strcmp(skill_sprite_ids[i], skill_id) == 0) {
            return skill_sprites[i][frame];
        }
    }

    return NULL;
}

// Buffer for pet response
static char *pet_response_buf = NULL;
static int pet_response_len = 0;

// Buffer size for pet response (needs to fit skill_details with gesture data)
#define PET_RESPONSE_BUF_SIZE 16384

static esp_err_t pet_http_event_handler(esp_http_client_event_t *evt) {
    switch(evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            if (!pet_response_buf) {
                pet_response_buf = malloc(PET_RESPONSE_BUF_SIZE);
                pet_response_len = 0;
            }
            if (pet_response_buf && pet_response_len + evt->data_len < PET_RESPONSE_BUF_SIZE) {
                memcpy(pet_response_buf + pet_response_len, evt->data, evt->data_len);
                pet_response_len += evt->data_len;
            }
            break;
        default:
            break;
    }
    return ESP_OK;
}

// Fetch active pet from server
int fetch_active_pet(pet_info_callback_t pet_callback, pet_skill_callback_t skill_callback,
                     skill_tree_callback_t tree_callback) {
    ESP_LOGI(TAG, "Fetching active pet from server...");

    // Reset buffer
    if (pet_response_buf) {
        free(pet_response_buf);
        pet_response_buf = NULL;
    }
    pet_response_len = 0;

    char url[128];
    snprintf(url, sizeof(url), "http://%s:%d/player/pet", SERVER_IP, SERVER_PORT);

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .event_handler = pet_http_event_handler,
        .timeout_ms = 10000
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    int result = -1;
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        if (status == 200 && pet_response_buf && pet_response_len > 0) {
            pet_response_buf[pet_response_len] = '\0';
            char *resp_buf = pet_response_buf;

            ESP_LOGI(TAG, "Pet response received (%d bytes)", pet_response_len);

            // Parse pet basic info
            char id[64] = {0};
            char nickname[32] = {0};
            char species[32] = {0};
            char element[32] = {0};
            char rarity[16] = {0};

            json_find_string(resp_buf, "id", id, sizeof(id));
            json_find_string(resp_buf, "nickname", nickname, sizeof(nickname));
            json_find_string(resp_buf, "species", species, sizeof(species));
            json_find_string(resp_buf, "element", element, sizeof(element));
            json_find_string(resp_buf, "rarity", rarity, sizeof(rarity));

            int level = json_find_int(resp_buf, "level");
            int xp = json_find_int(resp_buf, "xp");
            int xp_to_next = json_find_int(resp_buf, "xp_to_next");
            int hp = json_find_int(resp_buf, "hp");
            int max_hp = json_find_int(resp_buf, "max_hp");
            int current_hp = json_find_int(resp_buf, "current_hp");
            int atk = json_find_int(resp_buf, "atk");
            int def = json_find_int(resp_buf, "def");
            int spd = json_find_int(resp_buf, "spd");

            ESP_LOGI(TAG, "Parsed pet: %s (%s/%s) Lv%d XP:%d/%d", nickname, species, element, level, xp, xp_to_next);

            // Call pet callback with basic info
            if (pet_callback && id[0] != '\0') {
                pet_callback(id, nickname, species, element, rarity,
                             level, xp, xp_to_next, hp, max_hp, current_hp, atk, def, spd);
                result = 0;
            }

            // Parse skill_details array (server returns this with full skill info)
            if (skill_callback) {
                const char *skills_start = strstr(resp_buf, "\"skill_details\":");
                if (skills_start) {
                    ESP_LOGI(TAG, "Found skill_details in response");
                    skills_start += 16;  // Skip past "skill_details":
                    // Skip whitespace and opening bracket
                    while (*skills_start == ' ' || *skills_start == '\t') skills_start++;
                    if (*skills_start == '[') skills_start++;  // Skip [
                    ESP_LOGI(TAG, "Parsing skills starting at: %.50s...", skills_start);

                    // Find each skill object - need to handle nested objects
                    int skill_index = 0;
                    const char *skill_ptr = skills_start;
                    int brace_depth = 0;

                    while (skill_ptr && skill_index < 5) {
                        // Find next skill object start
                        const char *obj_start = strchr(skill_ptr, '{');
                        if (!obj_start) break;

                        // Find matching closing brace (handle nested objects like "gesture":{...})
                        const char *scan = obj_start;
                        const char *obj_end = NULL;
                        brace_depth = 0;
                        while (*scan) {
                            if (*scan == '{') brace_depth++;
                            else if (*scan == '}') {
                                brace_depth--;
                                if (brace_depth == 0) {
                                    obj_end = scan;
                                    break;
                                }
                            }
                            scan++;
                        }
                        if (!obj_end) break;

                        // Extract skill object as temporary string
                        int obj_len = obj_end - obj_start + 1;
                        char *skill_json = malloc(obj_len + 1);
                        if (skill_json) {
                            strncpy(skill_json, obj_start, obj_len);
                            skill_json[obj_len] = '\0';

                            // Parse skill fields
                            char skill_id[32] = {0};
                            char name[48] = {0};
                            char type[16] = {0};
                            char effect[32] = {0};

                            json_find_string(skill_json, "id", skill_id, sizeof(skill_id));
                            json_find_string(skill_json, "name", name, sizeof(name));
                            json_find_string(skill_json, "type", type, sizeof(type));
                            json_find_string(skill_json, "effect", effect, sizeof(effect));

                            int power = json_find_int(skill_json, "power");
                            int accuracy = json_find_int(skill_json, "accuracy");
                            int effect_chance = json_find_int(skill_json, "effect_chance");
                            bool has_gesture = json_find_bool(skill_json, "has_gesture");

                            // Parse gesture points if available
                            int8_t gesture_x[32] = {0};
                            int8_t gesture_y[32] = {0};
                            int gesture_count = 0;

                            // Look for "gesture":{"points":[[x,y],[x,y],...]}
                            const char *gesture_start = strstr(skill_json, "\"gesture\"");
                            if (gesture_start && has_gesture) {
                                const char *points_start = strstr(gesture_start, "\"points\"");
                                if (points_start) {
                                    points_start = strchr(points_start, '[');
                                    if (points_start) {
                                        points_start++;  // Skip outer [
                                        // Parse up to 32 points, sampling every 2nd point from 64
                                        int sample_idx = 0;
                                        while (gesture_count < 32 && *points_start && *points_start != ']') {
                                            // Find next [x,y] pair
                                            if (*points_start == '[') {
                                                points_start++;
                                                // Parse x
                                                float x = strtof(points_start, NULL);
                                                // Skip to comma
                                                while (*points_start && *points_start != ',') points_start++;
                                                if (*points_start == ',') points_start++;
                                                // Parse y
                                                float y = strtof(points_start, NULL);
                                                // Skip to ]
                                                while (*points_start && *points_start != ']') points_start++;
                                                if (*points_start == ']') points_start++;

                                                // Sample every 2nd point (64 -> 32)
                                                if (sample_idx % 2 == 0 && gesture_count < 32) {
                                                    // Convert from 0-1 to 0-100 range
                                                    gesture_x[gesture_count] = (int8_t)(x * 100);
                                                    gesture_y[gesture_count] = (int8_t)(y * 100);
                                                    gesture_count++;
                                                }
                                                sample_idx++;
                                            } else {
                                                points_start++;
                                            }
                                        }
                                    }
                                }
                            }

                            if (skill_id[0] != '\0') {
                                skill_callback(skill_index, skill_id, name, type,
                                               power, accuracy, effect, effect_chance, has_gesture,
                                               gesture_x, gesture_y, gesture_count);
                                ESP_LOGI(TAG, "  Skill %d: %s (pwr=%d, gesture=%d pts)",
                                         skill_index, name, power, gesture_count);
                            }

                            free(skill_json);
                        }

                        skill_ptr = obj_end + 1;
                        skill_index++;
                    }
                    ESP_LOGI(TAG, "Parsed %d skills total", skill_index);
                } else {
                    ESP_LOGW(TAG, "skill_details not found in response!");
                }
            }

            // Parse skill_tree array (all available skills for this pet)
            if (tree_callback) {
                const char *tree_start = strstr(resp_buf, "\"skill_tree\":");
                if (tree_start) {
                    ESP_LOGI(TAG, "Found skill_tree in response");
                    tree_start += 13;  // Skip past "skill_tree":
                    while (*tree_start == ' ' || *tree_start == '\t') tree_start++;
                    if (*tree_start == '[') tree_start++;

                    const char *tree_ptr = tree_start;
                    int tree_count = 0;

                    while (tree_ptr && tree_count < 20) {
                        const char *obj_start = strchr(tree_ptr, '{');
                        if (!obj_start) break;

                        // Find matching }
                        const char *obj_end = strchr(obj_start, '}');
                        if (!obj_end) break;

                        int obj_len = obj_end - obj_start + 1;
                        char *tree_json = malloc(obj_len + 1);
                        if (tree_json) {
                            strncpy(tree_json, obj_start, obj_len);
                            tree_json[obj_len] = '\0';

                            char skill_id[32] = {0};
                            char name[48] = {0};
                            char type[16] = {0};

                            json_find_string(tree_json, "id", skill_id, sizeof(skill_id));
                            json_find_string(tree_json, "name", name, sizeof(name));
                            json_find_string(tree_json, "type", type, sizeof(type));

                            int power = json_find_int(tree_json, "power");
                            int unlock_level = json_find_int(tree_json, "unlock_level");
                            bool unlocked = json_find_bool(tree_json, "unlocked");
                            bool has_gesture = json_find_bool(tree_json, "has_gesture");

                            if (skill_id[0] != '\0') {
                                tree_callback(skill_id, name, type, power, unlock_level, unlocked, has_gesture);
                                tree_count++;
                            }

                            free(tree_json);
                        }
                        tree_ptr = obj_end + 1;
                    }
                    ESP_LOGI(TAG, "Parsed %d skill tree entries", tree_count);
                }
            }
        } else {
            ESP_LOGW(TAG, "Pet fetch returned status %d", status);
        }
    } else {
        ESP_LOGE(TAG, "Failed to fetch pet: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);

    // Free response buffer
    if (pet_response_buf) {
        free(pet_response_buf);
        pet_response_buf = NULL;
    }

    return result;
}

// ============== TELEPORT SYSTEM ==============

int fetch_discovered_maps(discovered_map_callback_t callback) {
    ESP_LOGI(TAG, "Fetching discovered maps from server...");

    char url[128];
    snprintf(url, sizeof(url), "http://%s:%d/discovered_maps", SERVER_IP, SERVER_PORT);

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .event_handler = _asset_http_event_handler,
        .timeout_ms = 5000
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    int result = -1;

    esp_err_t err = esp_http_client_open(client, 0);
    if (err == ESP_OK) {
        int content_len = esp_http_client_fetch_headers(client);
        int status = esp_http_client_get_status_code(client);

        if (status == 200) {
            char *resp_buf = malloc(4096);
            if (resp_buf) {
                int read_len = 0;

                // Handle both chunked and fixed-length responses
                if (content_len > 0 && content_len < 4096) {
                    read_len = esp_http_client_read(client, resp_buf, content_len);
                } else {
                    int total = 0;
                    int r;
                    while (total < 4095) {
                        r = esp_http_client_read(client, resp_buf + total, 4095 - total);
                        if (r <= 0) break;
                        total += r;
                    }
                    read_len = total;
                }

                if (read_len > 0) {
                    resp_buf[read_len] = '\0';

                    // Parse count first
                    int count = json_find_int(resp_buf, "count");
                    ESP_LOGI(TAG, "Server reports %d discovered maps", count);

                    // Find the "maps" array
                    const char *maps_start = strstr(resp_buf, "\"maps\":");
                    if (maps_start) {
                        maps_start = strchr(maps_start, '[');
                        if (maps_start) {
                            maps_start++; // Skip '['

                            // Parse each map object
                            int maps_parsed = 0;
                            const char *obj_start = maps_start;

                            while ((obj_start = strchr(obj_start, '{')) != NULL) {
                                const char *obj_end = strchr(obj_start, '}');
                                if (!obj_end) break;

                                // Extract x, y, biome from this object
                                int obj_len = obj_end - obj_start + 1;
                                char *map_json = malloc(obj_len + 1);
                                if (map_json) {
                                    memcpy(map_json, obj_start, obj_len);
                                    map_json[obj_len] = '\0';

                                    int x = json_find_int(map_json, "x");
                                    int y = json_find_int(map_json, "y");
                                    char biome[32] = {0};
                                    json_find_string(map_json, "biome", biome, sizeof(biome));

                                    if (callback && biome[0] != '\0') {
                                        callback(x, y, biome);
                                        maps_parsed++;
                                        ESP_LOGI(TAG, "  Map %d: (%d, %d) - %s", maps_parsed, x, y, biome);
                                    }

                                    free(map_json);
                                }

                                obj_start = obj_end + 1;
                            }

                            result = maps_parsed;
                            ESP_LOGI(TAG, "Parsed %d discovered maps", maps_parsed);
                        }
                    } else {
                        ESP_LOGW(TAG, "No maps array found in response");
                    }
                }

                free(resp_buf);
            }
        } else {
            ESP_LOGW(TAG, "Discovered maps fetch returned status %d", status);
        }
    } else {
        ESP_LOGE(TAG, "Failed to fetch discovered maps: %s", esp_err_to_name(err));
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    return result;
}

// =============================================================================
// BUILDING INTERACTION SYSTEM
// =============================================================================

int fetch_buildings(int x, int y, building_callback_t callback) {
    char url[128];
    snprintf(url, sizeof(url), "http://%s:%d/buildings?x=%d&y=%d", SERVER_IP, SERVER_PORT, x, y);

    esp_http_client_config_t config = {
        .url = url,
        .event_handler = _asset_http_event_handler,
        .timeout_ms = 5000
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);

    int result = -1;

    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        if (status == 200) {
            int content_len = esp_http_client_get_content_length(client);
            if (content_len > 0 && content_len < 8192) {
                char *resp_buf = malloc(content_len + 1);
                if (resp_buf) {
                    esp_http_client_read(client, resp_buf, content_len);
                    resp_buf[content_len] = '\0';

                    // Check is_home_base
                    bool is_home_base = (strstr(resp_buf, "\"is_home_base\":true") != NULL) ||
                                        (strstr(resp_buf, "\"is_home_base\": true") != NULL);
                    result = is_home_base ? 1 : 0;

                    ESP_LOGI(TAG, "Buildings fetch - is_home_base=%d", is_home_base);

                    // Parse buildings array
                    const char *buildings_start = strstr(resp_buf, "\"buildings\":");
                    if (buildings_start && callback) {
                        buildings_start = strchr(buildings_start, '[');
                        if (buildings_start) {
                            buildings_start++;

                            const char *obj_start = buildings_start;
                            while ((obj_start = strchr(obj_start, '{')) != NULL) {
                                const char *obj_end = strchr(obj_start, '}');
                                if (!obj_end) break;

                                int obj_len = obj_end - obj_start + 1;
                                char *bldg_json = malloc(obj_len + 1);
                                if (bldg_json) {
                                    memcpy(bldg_json, obj_start, obj_len);
                                    bldg_json[obj_len] = '\0';

                                    char name[32] = {0};
                                    json_find_string(bldg_json, "name", name, sizeof(name));
                                    int tile_x = json_find_int(bldg_json, "tile_x");
                                    int tile_y = json_find_int(bldg_json, "tile_y");
                                    int width = json_find_int(bldg_json, "width");
                                    int height = json_find_int(bldg_json, "height");
                                    bool interactive = (strstr(bldg_json, "\"interactive\":true") != NULL) ||
                                                       (strstr(bldg_json, "\"interactive\": true") != NULL);

                                    // Parse interaction_zone if present
                                    int zone_x = 0, zone_y = 0, zone_w = 0, zone_h = 0;
                                    const char *zone_str = strstr(bldg_json, "\"interaction_zone\"");
                                    if (zone_str) {
                                        const char *zone_obj = strchr(zone_str, '{');
                                        if (zone_obj) {
                                            zone_x = json_find_int(zone_obj, "x");
                                            zone_y = json_find_int(zone_obj, "y");
                                            zone_w = json_find_int(zone_obj, "w");
                                            zone_h = json_find_int(zone_obj, "h");
                                        }
                                    }

                                    callback(name, tile_x, tile_y, width, height, interactive,
                                             zone_x, zone_y, zone_w, zone_h);

                                    free(bldg_json);
                                }

                                obj_start = obj_end + 1;
                            }
                        }
                    }

                    free(resp_buf);
                }
            }
        }
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    return result;
}

// =============================================================================
// DNA INVENTORY SYSTEM
// =============================================================================

int fetch_dna_inventory(dna_sample_callback_t callback) {
    char url[128];
    snprintf(url, sizeof(url), "http://%s:%d/player/dna", SERVER_IP, SERVER_PORT);

    esp_http_client_config_t config = {
        .url = url,
        .event_handler = _asset_http_event_handler,
        .timeout_ms = 5000
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    int result = -1;

    // Use open/fetch_headers/read pattern (not perform which consumes data internally)
    if (esp_http_client_open(client, 0) == ESP_OK) {
        int content_len = esp_http_client_fetch_headers(client);
        ESP_LOGI(TAG, "DNA: content_len=%d", content_len);

        if (content_len > 0 && content_len < 8192) {
            char *resp_buf = malloc(content_len + 1);
            if (resp_buf) {
                int total_read = 0;
                while (total_read < content_len) {
                    int r = esp_http_client_read(client, resp_buf + total_read, content_len - total_read);
                    if (r <= 0) break;
                    total_read += r;
                }
                resp_buf[total_read] = '\0';
                ESP_LOGI(TAG, "DNA response: %d bytes", total_read);

                int samples_parsed = 0;

                // Parse dna array
                const char *dna_start = strstr(resp_buf, "\"dna\":");
                if (dna_start && callback) {
                    dna_start = strchr(dna_start, '[');
                    if (dna_start) {
                        dna_start++;

                        const char *obj_start = dna_start;
                        while ((obj_start = strchr(obj_start, '{')) != NULL) {
                            const char *obj_end = strchr(obj_start, '}');
                            if (!obj_end) break;

                            int obj_len = obj_end - obj_start + 1;
                            char *sample_json = malloc(obj_len + 1);
                            if (sample_json) {
                                memcpy(sample_json, obj_start, obj_len);
                                sample_json[obj_len] = '\0';

                                char id[64] = {0};
                                char species[32] = {0};
                                char element[16] = {0};
                                char rarity[16] = {0};
                                json_find_string(sample_json, "id", id, sizeof(id));
                                json_find_string(sample_json, "species", species, sizeof(species));
                                json_find_string(sample_json, "element", element, sizeof(element));
                                json_find_string(sample_json, "rarity", rarity, sizeof(rarity));
                                int level = json_find_int(sample_json, "level");

                                ESP_LOGI(TAG, "  DNA: %s/%s Lv%d", species, element, level);
                                callback(id, species, element, rarity, level);
                                samples_parsed++;

                                free(sample_json);
                            }

                            obj_start = obj_end + 1;
                        }
                    }
                }

                result = samples_parsed;
                ESP_LOGI(TAG, "Parsed %d DNA samples", samples_parsed);
                free(resp_buf);
            }
        } else if (content_len == 0) {
            // Empty response means no DNA
            result = 0;
            ESP_LOGI(TAG, "DNA: Empty response (0 samples)");
        } else {
            ESP_LOGW(TAG, "DNA: Invalid content_len=%d", content_len);
        }
    } else {
        ESP_LOGE(TAG, "DNA: Failed to open connection");
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    return result;
}

// =============================================================================
// PET LIST SYSTEM
// =============================================================================

int fetch_pet_list(pet_list_callback_t callback) {
    char url[128];
    snprintf(url, sizeof(url), "http://%s:%d/player/pets", SERVER_IP, SERVER_PORT);

    esp_http_client_config_t config = {
        .url = url,
        .event_handler = _asset_http_event_handler,
        .timeout_ms = 5000
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    int result = -1;

    // Use open/fetch_headers/read pattern (not perform which consumes data internally)
    if (esp_http_client_open(client, 0) == ESP_OK) {
        int content_len = esp_http_client_fetch_headers(client);
        ESP_LOGI(TAG, "Pets: content_len=%d", content_len);

        if (content_len > 0 && content_len < 16384) {
            char *resp_buf = malloc(content_len + 1);
            if (resp_buf) {
                int total_read = 0;
                while (total_read < content_len) {
                    int r = esp_http_client_read(client, resp_buf + total_read, content_len - total_read);
                    if (r <= 0) break;
                    total_read += r;
                }
                resp_buf[total_read] = '\0';
                ESP_LOGI(TAG, "Pets response: %d bytes", total_read);

                int pets_parsed = 0;

                // Parse pets array
                const char *pets_start = strstr(resp_buf, "\"pets\":");
                if (pets_start && callback) {
                    pets_start = strchr(pets_start, '[');
                    if (pets_start) {
                        pets_start++;

                        const char *obj_start = pets_start;
                        while ((obj_start = strchr(obj_start, '{')) != NULL) {
                            const char *obj_end = strchr(obj_start, '}');
                            if (!obj_end) break;

                            int obj_len = obj_end - obj_start + 1;
                            char *pet_json = malloc(obj_len + 1);
                            if (pet_json) {
                                memcpy(pet_json, obj_start, obj_len);
                                pet_json[obj_len] = '\0';

                                char id[64] = {0};
                                char nickname[32] = {0};
                                char species[32] = {0};
                                char element[16] = {0};
                                json_find_string(pet_json, "id", id, sizeof(id));
                                json_find_string(pet_json, "nickname", nickname, sizeof(nickname));
                                json_find_string(pet_json, "species", species, sizeof(species));
                                json_find_string(pet_json, "element", element, sizeof(element));
                                int level = json_find_int(pet_json, "level");
                                int current_hp = json_find_int(pet_json, "current_hp");
                                int max_hp = json_find_int(pet_json, "max_hp");

                                ESP_LOGI(TAG, "  Pet: %s (%s/%s) Lv%d", nickname, species, element, level);
                                callback(id, nickname, species, element, level, current_hp, max_hp);
                                pets_parsed++;

                                free(pet_json);
                            }

                            obj_start = obj_end + 1;
                        }
                    }
                }

                result = pets_parsed;
                ESP_LOGI(TAG, "Parsed %d pets", pets_parsed);
                free(resp_buf);
            }
        } else if (content_len == 0) {
            result = 0;
            ESP_LOGI(TAG, "Pets: Empty response (0 pets)");
        } else {
            ESP_LOGW(TAG, "Pets: Invalid content_len=%d", content_len);
        }
    } else {
        ESP_LOGE(TAG, "Pets: Failed to open connection");
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    return result;
}

int switch_active_pet(const char *pet_id) {
    char url[128];
    snprintf(url, sizeof(url), "http://%s:%d/player/swap_pet", SERVER_IP, SERVER_PORT);

    // Prepare JSON body
    char body[128];
    snprintf(body, sizeof(body), "{\"pet_id\":\"%s\"}", pet_id);

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .event_handler = _asset_http_event_handler,
        .timeout_ms = 5000
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, body, strlen(body));

    esp_err_t err = esp_http_client_perform(client);
    int result = -1;

    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        if (status == 200) {
            result = 0;
            ESP_LOGI(TAG, "Successfully switched active pet to %s", pet_id);
        } else {
            ESP_LOGW(TAG, "Switch pet returned status %d", status);
        }
    } else {
        ESP_LOGE(TAG, "Failed to switch pet: %s", esp_err_to_name(err));
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    return result;
}

int clone_pets(const char *dna_id_1, const char *dna_id_2,
               char *out_species, char *out_element, int *out_level) {
    char url[128];
    snprintf(url, sizeof(url), "http://%s:%d/player/clone", SERVER_IP, SERVER_PORT);

    // Prepare JSON body
    char body[256];
    snprintf(body, sizeof(body), "{\"dna_id_1\":\"%s\",\"dna_id_2\":\"%s\"}", dna_id_1, dna_id_2);

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .event_handler = _asset_http_event_handler,
        .timeout_ms = 120000  // Cloning with AI sprite generation can take up to 2 minutes
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "Content-Type", "application/json");

    int result = -1;

    // Use open/write/fetch_headers/read pattern for POST
    if (esp_http_client_open(client, strlen(body)) == ESP_OK) {
        // Write request body
        esp_http_client_write(client, body, strlen(body));

        // Fetch response headers
        int content_len = esp_http_client_fetch_headers(client);
        int status = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "Clone response: status=%d, content_len=%d", status, content_len);

        if (status == 200 && content_len > 0 && content_len < 2048) {
            char *resp_buf = malloc(content_len + 1);
            if (resp_buf) {
                int total_read = 0;
                while (total_read < content_len) {
                    int r = esp_http_client_read(client, resp_buf + total_read, content_len - total_read);
                    if (r <= 0) break;
                    total_read += r;
                }
                resp_buf[total_read] = '\0';

                // Parse response for new pet info
                // Look for "pet" object
                const char *pet_start = strstr(resp_buf, "\"pet\"");
                if (pet_start) {
                    pet_start = strchr(pet_start, '{');
                    if (pet_start) {
                        json_find_string(pet_start, "species", out_species, 32);
                        json_find_string(pet_start, "element", out_element, 16);
                        *out_level = json_find_int(pet_start, "level");
                        result = 0;
                        ESP_LOGI(TAG, "Cloning complete: %s/%s Lv%d", out_species, out_element, *out_level);
                    }
                }

                free(resp_buf);
            }
        } else {
            ESP_LOGW(TAG, "Clone returned status %d", status);
        }
    } else {
        ESP_LOGE(TAG, "Failed to open clone connection");
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    return result;
}

int send_battle_end(bool player_won, const char *enemy_species, const char *enemy_element,
                    int enemy_level, char *out_dna_species, char *out_dna_element, int *out_xp_gained) {
    char url[128];
    snprintf(url, sizeof(url), "http://%s:%d/battle/end", SERVER_IP, SERVER_PORT);

    // Prepare JSON body
    char body[256];
    snprintf(body, sizeof(body),
             "{\"player_won\":%s,\"enemy_species\":\"%s\",\"enemy_element\":\"%s\",\"enemy_level\":%d}",
             player_won ? "true" : "false", enemy_species, enemy_element, enemy_level);

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .event_handler = _asset_http_event_handler,
        .timeout_ms = 5000
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, body, strlen(body));

    esp_err_t err = esp_http_client_perform(client);
    int result = -1;

    // Initialize outputs
    if (out_dna_species) out_dna_species[0] = '\0';
    if (out_dna_element) out_dna_element[0] = '\0';
    if (out_xp_gained) *out_xp_gained = 0;

    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        if (status == 200) {
            int content_len = esp_http_client_get_content_length(client);
            if (content_len > 0 && content_len < 2048) {
                char *resp_buf = malloc(content_len + 1);
                if (resp_buf) {
                    esp_http_client_read(client, resp_buf, content_len);
                    resp_buf[content_len] = '\0';

                    // Parse rewards
                    const char *rewards = strstr(resp_buf, "\"rewards\"");
                    if (rewards) {
                        // Parse XP
                        if (out_xp_gained) {
                            *out_xp_gained = json_find_int(rewards, "xp");
                        }

                        // Parse DNA info if present
                        const char *dna = strstr(rewards, "\"dna\"");
                        if (dna && out_dna_species && out_dna_element) {
                            const char *dna_obj = strchr(dna, '{');
                            if (dna_obj) {
                                json_find_string(dna_obj, "species", out_dna_species, 16);
                                json_find_string(dna_obj, "element", out_dna_element, 16);
                            }
                        }
                    }

                    result = 0;
                    ESP_LOGI(TAG, "Battle end reported. DNA: %s/%s, XP: %d",
                             out_dna_species ? out_dna_species : "none",
                             out_dna_element ? out_dna_element : "none",
                             out_xp_gained ? *out_xp_gained : 0);

                    free(resp_buf);
                }
            } else {
                result = 0; // No body is OK for battle end
            }
        } else {
            ESP_LOGW(TAG, "Battle end returned status %d", status);
        }
    } else {
        ESP_LOGE(TAG, "Failed to send battle end: %s", esp_err_to_name(err));
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    return result;
}

// =============================================================================
// RUNE BATTLE API IMPLEMENTATIONS
// =============================================================================

// Fetch available runes for battle based on pet species/element/level
int fetch_available_runes(const char *species, const char *element, int level,
                          available_rune_callback_t callback) {
    ESP_LOGI(TAG, "Fetching available runes for %s/%s Lv%d", species, element, level);

    char url[128];
    snprintf(url, sizeof(url), "http://%s:%d/runes/available", SERVER_IP, SERVER_PORT);

    // Build JSON body
    char body[256];
    snprintf(body, sizeof(body), "{\"species\":\"%s\",\"element\":\"%s\",\"level\":%d}",
             species, element, level);

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 5000
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, body, strlen(body));

    int result = -1;

    if (esp_http_client_open(client, strlen(body)) == ESP_OK) {
        esp_http_client_write(client, body, strlen(body));
        int content_len = esp_http_client_fetch_headers(client);
        int status = esp_http_client_get_status_code(client);

        ESP_LOGI(TAG, "Runes available: status=%d, content_len=%d", status, content_len);

        // Handle chunked encoding or missing Content-Length
        if (status == 200) {
            // If content_len is -1 (chunked) or 0, use a reasonable buffer
            int read_len = (content_len > 0 && content_len < 8192) ? content_len : 4096;
            char *resp = malloc(read_len + 1);
            if (resp) {
                int bytes_read = esp_http_client_read(client, resp, read_len);
                if (bytes_read > 0) {
                    resp[bytes_read] = '\0';
                    ESP_LOGI(TAG, "Runes response (%d bytes): %.100s...", bytes_read, resp);

                    // Parse runes array
                    const char *runes_arr = strstr(resp, "\"runes\"");
                    if (runes_arr) {
                        runes_arr = strchr(runes_arr, '[');
                        if (runes_arr) {
                            runes_arr++;
                            result = 0;

                            // Parse each rune object
                            const char *obj_start = strchr(runes_arr, '{');
                            while (obj_start) {
                                const char *obj_end = strchr(obj_start, '}');
                                if (!obj_end) break;

                                char rune_id[32] = {0};
                                char rune_name[32] = {0};

                                json_find_string(obj_start, "id", rune_id, sizeof(rune_id));
                                json_find_string(obj_start, "name", rune_name, sizeof(rune_name));

                                if (rune_id[0] && callback) {
                                    callback(rune_id, rune_name);
                                    result++;
                                }

                                obj_start = strchr(obj_end + 1, '{');
                            }
                        }
                    }
                } else {
                    ESP_LOGW(TAG, "Runes: read returned %d bytes", bytes_read);
                }
                free(resp);
            }
        }
    } else {
        ESP_LOGE(TAG, "Runes: failed to open HTTP connection");
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    ESP_LOGI(TAG, "Fetched %d available runes", result);
    return result;
}

// Recognize a gesture against available runes
int recognize_rune_gesture(int16_t *points_x, int16_t *points_y, int point_count,
                           const char *available_runes[], int rune_count,
                           char *out_rune_id, char *out_rune_name,
                           int *out_accuracy, int *out_power, bool *out_chain_bonus) {
    ESP_LOGI(TAG, "Recognizing rune gesture with %d points against %d runes", point_count, rune_count);

    // Initialize outputs
    if (out_rune_id) out_rune_id[0] = '\0';
    if (out_rune_name) out_rune_name[0] = '\0';
    if (out_accuracy) *out_accuracy = 0;
    if (out_power) *out_power = 0;
    if (out_chain_bonus) *out_chain_bonus = false;

    if (point_count < 5) {
        ESP_LOGW(TAG, "Not enough points for recognition");
        return -1;
    }

    // Build JSON body
    // Format: {"points": [[x,y], ...], "available_runes": ["rune1", "rune2", ...]}
    char *json_buf = malloc(16384);
    if (!json_buf) {
        ESP_LOGE(TAG, "OOM for rune recognition JSON");
        return -1;
    }

    int offset = 0;
    offset += snprintf(json_buf + offset, 16384 - offset, "{\"points\":[");

    for (int i = 0; i < point_count; i++) {
        if (i > 0) offset += snprintf(json_buf + offset, 16384 - offset, ",");
        offset += snprintf(json_buf + offset, 16384 - offset, "[%d,%d]", points_x[i], points_y[i]);
    }

    offset += snprintf(json_buf + offset, 16384 - offset, "],\"available_runes\":[");

    for (int i = 0; i < rune_count; i++) {
        if (i > 0) offset += snprintf(json_buf + offset, 16384 - offset, ",");
        offset += snprintf(json_buf + offset, 16384 - offset, "\"%s\"", available_runes[i]);
    }

    offset += snprintf(json_buf + offset, 16384 - offset, "]}");

    char url[128];
    snprintf(url, sizeof(url), "http://%s:%d/runes/recognize", SERVER_IP, SERVER_PORT);

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 5000
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, json_buf, strlen(json_buf));

    int result = -1;

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        int content_len = esp_http_client_get_content_length(client);

        if (status == 200 && content_len > 0 && content_len < 2048) {
            char *resp = malloc(content_len + 1);
            if (resp) {
                esp_http_client_read(client, resp, content_len);
                resp[content_len] = '\0';

                // Parse response
                char rune_id[32] = {0};
                char rune_name[32] = {0};
                json_find_string(resp, "rune_id", rune_id, sizeof(rune_id));
                json_find_string(resp, "name", rune_name, sizeof(rune_name));

                if (rune_id[0]) {
                    if (out_rune_id) strncpy(out_rune_id, rune_id, 31);
                    if (out_rune_name) strncpy(out_rune_name, rune_name, 31);
                    if (out_accuracy) *out_accuracy = json_find_int(resp, "accuracy");
                    if (out_power) *out_power = json_find_int(resp, "power");
                    if (out_chain_bonus) *out_chain_bonus = json_find_bool_in_range(resp, content_len, "chain_bonus");

                    ESP_LOGI(TAG, "Recognized rune: %s (%s) acc=%d pow=%d",
                             rune_id, rune_name,
                             out_accuracy ? *out_accuracy : 0,
                             out_power ? *out_power : 0);
                    result = 0;
                } else {
                    ESP_LOGW(TAG, "No rune recognized");
                }
                free(resp);
            }
        }
    } else {
        ESP_LOGE(TAG, "Failed to recognize rune: %s", esp_err_to_name(err));
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    free(json_buf);

    return result;
}

// Execute player's rune chain on server
int execute_rune_chain(const char *rune_ids[], int chain_count,
                       const char *enemy_species, const char *enemy_element, int enemy_level,
                       int *out_enemy_hp_after) {
    ESP_LOGI(TAG, "Executing rune chain with %d runes", chain_count);

    if (out_enemy_hp_after) *out_enemy_hp_after = 0;

    // Build JSON
    char *json_buf = malloc(2048);
    if (!json_buf) return -1;

    int offset = 0;
    offset += snprintf(json_buf + offset, 2048 - offset,
                       "{\"chain\":[");

    for (int i = 0; i < chain_count; i++) {
        if (i > 0) offset += snprintf(json_buf + offset, 2048 - offset, ",");
        offset += snprintf(json_buf + offset, 2048 - offset, "\"%s\"", rune_ids[i]);
    }

    offset += snprintf(json_buf + offset, 2048 - offset,
                       "],\"enemy_species\":\"%s\",\"enemy_element\":\"%s\",\"enemy_level\":%d}",
                       enemy_species, enemy_element, enemy_level);

    char url[128];
    snprintf(url, sizeof(url), "http://%s:%d/runes/execute_chain", SERVER_IP, SERVER_PORT);

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 5000
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, json_buf, strlen(json_buf));

    int result = -1;

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        int content_len = esp_http_client_get_content_length(client);

        if (status == 200 && content_len > 0 && content_len < 1024) {
            char *resp = malloc(content_len + 1);
            if (resp) {
                esp_http_client_read(client, resp, content_len);
                resp[content_len] = '\0';

                result = json_find_int(resp, "total_damage");
                if (out_enemy_hp_after) {
                    *out_enemy_hp_after = json_find_int(resp, "enemy_hp_after");
                }

                ESP_LOGI(TAG, "Chain executed: %d damage, enemy HP after: %d",
                         result, out_enemy_hp_after ? *out_enemy_hp_after : 0);
                free(resp);
            }
        }
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    free(json_buf);

    return result;
}

// Get enemy's rune chain from server AI
int fetch_enemy_rune_chain(const char *enemy_species, const char *enemy_element, int enemy_level,
                           int player_hp, int enemy_hp,
                           enemy_chain_callback_t callback, int *out_chain_count) {
    ESP_LOGI(TAG, "Fetching enemy chain for %s/%s Lv%d", enemy_species, enemy_element, enemy_level);

    if (out_chain_count) *out_chain_count = 0;

    char body[256];
    snprintf(body, sizeof(body),
             "{\"enemy_species\":\"%s\",\"enemy_element\":\"%s\",\"enemy_level\":%d,"
             "\"player_hp\":%d,\"enemy_hp\":%d}",
             enemy_species, enemy_element, enemy_level, player_hp, enemy_hp);

    char url[128];
    snprintf(url, sizeof(url), "http://%s:%d/runes/enemy_chain", SERVER_IP, SERVER_PORT);

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 5000
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, body, strlen(body));

    int result = -1;

    if (esp_http_client_open(client, strlen(body)) == ESP_OK) {
        esp_http_client_write(client, body, strlen(body));
        int content_len = esp_http_client_fetch_headers(client);
        int status = esp_http_client_get_status_code(client);

        if (status == 200 && content_len > 0 && content_len < 4096) {
            char *resp = malloc(content_len + 1);
            if (resp) {
                esp_http_client_read(client, resp, content_len);
                resp[content_len] = '\0';

                int total_damage = json_find_int(resp, "total_damage");

                // Parse chain array
                const char *chain_arr = strstr(resp, "\"chain\"");
                if (chain_arr) {
                    chain_arr = strchr(chain_arr, '[');
                    if (chain_arr) {
                        chain_arr++;
                        int idx = 0;

                        const char *obj_start = strchr(chain_arr, '{');
                        while (obj_start) {
                            const char *obj_end = strchr(obj_start, '}');
                            if (!obj_end) break;

                            char rune_id[32] = {0};
                            char rune_name[32] = {0};
                            int power = 0;

                            json_find_string(obj_start, "id", rune_id, sizeof(rune_id));
                            json_find_string(obj_start, "name", rune_name, sizeof(rune_name));
                            power = json_find_int(obj_start, "power");

                            if (rune_id[0] && callback) {
                                callback(idx, rune_id, rune_name, power);
                                idx++;
                            }

                            obj_start = strchr(obj_end + 1, '{');
                        }

                        if (out_chain_count) *out_chain_count = idx;
                        result = total_damage;
                    }
                }
                free(resp);
            }
        }
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    ESP_LOGI(TAG, "Enemy chain: %d runes, %d total damage",
             out_chain_count ? *out_chain_count : 0, result);
    return result;
}

// Fetch full rune tree for a pet (including locked runes)
int fetch_pet_rune_tree(const char *species, const char *element, int level,
                        rune_tree_callback_t callback) {
    ESP_LOGI(TAG, "Fetching rune tree for %s/%s Lv%d", species, element, level);

    char url[128];
    snprintf(url, sizeof(url), "http://%s:%d/runes/pet_tree", SERVER_IP, SERVER_PORT);

    char body[256];
    snprintf(body, sizeof(body), "{\"species\":\"%s\",\"element\":\"%s\",\"level\":%d}",
             species, element, level);

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 5000
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, body, strlen(body));

    int result = -1;

    if (esp_http_client_open(client, strlen(body)) == ESP_OK) {
        esp_http_client_write(client, body, strlen(body));
        int content_len = esp_http_client_fetch_headers(client);
        int status = esp_http_client_get_status_code(client);

        if (status == 200 && content_len > 0 && content_len < 16384) {
            char *resp = malloc(content_len + 1);
            if (resp) {
                esp_http_client_read(client, resp, content_len);
                resp[content_len] = '\0';

                // Parse runes array
                const char *runes_arr = strstr(resp, "\"runes\"");
                if (runes_arr) {
                    runes_arr = strchr(runes_arr, '[');
                    if (runes_arr) {
                        runes_arr++;
                        result = 0;

                        // Parse each rune object
                        const char *obj_start = strchr(runes_arr, '{');
                        while (obj_start) {
                            const char *obj_end = strchr(obj_start, '}');
                            if (!obj_end) break;

                            char rune_id[32] = {0};
                            char rune_name[32] = {0};
                            char source[16] = {0};
                            char rune_element[16] = {0};
                            int power = 0;
                            int unlock_level = 0;
                            bool unlocked = false;
                            bool calibrated = false;

                            json_find_string(obj_start, "id", rune_id, sizeof(rune_id));
                            json_find_string(obj_start, "name", rune_name, sizeof(rune_name));
                            json_find_string(obj_start, "source", source, sizeof(source));
                            json_find_string(obj_start, "element", rune_element, sizeof(rune_element));
                            power = json_find_int(obj_start, "power");
                            unlock_level = json_find_int(obj_start, "unlock_level");
                            unlocked = json_find_bool_in_range(obj_start, obj_end - obj_start, "unlocked");
                            calibrated = json_find_bool_in_range(obj_start, obj_end - obj_start, "calibrated");

                            if (rune_id[0] && callback) {
                                callback(rune_id, rune_name, source, rune_element,
                                         power, unlock_level, unlocked, calibrated);
                                result++;
                            }

                            obj_start = strchr(obj_end + 1, '{');
                        }
                    }
                }
                free(resp);
            }
        }
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    ESP_LOGI(TAG, "Fetched %d runes in tree", result);
    return result;
}
