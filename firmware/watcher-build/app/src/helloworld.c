// Uncomment this line to enable video stream mode instead of the game
#define VIDEO_STREAM_MODE

#include <stdio.h>
#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "sensecap-watcher.h"
#include "wifi_config.h"
#include "driver/i2s_std.h"
#include "esp_codec_dev.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch_spd2010.h"
#include "generated_assets.h"
#include "game_logic.h"
#include "iot_knob.h"
#include "font_8x8.h"
#include <dirent.h>
#include <sys/stat.h>
#include "esp_http_client.h"

#ifdef VIDEO_STREAM_MODE
#include "video_stream.h"
#endif

static const char *TAG = "YoyoGochi";

// Wi-Fi
static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

// Server Config
#define SERVER_IP "192.168.0.191"
#define SERVER_ASSET_PORT 8082

// Audio
#define SERVER_AUDIO_PORT 8081

// Display
#define SCR_W 412
#define SCR_H 412
#define FOX_W 104
#define FOX_H 88
#define CHROMA_KEY 0xE007

// Colors (RGB565)
#define COL_BLACK 0x0000
#define COL_WHITE 0xFFFF
#define COL_RED   0xF800
#define COL_GREEN 0x07E0
#define COL_BLUE  0x001F
#define COL_GRAY  0x8410
#define COL_DARK  0x2104
#define COL_YELLOW 0xFFE0
#define COL_CYAN   0x07FF

// Forward declarations
static void discovered_map_callback(int x, int y, const char *biome);
static void building_callback(const char *name, int tile_x, int tile_y,
                              int width, int height, bool interactive,
                              int zone_x, int zone_y, int zone_w, int zone_h);
static void dna_sample_callback(const char *id, const char *species,
                                const char *element, const char *rarity, int level);
static void pet_list_callback(const char *id, const char *nickname,
                              const char *species, const char *element,
                              int level, int current_hp, int max_hp);
static void uncalibrated_skill_callback(const char *skill_id, const char *name);
static void rune_calibration_callback(const char *rune_id, const char *display_name, int samples, bool calibrated);
static void available_rune_callback(const char *rune_id, const char *name);
static void enemy_chain_callback(int index, const char *rune_id, const char *name, int power);
static void rune_tree_callback(const char *rune_id, const char *name, const char *source,
                               const char *element, int power, int unlock_level,
                               bool unlocked, bool calibrated);
static void on_pet_info(const char *id, const char *nickname, const char *species,
                        const char *element, const char *rarity,
                        int level, int xp, int xp_to_next, int hp, int max_hp, int current_hp,
                        int atk, int def, int spd);
static void on_pet_skill(int skill_index, const char *id, const char *name, const char *type,
                         int power, int accuracy, const char *effect, int effect_chance, bool has_gesture,
                         const int8_t *gesture_x, const int8_t *gesture_y, int gesture_count);
static void on_skill_tree_entry(const char *id, const char *name, const char *type,
                                int power, int unlock_level, bool unlocked, bool has_gesture);
// RGB565 format: RRRRRGGGGGGBBBBB
// Gold (255, 200, 0) = 11111 110010 00000 = 0xFE40
#define COL_GOLD       0xFE40  // Bright golden yellow
#define COL_GOLD_LIGHT 0xFFE0  // Lighter gold/yellow
#define COL_GOLD_DARK  0xDC00  // Darker orange-gold 

// Global Objects
static uint16_t *frame_buf = NULL; 
static esp_lcd_touch_handle_t tp_handle = NULL; 
static knob_handle_t knob_handle = NULL; 
static GameState g_game_state;

// Assets (Dynamic Pointers)
uint16_t *sprites[16] = { NULL };

static void init_asset_arrays() {
    sprites[0] = fox_w1_data;
    sprites[1] = fox_w2_data;
    sprites[2] = fox_w3_data;
    sprites[3] = fox_w4_data;
    sprites[4] = fox_w5_data;
    sprites[5] = fox_w6_data;
    sprites[6] = fox_w7_data;
    sprites[7] = fox_w8_data;
    sprites[8] = fox_w1m_data;
    sprites[9] = fox_w2m_data;
    sprites[10] = fox_w3m_data;
    sprites[11] = fox_w4m_data;
    sprites[12] = fox_w5m_data;
    sprites[13] = fox_w6m_data;
    sprites[14] = fox_w7m_data;
    sprites[15] = fox_w8m_data;
}

// --- Helper: Manual Graphics ---

static void draw_rect(int x, int y, int w, int h, uint16_t color) {
    for (int i=0; i<h; i++) {
        for (int j=0; j<w; j++) {
            int dx = x + j;
            int dy = y + i;
            if (dx >= 0 && dx < SCR_W && dy >= 0 && dy < SCR_H) {
                frame_buf[dy * SCR_W + dx] = color;
            }
        }
    }
}

static void draw_char(int x, int y, char c, uint16_t color) {
    if (c < ' ' || c > '~') return;
    int idx = c - ' ';
    const uint8_t *bitmap = font_8x8_basic[idx];
    
    // Scale 8x8 to 16x16 (2x zoom)
    for (int i=0; i<8; i++) {
        for (int j=0; j<8; j++) {
            if (bitmap[i] & (1 << (7-j))) { 
                int px = x + j*2; 
                int py = y + i*2;
                if (px < SCR_W-1 && py < SCR_H-1) {
                    frame_buf[py * SCR_W + px] = color;
                    frame_buf[py * SCR_W + px + 1] = color;
                    frame_buf[(py+1) * SCR_W + px] = color;
                    frame_buf[(py+1) * SCR_W + px + 1] = color;
                }
            }
        }
    }
}

static void draw_string(int x, int y, const char *str, uint16_t color) {
    while (*str) {
        draw_char(x, y, *str, color);
        x += 16;
        str++;
    }
}

// Helper: Check if point index is the start of a new stroke
static bool is_stroke_start(GestureState *gest, int point_index) {
    for (int s = 0; s < gest->stroke_count; s++) {
        if (gest->stroke_starts[s] == point_index) {
            return true;
        }
    }
    return false;
}

// Draw golden particle trail for gesture - thick graffiti style
// Supports multi-stroke gestures (doesn't connect between strokes)
static void draw_gesture_trail(GestureState *gest) {
    if (gest->point_count < 1) return;

    int brush_radius = 12;  // Much thicker brush

    // Draw each point as a big pixelated golden blob
    for (int i = 0; i < gest->point_count; i++) {
        int cx = gest->points[i].x;
        int cy = gest->points[i].y;

        for (int dy = -brush_radius; dy <= brush_radius; dy++) {
            for (int dx = -brush_radius; dx <= brush_radius; dx++) {
                int px = cx + dx;
                int py = cy + dy;

                if (px < 0 || px >= SCR_W || py < 0 || py >= SCR_H) continue;

                int dist_sq = dx * dx + dy * dy;
                int r_sq = brush_radius * brush_radius;

                if (dist_sq <= r_sq) {
                    uint16_t col;
                    if (dist_sq <= r_sq / 9) {
                        col = COL_GOLD_LIGHT;  // Bright center
                    } else if (dist_sq <= r_sq / 2) {
                        col = COL_GOLD;        // Main gold
                    } else {
                        col = COL_GOLD_DARK;   // Darker edge
                    }
                    frame_buf[py * SCR_W + px] = col;
                }
            }
        }
    }

    // Draw thick connecting lines between points (but not across strokes!)
    for (int i = 1; i < gest->point_count; i++) {
        // Skip if this point is the start of a new stroke
        if (is_stroke_start(gest, i)) {
            continue;  // Don't draw line from previous stroke to this one
        }

        int x0 = gest->points[i - 1].x;
        int y0 = gest->points[i - 1].y;
        int x1 = gest->points[i].x;
        int y1 = gest->points[i].y;

        // Bresenham line with thick brush at each point
        int dx = abs(x1 - x0);
        int dy = abs(y1 - y0);
        int sx = (x0 < x1) ? 1 : -1;
        int sy = (y0 < y1) ? 1 : -1;
        int err = dx - dy;

        int line_radius = 10;  // Thick line

        while (1) {
            // Draw filled circle at this line point
            for (int oy = -line_radius; oy <= line_radius; oy++) {
                for (int ox = -line_radius; ox <= line_radius; ox++) {
                    if (ox * ox + oy * oy <= line_radius * line_radius) {
                        int px = x0 + ox;
                        int py = y0 + oy;
                        if (px >= 0 && px < SCR_W && py >= 0 && py < SCR_H) {
                            frame_buf[py * SCR_W + px] = COL_GOLD;
                        }
                    }
                }
            }

            if (x0 == x1 && y0 == y1) break;

            int e2 = 2 * err;
            if (e2 > -dy) { err -= dy; x0 += sx; }
            if (e2 < dx) { err += dx; y0 += sy; }
        }
    }
}

// --- Battle Scene Drawing ---
static const char* spell_names[] = {"METEOR", "WAVE", "STORM", "NOVA", "ORB", "PUNCH"};

// Draw a spell/skill animation frame at the specified position
// First tries to use skill-specific sprite, falls back to generic spell animation
static void draw_skill_effect(const char *skill_id, int spell_type, int frame, int center_x, int center_y) {
    uint16_t *sprite = NULL;

    // Try to get skill-specific sprite first
    if (skill_id && skill_id[0] != '\0') {
        sprite = get_skill_sprite(skill_id, frame);
    }

    // Fall back to generic spell animation if no skill sprite
    if (!sprite) {
        if (spell_type >= 0 && spell_type < SPELL_TYPE_COUNT && frame >= 0 && frame < SPELL_FRAME_COUNT) {
            sprite = spell_frames[spell_type][frame];
        }
    }

    if (!sprite) return;

    // Draw centered at position
    int start_x = center_x - SPELL_FRAME_W / 2;
    int start_y = center_y - SPELL_FRAME_H / 2;

    for (int y = 0; y < SPELL_FRAME_H; y++) {
        int dy = start_y + y;
        if (dy < 0 || dy >= SCR_H) continue;

        for (int x = 0; x < SPELL_FRAME_W; x++) {
            int dx = start_x + x;
            if (dx < 0 || dx >= SCR_W) continue;

            uint16_t pixel = sprite[y * SPELL_FRAME_W + x];
            if (pixel != CHROMA_KEY) {
                frame_buf[dy * SCR_W + dx] = pixel;
            }
        }
    }
}

// Legacy wrapper for enemy spells (no skill-specific sprites yet)
static void draw_spell_effect(int spell_type, int frame, int center_x, int center_y) {
    draw_skill_effect(NULL, spell_type, frame, center_x, center_y);
}

static void draw_hp_bar(int x, int y, int w, int h, int hp, int max_hp, uint16_t color) {
    // Background
    draw_rect(x, y, w, h, COL_DARK);
    // HP fill
    int fill_w = (hp * (w - 4)) / max_hp;
    if (fill_w > 0) {
        draw_rect(x + 2, y + 2, fill_w, h - 4, color);
    }
    // Border
    for (int i = 0; i < w; i++) {
        frame_buf[y * SCR_W + x + i] = COL_WHITE;
        frame_buf[(y + h - 1) * SCR_W + x + i] = COL_WHITE;
    }
    for (int i = 0; i < h; i++) {
        frame_buf[(y + i) * SCR_W + x] = COL_WHITE;
        frame_buf[(y + i) * SCR_W + x + w - 1] = COL_WHITE;
    }
}

static void draw_battle_scene(GameState *state, uint16_t *player_sprite, Mob *enemy_mob) {
    BattleState *b = &state->battle;

    // Dark background
    for (int i = 0; i < SCR_W * SCR_H; i++) {
        frame_buf[i] = COL_DARK;
    }

    // Draw battle arena gradient (simple)
    for (int y = 200; y < SCR_H; y++) {
        uint16_t ground_col = 0x2945;  // Darker ground
        for (int x = 0; x < SCR_W; x++) {
            frame_buf[y * SCR_W + x] = ground_col;
        }
    }

    // Determine which sprite to use for player (attack or walk)
    uint16_t *player_draw_sprite = player_sprite;
    if (b->phase == BATTLE_PHASE_PLAYER_CAST && pet_attack_sprites[0]) {
        // Calculate attack animation frame based on timer (45 frames total, 4 animation frames)
        int anim_frame = (45 - b->phase_timer) * 4 / 45;
        if (anim_frame >= 4) anim_frame = 3;
        if (pet_attack_sprites[anim_frame]) {
            player_draw_sprite = pet_attack_sprites[anim_frame];
        }
    }

    // Draw player sprite (left side, lower on screen)
    if (player_draw_sprite) {
        int px = 50;
        int py = 280 - FOX_H;
        for (int y = 0; y < FOX_H; y++) {
            for (int x = 0; x < FOX_W; x++) {
                uint16_t p = player_draw_sprite[y * FOX_W + x];
                if (p != CHROMA_KEY) {
                    int dx = px + x;
                    int dy = py + y;
                    if (dx >= 0 && dx < SCR_W && dy >= 0 && dy < SCR_H) {
                        frame_buf[dy * SCR_W + dx] = p;
                    }
                }
            }
        }
    }

    // Determine which sprite to use for enemy (attack or idle)
    // For enemy, we use their sprite_data but during ENEMY_CAST we could animate if we had attack sprites
    // For now, just use idle sprite for enemy (we don't load enemy attack sprites separately)
    uint16_t *enemy_draw_sprite = (enemy_mob && enemy_mob->sprite_data) ? enemy_mob->sprite_data : NULL;

    // Draw enemy sprite (right side, mirrored, lower on screen)
    if (enemy_draw_sprite) {
        int ex = SCR_W - 50 - FOX_W;
        int ey = 280 - FOX_H;
        for (int y = 0; y < FOX_H; y++) {
            for (int x = 0; x < FOX_W; x++) {
                uint16_t p = enemy_draw_sprite[y * FOX_W + (FOX_W - 1 - x)];  // Mirror
                if (p != CHROMA_KEY) {
                    int dx = ex + x;
                    int dy = ey + y;
                    if (dx >= 0 && dx < SCR_W && dy >= 0 && dy < SCR_H) {
                        frame_buf[dy * SCR_W + dx] = p;
                    }
                }
            }
        }
    }

    // Draw HP bars (positioned above the pets at y=280-FOX_H)
    // Player HP bar on left side
    int player_hp_y = 280 - FOX_H - 50;  // 50 pixels above pet
    draw_hp_bar(20, player_hp_y, 150, 16, b->player_hp, b->player_max_hp, COL_GREEN);
    draw_string(20, player_hp_y + 20, "YOU", COL_WHITE);

    // Enemy HP bar on right side
    int enemy_hp_y = 280 - FOX_H - 50;
    draw_hp_bar(SCR_W - 170, enemy_hp_y, 150, 16, b->enemy_hp, b->enemy_max_hp, COL_RED);
    draw_string(SCR_W - 170, enemy_hp_y + 20, "ENEMY", COL_WHITE);

    // Draw phase-specific UI (text at bottom, below pets)
    int text_y = 320;  // Below pets (which end at y=280)
    int text_y2 = 350;
    int text_y3 = 380;

    switch (b->phase) {
        case BATTLE_PHASE_INTRO:
            draw_string(130, text_y, "BATTLE START!", COL_GOLD);
            break;

        case BATTLE_PHASE_PLAYER_TURN:
            draw_string(100, text_y, "DRAW YOUR SPELL!", COL_WHITE);
            {
                char attempts_str[32];
                snprintf(attempts_str, sizeof(attempts_str), "ATTEMPTS: %d/3", b->cast_attempts);
                draw_string(120, text_y2, attempts_str, COL_GRAY);
            }
            break;

        case BATTLE_PHASE_PLAYER_CAST:
            if (b->last_player_spell >= 0 && b->last_player_spell <= SPELL_BASIC) {
                // Show actual skill name if available, otherwise generic spell name
                const char *display_name = (b->last_skill_name[0] != '\0') ? b->last_skill_name : spell_names[b->last_player_spell];
                draw_string(100, text_y, display_name, COL_GOLD);
                // Draw spell animation on enemy (right side)
                // Calculate animation frame based on timer (45 frames total, 4 animation frames)
                int anim_frame = (45 - b->phase_timer) * SPELL_FRAME_COUNT / 45;
                if (anim_frame >= SPELL_FRAME_COUNT) anim_frame = SPELL_FRAME_COUNT - 1;
                // Center spell on enemy position
                int enemy_center_x = SCR_W - 50 - FOX_W / 2;
                int enemy_center_y = 280 - FOX_H / 2;
                draw_skill_effect(b->last_skill_id, b->last_player_spell, anim_frame, enemy_center_x, enemy_center_y);
            }
            break;

        case BATTLE_PHASE_ENEMY_TURN:
            draw_string(120, text_y, "ENEMY TURN...", COL_RED);
            break;

        case BATTLE_PHASE_ENEMY_CAST:
            draw_string(110, text_y, "ENEMY ATTACKS!", COL_RED);
            // Draw spell animation on player (left side)
            if (b->last_enemy_spell >= 0 && b->last_enemy_spell <= SPELL_BASIC) {
                int anim_frame = (45 - b->phase_timer) * SPELL_FRAME_COUNT / 45;
                if (anim_frame >= SPELL_FRAME_COUNT) anim_frame = SPELL_FRAME_COUNT - 1;
                // Center spell on player position
                int player_center_x = 50 + FOX_W / 2;
                int player_center_y = 280 - FOX_H / 2;
                draw_spell_effect(b->last_enemy_spell, anim_frame, player_center_x, player_center_y);
            }
            break;

        case BATTLE_PHASE_WIN:
            draw_string(140, text_y, "YOU WIN!", COL_GOLD);
            draw_string(100, text_y2, "CLICK TO CONTINUE", COL_WHITE);
            break;

        case BATTLE_PHASE_LOSE:
            draw_string(130, text_y, "YOU LOSE!", COL_RED);
            draw_string(100, text_y2, "CLICK TO CONTINUE", COL_WHITE);
            break;
    }
}

// =============================================================================
// RUNE BATTLE SCENE RENDERING
// =============================================================================

static void draw_rune_battle_scene(GameState *state, uint16_t *player_sprite, Mob *enemy_mob) {
    RuneBattleState *rb = &state->rune_battle;

    // Dark background
    for (int i = 0; i < SCR_W * SCR_H; i++) {
        frame_buf[i] = COL_DARK;
    }

    // Draw battle arena gradient
    for (int y = 200; y < SCR_H; y++) {
        uint16_t ground_col = 0x2945;
        for (int x = 0; x < SCR_W; x++) {
            frame_buf[y * SCR_W + x] = ground_col;
        }
    }

    // Draw player sprite (left side)
    if (player_sprite) {
        int px = 50;
        int py = 280 - FOX_H;
        for (int y = 0; y < FOX_H; y++) {
            for (int x = 0; x < FOX_W; x++) {
                uint16_t p = player_sprite[y * FOX_W + x];
                if (p != CHROMA_KEY) {
                    int dx = px + x;
                    int dy = py + y;
                    if (dx >= 0 && dx < SCR_W && dy >= 0 && dy < SCR_H) {
                        frame_buf[dy * SCR_W + dx] = p;
                    }
                }
            }
        }
    }

    // Draw enemy sprite (right side, mirrored)
    uint16_t *enemy_sprite = (enemy_mob && enemy_mob->sprite_data) ? enemy_mob->sprite_data : NULL;
    if (enemy_sprite) {
        int ex = SCR_W - 50 - FOX_W;
        int ey = 280 - FOX_H;
        for (int y = 0; y < FOX_H; y++) {
            for (int x = 0; x < FOX_W; x++) {
                uint16_t p = enemy_sprite[y * FOX_W + (FOX_W - 1 - x)];
                if (p != CHROMA_KEY) {
                    int dx = ex + x;
                    int dy = ey + y;
                    if (dx >= 0 && dx < SCR_W && dy >= 0 && dy < SCR_H) {
                        frame_buf[dy * SCR_W + dx] = p;
                    }
                }
            }
        }
    }

    // HP bars
    int player_hp_y = 280 - FOX_H - 50;
    draw_hp_bar(20, player_hp_y, 150, 16, rb->player_hp, rb->player_max_hp, COL_GREEN);
    draw_string(20, player_hp_y + 20, "YOU", COL_WHITE);

    int enemy_hp_y = 280 - FOX_H - 50;
    draw_hp_bar(SCR_W - 170, enemy_hp_y, 150, 16, rb->enemy_hp, rb->enemy_max_hp, COL_RED);
    char enemy_label[32];
    snprintf(enemy_label, sizeof(enemy_label), "Lv%d", rb->enemy_level);
    draw_string(SCR_W - 170, enemy_hp_y + 20, enemy_label, COL_WHITE);

    // ========== PHASE-SPECIFIC UI ==========
    int text_y = 305;

    switch (rb->phase) {
        case RUNE_PHASE_INTRO:
            draw_string(110, 180, "RUNE BATTLE!", COL_GOLD);
            break;

        case RUNE_PHASE_PLAYER_CHAIN: {
            // Timer bar at top
            int timer_percent = (rb->chain_timer * 100) / rb->chain_timer_max;
            int timer_width = (SCR_W - 40) * timer_percent / 100;
            uint16_t timer_col = (timer_percent > 30) ? COL_GREEN : COL_RED;

            // Timer background
            for (int x = 20; x < SCR_W - 20; x++) {
                for (int y = 5; y < 15; y++) {
                    frame_buf[y * SCR_W + x] = 0x2104;
                }
            }
            // Timer fill
            for (int x = 20; x < 20 + timer_width; x++) {
                for (int y = 5; y < 15; y++) {
                    frame_buf[y * SCR_W + x] = timer_col;
                }
            }

            // Momentum display
            char momentum_str[32];
            snprintf(momentum_str, sizeof(momentum_str), "MOMENTUM: %.1fx", rb->momentum);
            uint16_t mom_col = (rb->momentum >= 2.0f) ? COL_GOLD : COL_WHITE;
            draw_string(130, 20, momentum_str, mom_col);

            // Chain display (show chained runes)
            draw_string(10, text_y, "CHAIN:", COL_GRAY);
            int chain_x = 80;
            for (int i = 0; i < rb->player_chain_count && i < 4; i++) {
                // Show first few letters of rune name
                char short_name[8];
                strncpy(short_name, rb->player_chain[i].name, 6);
                short_name[6] = '\0';
                draw_string(chain_x, text_y, short_name, COL_GOLD);
                chain_x += 60;
            }
            if (rb->player_chain_count > 4) {
                draw_string(chain_x, text_y, "...", COL_GRAY);
            }

            // Instructions
            draw_string(60, text_y + 35, "DRAW RUNES! CLICK TO SEND", COL_WHITE);

            // Recognition feedback
            if (rb->show_recognition_feedback) {
                char feedback[64];
                if (rb->last_recognized_id[0]) {
                    snprintf(feedback, sizeof(feedback), "%s (%d%%)",
                             rb->last_recognized_name, rb->last_accuracy);
                    draw_string(100, text_y + 60, feedback, COL_GREEN);
                } else {
                    draw_string(120, text_y + 60, "NOT RECOGNIZED", COL_RED);
                }
            }
            break;
        }

        case RUNE_PHASE_PLAYER_EXECUTE: {
            // Show which rune is executing
            if (rb->execute_index < rb->player_chain_count) {
                draw_string(120, text_y, rb->player_chain[rb->execute_index].name, COL_GOLD);
            }

            // Show damage
            char dmg_str[32];
            snprintf(dmg_str, sizeof(dmg_str), "TOTAL: %d DMG", rb->total_damage_dealt);
            draw_string(120, text_y + 30, dmg_str, COL_WHITE);
            break;
        }

        case RUNE_PHASE_ENEMY_CHAIN:
            draw_string(100, text_y, "ENEMY BUILDING CHAIN...", COL_RED);
            break;

        case RUNE_PHASE_ENEMY_EXECUTE: {
            // Show enemy's rune
            if (rb->execute_index < rb->enemy_chain_count) {
                char enemy_rune[64];
                snprintf(enemy_rune, sizeof(enemy_rune), "ENEMY: %s",
                         rb->enemy_chain[rb->execute_index].name);
                draw_string(100, text_y, enemy_rune, COL_RED);
            }
            break;
        }

        case RUNE_PHASE_WIN:
            draw_string(140, text_y, "YOU WIN!", COL_GOLD);
            draw_string(100, text_y + 30, "CLICK TO CONTINUE", COL_WHITE);
            break;

        case RUNE_PHASE_LOSE:
            draw_string(130, text_y, "YOU LOSE!", COL_RED);
            draw_string(100, text_y + 30, "CLICK TO CONTINUE", COL_WHITE);
            break;
    }
}

// --- Server Map Fetcher ---
esp_err_t _map_http_event_handler(esp_http_client_event_t *evt) { return ESP_OK; }

void fetch_new_map(int exit_edge, int exit_px, int exit_py) {
    char url[256];
    
    // 1. Trigger Generation on Server
    // Send detailed exit info so server can calculate spawn
    snprintf(url, sizeof(url), "http://%s:%d/generate_map?x=%d&y=%d&ex_edge=%d&ex_px=%d&ex_py=%d", 
             SERVER_IP, SERVER_ASSET_PORT, 
             g_game_state.world_grid_x, g_game_state.world_grid_y,
             exit_edge, exit_px, exit_py);
             
    ESP_LOGI(TAG, "Requesting Map at (%d, %d) exiting edge %d at (%d, %d)", 
             g_game_state.world_grid_x, g_game_state.world_grid_y,
             exit_edge, exit_px, exit_py);

    esp_http_client_config_t config = { .url = url, .event_handler = _map_http_event_handler, .timeout_ms = 60000 };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    
    int server_spawn_x = -1;
    int server_spawn_y = -1;
    
    if (esp_http_client_open(client, 0) == ESP_OK) {
        int len = esp_http_client_fetch_headers(client);
        if (len > 0) {
            char resp_buf[64] = {0};
            int read_len = esp_http_client_read(client, resp_buf, sizeof(resp_buf)-1);
            if (read_len > 0) {
                // Server should respond with "OK <spawn_x> <spawn_y>"
                if (sscanf(resp_buf, "OK %d %d", &server_spawn_x, &server_spawn_y) == 2) {
                     ESP_LOGI(TAG, "Server assigned spawn: (%d, %d)", server_spawn_x, server_spawn_y);
                     spawn_x = server_spawn_x; // Update globals if needed
                     spawn_y = server_spawn_y;
                } else {
                    ESP_LOGW(TAG, "Server response format error: %s", resp_buf);
                }
            }
        }
    } else {
        ESP_LOGE(TAG, "Failed to trigger map generation");
    }
    esp_http_client_cleanup(client);
    
    // Small delay to ensure server file writes flush if needed (though response implies done)
    vTaskDelay(pdMS_TO_TICKS(200));
    
    // 2. Download the Big Map and Collision Data
    fetch_global_map_data();

    // 3. Fetch mobs for this map
    fetch_mobs();

    // 4. Fetch buildings for this map (for home base interaction)
    game_set_buildings(&g_game_state, false);  // Reset buildings
    int is_home_base = fetch_buildings(g_game_state.world_grid_x, g_game_state.world_grid_y, building_callback);
    if (is_home_base == 1) {
        g_game_state.buildings.is_home_base = true;
        ESP_LOGI(TAG, "Loaded home base with %d buildings", g_game_state.buildings.building_count);
    }

    // 5. Apply Spawn (Server Authoritative)
    if (server_spawn_x != -1 && server_spawn_y != -1) {
        g_game_state.player.x = server_spawn_x;
        g_game_state.player.y = server_spawn_y;
    } else {
        // Fallback (should rarely happen if server is good)
        g_game_state.player.x = 800;
        g_game_state.player.y = 800;
    }
    g_game_state.player.vy = 0;
}

// --- Hardware Control ---
static void apply_hardware_settings(void) {
    static uint8_t last_brightness = 255;
    static bool last_sound = true;
    static bool last_sleep_state = false; 

    bool current_sleep_mode = (g_game_state.menu_state == MENU_SLEEP);
    
    if (current_sleep_mode && !last_sleep_state) {
        gpio_set_level(BSP_LCD_GPIO_BL, 0);
        last_sleep_state = true;
    } else if (!current_sleep_mode && last_sleep_state) {
        gpio_set_level(BSP_LCD_GPIO_BL, 1);
        bsp_lcd_brightness_set(100);
        last_brightness = 0;
        last_sleep_state = false;
    }

    if (!current_sleep_mode) {
        if (g_game_state.brightness != last_brightness) {
            int percent = (g_game_state.brightness * 100) / 255;
            if (percent < 5) percent = 5; 
            bsp_lcd_brightness_set(percent);
            last_brightness = g_game_state.brightness;
        }
    }

    if (g_game_state.sound_on != last_sound) {
        bsp_codec_mute_set(!g_game_state.sound_on);
        int vol = g_game_state.sound_on ? 100 : 0;
        bsp_codec_volume_set(vol, NULL);
        last_sound = g_game_state.sound_on;
    }
}

// --- RENDERER ---
void game_render_task(void *pvParameters) {
    frame_buf = (uint16_t*)heap_caps_malloc(SCR_W * SCR_H * 2, MALLOC_CAP_SPIRAM);
    if (frame_buf == NULL) {
        ESP_LOGE(TAG, "Failed to allocate frame_buf");
        vTaskDelete(NULL);
        return;
    }
    
    esp_lcd_panel_handle_t panel_handle = bsp_lcd_get_panel_handle();

    // game_init() is now called in app_main() before fetching pet data

    // Initial Map Fetch (No exit data)
    fetch_new_map(-1, 0, 0);

    // Set initial spawn position from exit points
    if (exit_point_count > 0) {
        g_game_state.player.x = spawn_x - FOX_W / 2;
        g_game_state.player.y = spawn_y - FOX_H / 2;
        ESP_LOGI(TAG, "Initial spawn at: (%d, %d)", g_game_state.player.x, g_game_state.player.y);
    }

    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(33);

    const char *main_opts[] = {"PET", "SWITCH PET", "CLONING", "INVENTORY", "COLLECTION", "TELEPORT", "SLEEP", "SETTINGS", "BACK"};
    
    ESP_LOGI(TAG, "Starting Render Loop");
    
    while(1) {
        // --- BATTLE MODE LOGIC ---
        if (g_game_state.mode == MODE_BATTLE) {
            BattleState *b = &g_game_state.battle;

            // Handle gesture in battle (player turn)
            if (b->phase == BATTLE_PHASE_PLAYER_TURN) {
                // Wheel click confirms spell gesture
                if (g_game_state.input.btn_clicked && g_game_state.gesture.has_strokes) {
                    // Extract points for server recognition
                    int16_t pts_x[MAX_GESTURE_POINTS];
                    int16_t pts_y[MAX_GESTURE_POINTS];
                    int pt_count = g_game_state.gesture.point_count;
                    for (int i = 0; i < pt_count; i++) {
                        pts_x[i] = g_game_state.gesture.points[i].x;
                        pts_y[i] = g_game_state.gesture.points[i].y;
                    }

                    // Get pet's active skills for recognition
                    ActivePetState *pet = &g_game_state.active_pet;
                    const char *active_skills[MAX_ACTIVE_SKILLS];
                    int active_count = 0;
                    if (pet->loaded) {
                        for (int i = 0; i < pet->skill_count && i < MAX_ACTIVE_SKILLS; i++) {
                            active_skills[i] = pet->skills[i].id;
                            active_count++;
                        }
                    }

                    // Recognize gesture against pet's skills
                    char recognized_skill[32] = {0};
                    int accuracy = 0;
                    float damage_mult = 1.0f;
                    SpellType spell = SPELL_BASIC;

                    if (active_count > 0) {
                        int result = recognize_skill_gesture(pts_x, pts_y, pt_count,
                                                             active_skills, active_count,
                                                             recognized_skill, &accuracy, &damage_mult);
                        if (result == 0 && recognized_skill[0] != '\0') {
                            // Store skill ID for sprite lookup
                            strncpy(b->last_skill_id, recognized_skill, sizeof(b->last_skill_id) - 1);
                            b->last_skill_id[sizeof(b->last_skill_id) - 1] = '\0';

                            // Map skill type to spell for fallback animation
                            // Check the skill's type to pick animation
                            for (int i = 0; i < pet->skill_count; i++) {
                                if (strcmp(pet->skills[i].id, recognized_skill) == 0) {
                                    const char *type = pet->skills[i].type;
                                    if (strstr(type, "fire")) spell = SPELL_M;
                                    else if (strstr(type, "ice")) spell = SPELL_W;
                                    else if (strstr(type, "electric")) spell = SPELL_S;
                                    else if (strstr(type, "water")) spell = SPELL_O;
                                    else spell = SPELL_N;  // Physical/other
                                    // Store the actual skill name for display
                                    strncpy(b->last_skill_name, pet->skills[i].name, sizeof(b->last_skill_name) - 1);
                                    b->last_skill_name[sizeof(b->last_skill_name) - 1] = '\0';
                                    break;
                                }
                            }
                            ESP_LOGI(TAG, "Recognized skill: %s (accuracy=%d%%, dmg=%.2f)", recognized_skill, accuracy, damage_mult);
                        } else {
                            ESP_LOGI(TAG, "Gesture not recognized (accuracy=%d%%)", accuracy);
                            strncpy(b->last_skill_name, "FAILED", sizeof(b->last_skill_name) - 1);
                            b->last_skill_id[0] = '\0';  // No skill to show
                        }
                    } else {
                        // Fallback to old global recognition if no pet skills
                        int spell_idx = recognize_gesture_server(pts_x, pts_y, pt_count);
                        spell = (spell_idx >= 0 && spell_idx <= 5) ? (SpellType)spell_idx : SPELL_BASIC;
                        strncpy(b->last_skill_name, spell_names[spell], sizeof(b->last_skill_name) - 1);
                    }

                    b->cast_attempts++;

                    if (spell != SPELL_BASIC || b->cast_attempts >= 3) {
                        // Successful cast or out of attempts
                        b->last_player_spell = spell;
                        b->phase = BATTLE_PHASE_PLAYER_CAST;
                        b->phase_timer = 45;
                        ESP_LOGI(TAG, "Cast spell: %d (%s)", spell, b->last_skill_name);
                    }
                    game_clear_gesture(&g_game_state);
                    g_game_state.input.btn_clicked = false;
                }
            }

            // Handle win/lose click to exit
            if ((b->phase == BATTLE_PHASE_WIN || b->phase == BATTLE_PHASE_LOSE) && b->phase_timer == 0) {
                if (g_game_state.input.btn_clicked) {
                    // Report battle end to server for rewards
                    bool player_won = (b->phase == BATTLE_PHASE_WIN);
                    char dna_species[32] = {0};
                    char dna_element[16] = {0};
                    int xp_gained = 0;

                    int result = send_battle_end(player_won, b->enemy_species, b->enemy_element,
                                                 b->enemy_level, dna_species, dna_element, &xp_gained);
                    if (result == 0 && player_won) {
                        ESP_LOGI(TAG, "Battle victory! Got DNA: %s/%s, XP: %d", dna_species, dna_element, xp_gained);

                        // Update local pet XP (server handles level-up logic)
                        g_game_state.active_pet.xp += xp_gained;

                        // Check if we crossed the XP threshold for next level
                        // XP to next level = current_level * 100
                        while (g_game_state.active_pet.xp >= g_game_state.active_pet.xp_to_next_level &&
                               g_game_state.active_pet.level < 10) {
                            g_game_state.active_pet.xp -= g_game_state.active_pet.xp_to_next_level;
                            g_game_state.active_pet.level++;
                            g_game_state.active_pet.xp_to_next_level = g_game_state.active_pet.level * 100;
                            ESP_LOGI(TAG, "Level up! Now Lv%d", g_game_state.active_pet.level);
                        }
                    }

                    // Return to overworld
                    g_game_state.mode = MODE_OVERWORLD;

                    // If won, remove the mob
                    if (player_won && b->enemy_mob_index >= 0 && b->enemy_mob_index < current_mob_count) {
                        // Clear mob sprite
                        current_mobs[b->enemy_mob_index].sprite_data = NULL;
                    }
                    g_game_state.input.btn_clicked = false;
                }
            }

            game_update_battle(&g_game_state);

            // Render battle scene
            Mob *enemy = NULL;
            if (b->enemy_mob_index >= 0 && b->enemy_mob_index < current_mob_count) {
                enemy = &current_mobs[b->enemy_mob_index];
            }
            // Use pet sprite if loaded, otherwise fall back to fox
            uint16_t *battle_sprite = (pet_sprites_loaded && pet_walk_sprites[0]) ? pet_walk_sprites[0] : sprites[0];
            draw_battle_scene(&g_game_state, battle_sprite, enemy);

            // Draw gesture trail on top in battle
            if (g_game_state.gesture.point_count > 0 && b->phase == BATTLE_PHASE_PLAYER_TURN) {
                draw_gesture_trail(&g_game_state.gesture);
            }

            esp_lcd_panel_draw_bitmap(panel_handle, 0, 0, SCR_W, SCR_H, frame_buf);
            vTaskDelayUntil(&xLastWakeTime, xFrequency);
            continue;
        }

        // --- RUNE BATTLE MODE (New System) ---
        if (g_game_state.mode == MODE_RUNE_BATTLE) {
            RuneBattleState *rb = &g_game_state.rune_battle;

            // Handle intro phase transition
            static int intro_timer = 60;
            static bool runes_fetched = false;
            if (rb->phase == RUNE_PHASE_INTRO) {
                intro_timer--;
                if (intro_timer <= 0) {
                    intro_timer = 60;  // Reset for next battle

                    // Fetch available runes from server
                    if (!runes_fetched) {
                        rb->available_count = 0;  // Reset before fetching
                        ActivePetState *pet = &g_game_state.active_pet;
                        int count = fetch_available_runes(pet->species, pet->element, pet->level,
                                                          available_rune_callback);
                        ESP_LOGI(TAG, "Fetched %d available runes for battle", count);
                        if (count >= 0) {
                            runes_fetched = true;
                        } else {
                            // Retry next frame if fetch failed
                            intro_timer = 30;  // Short delay before retry
                            // Don't set runes_fetched, so we'll retry
                        }
                    }

                    // Only proceed to player chain phase if runes fetched successfully
                    if (!runes_fetched) {
                        // Don't proceed - continue the main loop to retry later
                        goto rune_battle_render;
                    }

                    rb->phase = RUNE_PHASE_PLAYER_CHAIN;
                    rb->chain_timer = rb->chain_timer_max;  // Start timer
                }
            } else {
                runes_fetched = false;  // Reset for next battle
            }

            // Handle player chain building phase
            if (rb->phase == RUNE_PHASE_PLAYER_CHAIN) {
                // Gesture recognition when player clicks
                if (g_game_state.input.btn_clicked && g_game_state.gesture.has_strokes) {
                    // Extract gesture points
                    int16_t pts_x[MAX_GESTURE_POINTS];
                    int16_t pts_y[MAX_GESTURE_POINTS];
                    int pt_count = g_game_state.gesture.point_count;
                    for (int i = 0; i < pt_count; i++) {
                        pts_x[i] = g_game_state.gesture.points[i].x;
                        pts_y[i] = g_game_state.gesture.points[i].y;
                    }

                    // Build available runes array for recognition
                    const char *avail[20];
                    for (int i = 0; i < rb->available_count && i < 20; i++) {
                        avail[i] = rb->available_runes[i];
                    }

                    // Recognize against available runes using server
                    char rune_id[32] = {0};
                    char rune_name[32] = {0};
                    int accuracy = 0;
                    int power = 0;
                    bool chain_bonus = false;

                    // Call server to recognize rune gesture
                    int result = recognize_rune_gesture(pts_x, pts_y, pt_count,
                                                        avail, rb->available_count,
                                                        rune_id, rune_name,
                                                        &accuracy, &power, &chain_bonus);

                    if (result == 0 && rune_id[0]) {
                        // Rune recognized! Add to chain
                        game_rune_battle_add_to_chain(&g_game_state, rune_id, rune_name, power, accuracy, chain_bonus);

                        // Show feedback
                        strncpy(rb->last_recognized_id, rune_id, sizeof(rb->last_recognized_id) - 1);
                        strncpy(rb->last_recognized_name, rune_name, sizeof(rb->last_recognized_name) - 1);
                        rb->last_accuracy = accuracy;
                        rb->show_recognition_feedback = true;
                        rb->feedback_timer = 45;

                        ESP_LOGI(TAG, "Rune recognized: %s (%s) power=%d acc=%d",
                                 rune_id, rune_name, power, accuracy);
                    } else {
                        // Not recognized - show failure feedback
                        strncpy(rb->last_recognized_name, "???", sizeof(rb->last_recognized_name) - 1);
                        rb->last_accuracy = 0;
                        rb->show_recognition_feedback = true;
                        rb->feedback_timer = 30;

                        ESP_LOGW(TAG, "Rune not recognized (or already used)");
                    }

                    game_clear_gesture(&g_game_state);
                    g_game_state.input.btn_clicked = false;
                }

                // Long press to execute chain early
                if (g_game_state.input.btn_menu_open_combo) {
                    if (rb->player_chain_count > 0) {
                        // Execute chain
                        rb->phase = RUNE_PHASE_PLAYER_EXECUTE;
                        rb->execute_index = 0;
                        rb->execute_timer = 30;

                        // Calculate total damage (simplified)
                        int total = 0;
                        for (int i = 0; i < rb->player_chain_count; i++) {
                            total += rb->player_chain[i].power;
                        }
                        total = (int)(total * rb->momentum);
                        rb->total_damage_dealt = total;
                        rb->enemy_hp -= total;
                        if (rb->enemy_hp < 0) rb->enemy_hp = 0;
                    }
                    g_game_state.input.btn_menu_open_combo = false;
                }
            }

            // Handle enemy chain phase
            static bool enemy_chain_fetched = false;
            if (rb->phase == RUNE_PHASE_ENEMY_CHAIN) {
                // Fetch enemy chain from server (only once per phase)
                if (!enemy_chain_fetched) {
                    enemy_chain_fetched = true;

                    // Clear enemy chain before fetching
                    rb->enemy_chain_count = 0;

                    // Fetch enemy chain from server
                    int chain_count = 0;
                    int enemy_damage = fetch_enemy_rune_chain(
                        rb->enemy_species, rb->enemy_element, rb->enemy_level,
                        rb->player_hp, rb->enemy_hp,
                        enemy_chain_callback, &chain_count);

                    ESP_LOGI(TAG, "Enemy chain: %d runes, %d total damage", chain_count, enemy_damage);

                    // Apply enemy damage to player
                    if (enemy_damage > 0) {
                        rb->player_hp -= enemy_damage;
                        if (rb->player_hp < 0) rb->player_hp = 0;
                    }

                    // Transition to execute phase
                    rb->phase = RUNE_PHASE_ENEMY_EXECUTE;
                    rb->execute_index = 0;
                    rb->execute_timer = 20;
                }
            } else {
                enemy_chain_fetched = false;  // Reset for next enemy turn
            }

            // Handle win/lose click to exit
            if (rb->phase == RUNE_PHASE_WIN || rb->phase == RUNE_PHASE_LOSE) {
                if (g_game_state.input.btn_clicked) {
                    // Report battle end
                    bool player_won = (rb->phase == RUNE_PHASE_WIN);
                    char dna_species[32] = {0};
                    char dna_element[16] = {0};
                    int xp_gained = 0;

                    send_battle_end(player_won, rb->enemy_species, rb->enemy_element,
                                   rb->enemy_level, dna_species, dna_element, &xp_gained);

                    if (player_won) {
                        g_game_state.active_pet.xp += xp_gained;
                        if (rb->enemy_mob_index >= 0 && rb->enemy_mob_index < current_mob_count) {
                            current_mobs[rb->enemy_mob_index].sprite_data = NULL;
                        }
                    }

                    g_game_state.mode = MODE_OVERWORLD;
                    g_game_state.input.btn_clicked = false;
                }
            }

            // Update battle state
            game_update_rune_battle(&g_game_state);

rune_battle_render:
            // Render rune battle scene
            Mob *enemy = NULL;
            if (rb->enemy_mob_index >= 0 && rb->enemy_mob_index < current_mob_count) {
                enemy = &current_mobs[rb->enemy_mob_index];
            }
            uint16_t *battle_sprite = (pet_sprites_loaded && pet_walk_sprites[0]) ? pet_walk_sprites[0] : sprites[0];
            draw_rune_battle_scene(&g_game_state, battle_sprite, enemy);

            // Draw gesture trail during player chain phase
            if (g_game_state.gesture.point_count > 0 && rb->phase == RUNE_PHASE_PLAYER_CHAIN) {
                draw_gesture_trail(&g_game_state.gesture);
            }

            esp_lcd_panel_draw_bitmap(panel_handle, 0, 0, SCR_W, SCR_H, frame_buf);
            vTaskDelayUntil(&xLastWakeTime, xFrequency);
            continue;
        }

        // --- RUNE CALIBRATION MODE ---
        if (g_game_state.mode == MODE_CALIBRATION) {
            CalibrationState *cal = &g_game_state.calibration;

            // Load runes list on first entry (when loading_skills is true)
            if (cal->loading_skills) {
                // Fetch runes from server for calibration
                ESP_LOGI(TAG, "Fetching runes for calibration...");
                cal->skill_count = 0;  // Reset before loading
                int result = fetch_runes_for_calibration(rune_calibration_callback,
                                                         &cal->total_skills,
                                                         &cal->calibrated_count);
                if (result >= 0) {
                    ESP_LOGI(TAG, "Loaded %d uncalibrated runes (total: %d, calibrated: %d)",
                             cal->skill_count, cal->total_skills, cal->calibrated_count);
                } else {
                    ESP_LOGE(TAG, "Failed to fetch runes for calibration");
                }
                cal->loading_skills = false;
            }

            // Handle input
            if (cal->show_skill_list) {
                // Skill list mode: click to select skill
                if (g_game_state.input.btn_clicked) {
                    if (cal->skill_count > 0) {
                        game_calibration_select_skill(&g_game_state);
                    }
                    g_game_state.input.btn_clicked = false;
                }
                // Long press to exit calibration
                if (g_game_state.input.btn_menu_open_combo) {
                    g_game_state.mode = MODE_OVERWORLD;
                    g_game_state.input.btn_menu_open_combo = false;
                }
                // Knob rotation for scrolling
                if (g_game_state.input.knob_rotated_left) {
                    if (cal->selected_skill_index > 0) {
                        cal->selected_skill_index--;
                        // Adjust scroll to keep selection visible
                        if (cal->selected_skill_index < cal->scroll_offset) {
                            cal->scroll_offset = cal->selected_skill_index;
                        }
                    }
                    g_game_state.input.knob_rotated_left = false;
                }
                if (g_game_state.input.knob_rotated_right) {
                    if (cal->selected_skill_index < cal->skill_count - 1) {
                        cal->selected_skill_index++;
                        // Adjust scroll to keep selection visible (8 items visible)
                        if (cal->selected_skill_index >= cal->scroll_offset + 8) {
                            cal->scroll_offset = cal->selected_skill_index - 7;
                        }
                    }
                    g_game_state.input.knob_rotated_right = false;
                }
            } else {
                // Drawing mode: click to send calibration sample
                if (g_game_state.input.btn_clicked) {
                    if (g_game_state.gesture.point_count >= 5) {
                        int16_t pts_x[MAX_GESTURE_POINTS];
                        int16_t pts_y[MAX_GESTURE_POINTS];
                        int pt_count = g_game_state.gesture.point_count;
                        for (int i = 0; i < pt_count; i++) {
                            pts_x[i] = g_game_state.gesture.points[i].x;
                            pts_y[i] = g_game_state.gesture.points[i].y;
                        }

                        // Get current rune ID (stored in skill_id field)
                        const char *rune_id = cal->skills[cal->selected_skill_index].skill_id;

                        // Send calibration sample for this rune
                        int result = send_rune_calibration(rune_id, pts_x, pts_y, pt_count);
                        if (result == 0) {
                            cal->skills[cal->selected_skill_index].samples_count++;
                            cal->sample_sent = true;
                            cal->feedback_timer = 30;

                            // Only go back to list after 10 samples (rune fully calibrated)
                            if (cal->skills[cal->selected_skill_index].samples_count >= SAMPLES_NEEDED) {
                                // Refresh list to remove this calibrated rune
                                cal->loading_skills = true;
                                cal->show_skill_list = true;
                                cal->selected_skill_index = 0;
                                cal->scroll_offset = 0;
                            }
                            // Otherwise stay on same rune for more samples
                        }
                    }
                    game_clear_gesture(&g_game_state);
                    g_game_state.input.btn_clicked = false;
                }

                // Long press to go back to rune list
                if (g_game_state.input.btn_menu_open_combo) {
                    game_calibration_back(&g_game_state);
                    g_game_state.input.btn_menu_open_combo = false;
                }
            }

            // Update calibration state (handles navigation, timers)
            game_update_calibration(&g_game_state);

            // --- Render ---
            for (int i = 0; i < SCR_W * SCR_H; i++) {
                frame_buf[i] = COL_DARK;
            }

            if (cal->show_skill_list) {
                // Draw header with progress
                draw_string(60, 10, "RUNE CALIBRATE", COL_GOLD);

                // Progress indicator (calibrated / total)
                char progress_str[32];
                snprintf(progress_str, sizeof(progress_str), "%d/%d done",
                         cal->calibrated_count, cal->total_skills);
                draw_string(280, 10, progress_str, COL_GREEN);

                // Show remaining count
                char remain_str[32];
                snprintf(remain_str, sizeof(remain_str), "%d left", cal->skill_count);
                draw_string(160, 35, remain_str, COL_GRAY);

                int start_y = 65;
                int row_h = 36;
                int max_visible = 8;  // Number of items visible on screen

                if (cal->skill_count == 0 && !cal->loading_skills) {
                    // All runes calibrated!
                    draw_string(80, 180, "ALL DONE!", COL_GREEN);
                    draw_string(40, 220, "All runes calibrated!", COL_GRAY);
                } else if (cal->loading_skills) {
                    draw_string(100, 180, "LOADING...", COL_GRAY);
                } else {
                    // Draw visible runes with scroll offset
                    // Format: "Fox - Lv1" or "Fire - Lv3"
                    for (int vi = 0; vi < max_visible; vi++) {
                        int i = cal->scroll_offset + vi;
                        if (i >= cal->skill_count) break;

                        SkillCalibrationInfo *rune = &cal->skills[i];
                        int y = start_y + vi * row_h;

                        uint16_t col = (i == cal->selected_skill_index) ? COL_GOLD : COL_GRAY;

                        // Highlight selected with cursor
                        if (i == cal->selected_skill_index) {
                            draw_string(10, y, ">", COL_GOLD);
                        }

                        // Rune display name (e.g., "Fox - Lv1")
                        draw_string(30, y, rune->name, col);
                    }

                    // Scroll indicators
                    if (cal->scroll_offset > 0) {
                        draw_string(200, 55, "^", COL_GRAY);  // Up arrow
                    }
                    if (cal->scroll_offset + max_visible < cal->skill_count) {
                        draw_string(200, 355, "v", COL_GRAY);  // Down arrow
                    }
                }

                draw_string(20, 380, "CLICK:SELECT", COL_GRAY);
                draw_string(220, 380, "HOLD:EXIT", COL_GRAY);

            } else {
                // Drawing mode
                SkillCalibrationInfo *rune = &cal->skills[cal->selected_skill_index];

                // Title: rune display name (e.g., "Fox - Lv1")
                draw_string(30, 15, rune->name, COL_GOLD);

                // Sample count in top right
                char samples_str[16];
                snprintf(samples_str, sizeof(samples_str), "%d/%d", rune->samples_count, SAMPLES_NEEDED);
                uint16_t samples_col = rune->samples_count >= SAMPLES_NEEDED ? COL_GREEN : COL_WHITE;
                draw_string(320, 15, samples_str, samples_col);

                // Draw gesture trail
                if (g_game_state.gesture.point_count > 0) {
                    draw_gesture_trail(&g_game_state.gesture);
                }

                // Feedback in center when sample saved
                if (cal->sample_sent && cal->feedback_timer > 0) {
                    draw_string(160, 190, "SAVED!", COL_GREEN);
                }

                // Instructions at bottom
                draw_string(20, 380, "CLICK:SAVE", COL_GRAY);
                draw_string(220, 380, "HOLD:BACK", COL_GRAY);
            }

            esp_lcd_panel_draw_bitmap(panel_handle, 0, 0, SCR_W, SCR_H, frame_buf);
            vTaskDelayUntil(&xLastWakeTime, xFrequency);
            continue;
        }

        // --- OVERWORLD MODE LOGIC ---

        // Check for battle initiation: wheel click near mob
        if (g_game_state.input.btn_clicked && g_game_state.mode == MODE_OVERWORLD && !g_game_state.gesture.has_strokes) {
            // Check distance to each mob
            int player_cx = g_game_state.player.x + FOX_W / 2;
            int player_cy = g_game_state.player.y + FOX_H / 2;

            for (int m = 0; m < current_mob_count; m++) {
                Mob *mob = &current_mobs[m];
                if (!mob->sprite_data) continue;

                int dx = player_cx - mob->x;
                int dy = player_cy - mob->y;
                int dist_sq = dx * dx + dy * dy;

                // Within ~100 pixels
                if (dist_sq < 10000) {
                    ESP_LOGI(TAG, "Starting RUNE battle with mob %d", m);
                    // Use new rune battle system
                    game_start_rune_battle(&g_game_state, m);
                    // Available runes will be fetched from server during RUNE_PHASE_INTRO

                    g_game_state.input.btn_clicked = false;
                    break;
                }
            }
        }

        // Check for building interaction: wheel click near interactive building
        if (g_game_state.input.btn_clicked && g_game_state.mode == MODE_OVERWORLD &&
            g_game_state.buildings.is_home_base && g_game_state.menu_state == MENU_NONE) {
            int building_idx = game_check_building_interaction(&g_game_state);
            if (building_idx >= 0) {
                ESP_LOGI(TAG, "Entering building %d: %s",
                         building_idx, g_game_state.buildings.buildings[building_idx].name);
                game_enter_building(&g_game_state, building_idx);
                g_game_state.input.btn_clicked = false;
            }
        }

        game_update(&g_game_state);

        // Handle Teleport Menu - fetch discovered maps when menu opens
        if (g_game_state.menu_state == MENU_TELEPORT && g_game_state.teleport.loading) {
            // Fetch discovered maps from server
            int maps_found = fetch_discovered_maps(discovered_map_callback);
            g_game_state.teleport.loading = false;
            ESP_LOGI(TAG, "Teleport: loaded %d discovered maps", maps_found);
        }

        // Handle Cloning Center / DNA Inventory - fetch DNA samples when menu opens
        if ((g_game_state.menu_state == MENU_CLONING_CENTER ||
             g_game_state.menu_state == MENU_DNA_INVENTORY) &&
            g_game_state.dna_inventory.loading) {
            int samples_found = fetch_dna_inventory(dna_sample_callback);
            g_game_state.dna_inventory.loading = false;
            ESP_LOGI(TAG, "DNA Inventory: loaded %d samples", samples_found);
        }

        // Handle Pet Yard - fetch pet list when menu opens
        if (g_game_state.menu_state == MENU_PET_YARD && g_game_state.pet_yard.loading) {
            int pets_found = fetch_pet_list(pet_list_callback);
            g_game_state.pet_yard.loading = false;
            ESP_LOGI(TAG, "Pet Yard: loaded %d pets", pets_found);
        }

        // Handle cloning request
        if (g_game_state.cloning.cloning_in_progress) {
            DNAInventoryState *inv = &g_game_state.dna_inventory;
            CloningState *clone = &g_game_state.cloning;

            if (clone->selected_sample_1 >= 0 && clone->selected_sample_2 >= 0 &&
                clone->selected_sample_1 < inv->sample_count &&
                clone->selected_sample_2 < inv->sample_count) {

                draw_rect(0, 0, SCR_W, SCR_H, COL_BLACK);
                draw_string(120, 190, "CLONING...", COL_WHITE);
                esp_lcd_panel_draw_bitmap(panel_handle, 0, 0, SCR_W, SCR_H, frame_buf);

                char out_species[32] = {0};
                char out_element[16] = {0};
                int out_level = 0;

                const char *dna_id_1 = inv->samples[clone->selected_sample_1].id;
                const char *dna_id_2 = inv->samples[clone->selected_sample_2].id;

                int result = clone_pets(dna_id_1, dna_id_2, out_species, out_element, &out_level);

                if (result == 0) {
                    game_cloning_set_result(&g_game_state, out_species, out_element, out_level);
                    ESP_LOGI(TAG, "Cloning successful: %s/%s Lv%d", out_species, out_element, out_level);

                    // Refresh DNA inventory (2 samples were consumed)
                    g_game_state.dna_inventory.sample_count = 0;
                    int dna_found = fetch_dna_inventory(dna_sample_callback);
                    ESP_LOGI(TAG, "DNA inventory refreshed: %d samples", dna_found);
                } else {
                    clone->cloning_in_progress = false;
                    ESP_LOGE(TAG, "Cloning failed");
                }
            } else {
                clone->cloning_in_progress = false;
            }
        }

        // Handle pet switch request
        if (g_game_state.pet_yard.switch_requested && g_game_state.pet_yard.switch_pet_id[0] != '\0') {
            draw_rect(0, 0, SCR_W, SCR_H, COL_BLACK);
            draw_string(100, 190, "SWITCHING PET...", COL_WHITE);
            esp_lcd_panel_draw_bitmap(panel_handle, 0, 0, SCR_W, SCR_H, frame_buf);

            int result = switch_active_pet(g_game_state.pet_yard.switch_pet_id);

            if (result == 0) {
                // Reload active pet data
                fetch_active_pet(on_pet_info, on_pet_skill, on_skill_tree_entry);
                // Reload pet sprites
                if (g_game_state.active_pet.loaded) {
                    fetch_pet_sprites(g_game_state.active_pet.species, g_game_state.active_pet.element);
                    // Fetch rune tree for the new pet
                    game_clear_rune_tree(&g_game_state);
                    fetch_pet_rune_tree(g_game_state.active_pet.species,
                                        g_game_state.active_pet.element,
                                        g_game_state.active_pet.level,
                                        rune_tree_callback);
                }
                ESP_LOGI(TAG, "Switched to pet: %s", g_game_state.active_pet.nickname);
            }

            g_game_state.pet_yard.switch_requested = false;
            g_game_state.menu_state = MENU_NONE;
        }

        // Handle teleport request (user selected a location)
        if (g_game_state.teleport.teleport_requested) {
            draw_rect(0, 0, SCR_W, SCR_H, COL_BLACK);
            draw_string(120, 190, "TELEPORTING...", COL_WHITE);
            esp_lcd_panel_draw_bitmap(panel_handle, 0, 0, SCR_W, SCR_H, frame_buf);

            // Update world coordinates
            int target_x = g_game_state.teleport.teleport_x;
            int target_y = g_game_state.teleport.teleport_y;
            g_game_state.world_grid_x = target_x;
            g_game_state.world_grid_y = target_y;

            ESP_LOGI(TAG, "Teleporting to world grid (%d, %d)", target_x, target_y);

            // Fetch the map at the target location (pass -1 for exit_edge to indicate teleport)
            fetch_new_map(-1, 800, 800);  // Spawn in center of map

            // Clear teleport state
            g_game_state.teleport.teleport_requested = false;
            g_game_state.menu_state = MENU_NONE;
        }

        // Handle Map Transition Request
        if (g_game_state.request_new_map) {
             draw_rect(0, 0, SCR_W, SCR_H, COL_BLACK);
             draw_string(130, 190, "LOADING MAP...", COL_WHITE);
             esp_lcd_panel_draw_bitmap(panel_handle, 0, 0, SCR_W, SCR_H, frame_buf);

             // 1. Capture State Before Load
             int exited_edge = g_game_state.exit_edge_used;
             int old_x = g_game_state.player.x;
             int old_y = g_game_state.player.y;

             // 2. Load New Map and Get Server-Assigned Spawn
             fetch_new_map(exited_edge, old_x, old_y);

             g_game_state.request_new_map = false;
        }
        
        apply_hardware_settings();

        if (g_game_state.menu_state == MENU_SLEEP) {
             memset(frame_buf, 0, SCR_W*SCR_H*2);
             esp_lcd_panel_draw_bitmap(panel_handle, 0, 0, SCR_W, SCR_H, frame_buf);
             vTaskDelay(pdMS_TO_TICKS(100));
             continue;
        }

        if (g_game_state.mode == MODE_OVERWORLD) {
             // Clear buffer first to avoid ghosting
             memset(frame_buf, 0, SCR_W * SCR_H * 2);
             
             // Draw Background from Big Map
             int cam_x = g_game_state.cam_x;
             int cam_y = g_game_state.cam_y;
             
             if (current_map_image_data) {
                 // MAP_PX_W is 1600
                 int map_w = 1600; 
                 
                 for (int y = 0; y < SCR_H; y++) {
                     int src_y = cam_y + y;
                     if (src_y < 0 || src_y >= 1600) continue; // Clip
                     
                     // We can do a memcpy for the row if we handle clipping carefully
                     // Src offset: src_y * 1600 + cam_x
                     // Dest offset: y * SCR_W
                     // Length: SCR_W pixels (times 2 bytes)
                     
                     // Check X clipping
                     int copy_w = SCR_W;
                     int src_x = cam_x;
                     int dst_x = 0;
                     
                     if (src_x < 0) { dst_x = -src_x; copy_w += src_x; src_x = 0; }
                     if (src_x + copy_w > 1600) { copy_w = 1600 - src_x; }
                     
                     if (copy_w > 0) {
                        memcpy(&frame_buf[y * SCR_W + dst_x], 
                               &current_map_image_data[src_y * map_w + src_x], 
                               copy_w * 2);
                     }
                 }
             } else {
                 draw_rect(0, 0, SCR_W, SCR_H, COL_BLACK); // Fallback
             }

             // Draw Mobs
             for (int m = 0; m < current_mob_count; m++) {
                 Mob *mob = &current_mobs[m];
                 if (!mob->sprite_data) continue;

                 // Calculate screen position (mob position is center of sprite)
                 int mob_dx = mob->x - (FOX_W / 2) - cam_x;
                 int mob_dy = mob->y - (FOX_H / 2) - cam_y;

                 // Skip if completely off screen
                 if (mob_dx + FOX_W < 0 || mob_dx >= SCR_W) continue;
                 if (mob_dy + FOX_H < 0 || mob_dy >= SCR_H) continue;

                 // Draw mob sprite with chroma key
                 for (int y = 0; y < FOX_H; y++) {
                     int d_y = mob_dy + y;
                     if (d_y < 0 || d_y >= SCR_H) continue;
                     for (int x = 0; x < FOX_W; x++) {
                         int d_x = mob_dx + x;
                         if (d_x < 0 || d_x >= SCR_W) continue;
                         uint16_t p = mob->sprite_data[y * FOX_W + x];
                         if (p != CHROMA_KEY) frame_buf[d_y * SCR_W + d_x] = p;
                     }
                 }
             }

        } else {
            // Platformer Mode (Disabled map drawing for now as we focus on Overworld)
             memset(frame_buf, 0, SCR_W*SCR_H*2);
        }

        // Draw Game Sprite
        // pet_walk_sprites has 4 frames per direction: [0-3]=right, [4-7]=left
        // sprites (fox fallback) has 8 frames per direction: [0-7]=right, [8-15]=left
        const uint16_t* sprite = NULL;

        if (pet_sprites_loaded && pet_walk_sprites[0]) {
            // Use dynamically loaded pet sprites (4 frames per direction)
            int frame = g_game_state.player.frame_index % 4;  // Map 8-frame to 4-frame
            if (!g_game_state.player.facing_right) frame += 4;
            sprite = pet_walk_sprites[frame];
        } else {
            // Fallback to fox sprites (8 frames per direction)
            int s_idx = g_game_state.player.frame_index;
            if (!g_game_state.player.facing_right) s_idx += 8;
            sprite = sprites[s_idx];
        }

        int dx = g_game_state.player.x - g_game_state.cam_x;
        int dy = g_game_state.player.y - g_game_state.cam_y;

        if (sprite) {
            for (int y=0; y<FOX_H; y++) {
                int d_y = dy + y;
                if (d_y < 0 || d_y >= SCR_H) continue;
                for (int x=0; x<FOX_W; x++) {
                    int d_x = dx + x;
                    if (d_x < 0 || d_x >= SCR_W) continue;
                    uint16_t p = sprite[y * FOX_W + x];
                    if (p != CHROMA_KEY) frame_buf[d_y * SCR_W + d_x] = p;
                }
            }
        }

        // Show building interaction prompt when at home base
        if (g_game_state.mode == MODE_OVERWORLD && g_game_state.buildings.is_home_base &&
            g_game_state.menu_state == MENU_NONE) {
            int building_idx = game_check_building_interaction(&g_game_state);
            if (building_idx >= 0) {
                BuildingInfo *bldg = &g_game_state.buildings.buildings[building_idx];
                // Draw interaction prompt at bottom of screen
                draw_rect(60, SCR_H - 45, SCR_W - 120, 40, COL_DARK);
                // Border
                for (int i = 0; i < SCR_W - 120; i++) {
                    frame_buf[(SCR_H - 45) * SCR_W + 60 + i] = COL_WHITE;
                    frame_buf[(SCR_H - 6) * SCR_W + 60 + i] = COL_WHITE;
                }
                for (int j = 0; j < 40; j++) {
                    frame_buf[(SCR_H - 45 + j) * SCR_W + 60] = COL_WHITE;
                    frame_buf[(SCR_H - 45 + j) * SCR_W + SCR_W - 61] = COL_WHITE;
                }

                char prompt[48];
                if (strcmp(bldg->name, "cloning_center") == 0) {
                    snprintf(prompt, sizeof(prompt), "CLONING CENTER");
                } else if (strcmp(bldg->name, "pet_yard") == 0) {
                    snprintf(prompt, sizeof(prompt), "PET YARD");
                } else {
                    snprintf(prompt, sizeof(prompt), "%s", bldg->name);
                }
                draw_string(80, SCR_H - 38, prompt, COL_WHITE);
                draw_string(80, SCR_H - 18, "Click to enter", COL_GRAY);
            }
        }

        // Draw Pet Stats - Full Screen (before menu so it can take over)
        if (g_game_state.menu_state == MENU_PET_STATS) {
            // Full screen dark background
            for (int i = 0; i < SCR_W * SCR_H; i++) {
                frame_buf[i] = COL_DARK;
            }

            ActivePetState *pet = &g_game_state.active_pet;
            PetStatsMenuState *pmenu = &g_game_state.pet_menu;

            if (!pet->loaded) {
                draw_string(120, 200, "NO PET DATA", COL_RED);
                draw_string(100, 240, "Loading...", COL_GRAY);
            } else {
                // Left side: Pet sprite and basic info
                // Draw pet sprite at top left
                if (pet_sprites_loaded && pet_walk_sprites[0]) {
                    int px = 20;
                    int py = 30;
                    for (int y = 0; y < FOX_H; y++) {
                        for (int x = 0; x < FOX_W; x++) {
                            uint16_t p = pet_walk_sprites[0][y * FOX_W + x];
                            if (p != CHROMA_KEY) {
                                int dx = px + x;
                                int dy = py + y;
                                if (dx >= 0 && dx < SCR_W && dy >= 0 && dy < SCR_H) {
                                    frame_buf[dy * SCR_W + dx] = p;
                                }
                            }
                        }
                    }
                }

                // Pet name and level (next to sprite)
                char title_str[64];
                snprintf(title_str, sizeof(title_str), "%s Lv%d", pet->nickname, pet->level);
                draw_string(130, 40, title_str, COL_GOLD);

                // Species/Element
                char type_str[48];
                snprintf(type_str, sizeof(type_str), "%s / %s", pet->species, pet->element);
                draw_string(130, 65, type_str, COL_GRAY);

                // HP Bar
                draw_string(130, 85, "HP", COL_WHITE);
                draw_hp_bar(165, 85, 130, 12, pet->current_hp, pet->max_hp, COL_GREEN);
                char hp_str[32];
                snprintf(hp_str, sizeof(hp_str), "%d/%d", pet->current_hp, pet->max_hp);
                draw_string(220, 99, hp_str, COL_GRAY);

                // XP Bar (below HP bar)
                draw_string(130, 110, "XP", COL_CYAN);
                int xp_to_next = pet->xp_to_next_level > 0 ? pet->xp_to_next_level : 100;
                draw_hp_bar(165, 110, 130, 12, pet->xp, xp_to_next, COL_CYAN);
                char xp_str[32];
                snprintf(xp_str, sizeof(xp_str), "%d/%d", pet->xp, xp_to_next);
                draw_string(220, 124, xp_str, COL_GRAY);

                // Separator line
                for (int x = 10; x < SCR_W - 10; x++) {
                    frame_buf[143 * SCR_W + x] = COL_GRAY;
                }

                // Stats row
                char stat_str[64];
                snprintf(stat_str, sizeof(stat_str), "ATK:%d  DEF:%d  SPD:%d", pet->atk, pet->def, pet->spd);
                draw_string(20, 152, stat_str, COL_WHITE);

                // Runes section header
                draw_string(20, 180, "RUNE TREE", COL_GOLD);

                // Use rune_tree for full list (includes locked runes)
                int total_items = pet->rune_tree_count + 1; // runes + BACK

                // Show scroll indicators
                if (pmenu->scroll_offset > 0) {
                    draw_string(SCR_W - 30, 180, "^", COL_GRAY);
                }
                if (pmenu->scroll_offset + 3 < total_items) {
                    draw_string(SCR_W - 30, SCR_H - 50, "v", COL_GRAY);
                }

                // Runes list (3 visible + BACK)
                int rune_y = 210;
                int rune_row_h = 65;
                int visible_count = 3;

                if (pet->rune_tree_count == 0) {
                    draw_string(40, rune_y, "No runes available", COL_GRAY);
                    rune_y += rune_row_h;
                    bool back_selected = (pmenu->selected_skill == 0);
                    uint16_t back_col = back_selected ? COL_WHITE : COL_GRAY;
                    if (back_selected) {
                        draw_string(20, rune_y, ">", COL_GOLD);
                    }
                    draw_string(40, rune_y, "BACK", back_col);
                } else {
                    for (int v = 0; v < visible_count && (v + pmenu->scroll_offset) < total_items; v++) {
                        int item_idx = v + pmenu->scroll_offset;
                        bool is_back = (item_idx == pet->rune_tree_count);
                        bool selected = (item_idx == pmenu->selected_skill);

                        // Draw row background if selected
                        if (selected) {
                            uint16_t bg_col = 0x2104;
                            for (int y = rune_y - 2; y < rune_y + rune_row_h - 12; y++) {
                                for (int x = 15; x < SCR_W - 15; x++) {
                                    if (y >= 0 && y < SCR_H) {
                                        frame_buf[y * SCR_W + x] = bg_col;
                                    }
                                }
                            }
                        }

                        // Draw selection cursor
                        if (selected) {
                            draw_string(20, rune_y, ">", COL_GOLD);
                        }

                        if (is_back) {
                            uint16_t col = selected ? COL_WHITE : COL_GRAY;
                            draw_string(40, rune_y, "BACK", col);
                        } else {
                            // Rune entry from rune_tree
                            RuneTreeEntry *rune = &pet->rune_tree[item_idx];
                            bool is_unlocked = rune->unlocked;

                            // Rune name - gold if unlocked, gray if locked
                            uint16_t name_col = is_unlocked ? (selected ? COL_WHITE : COL_GOLD) : COL_GRAY;
                            draw_string(40, rune_y, rune->name, name_col);

                            // Second line: source/power if unlocked, "Unlock at Lv X" if locked
                            char detail_str[48];
                            if (is_unlocked) {
                                snprintf(detail_str, sizeof(detail_str), "%s Pwr:%d", rune->source, rune->power);
                                draw_string(40, rune_y + 20, detail_str, COL_GRAY);
                            } else {
                                snprintf(detail_str, sizeof(detail_str), "Unlock at Lv %d", rune->unlock_level);
                                draw_string(40, rune_y + 20, detail_str, COL_RED);
                            }

                            // Calibration indicator box (right side) - 40x40 box
                            int gbox_x = SCR_W - 70;
                            int gbox_y_pos = rune_y;
                            int gbox_size = 40;

                            // Draw box border - gray if locked, colored if unlocked
                            uint16_t box_col = is_unlocked ? COL_GRAY : 0x4208;
                            for (int x = gbox_x; x < gbox_x + gbox_size; x++) {
                                frame_buf[gbox_y_pos * SCR_W + x] = box_col;
                                frame_buf[(gbox_y_pos + gbox_size - 1) * SCR_W + x] = box_col;
                            }
                            for (int gy = gbox_y_pos; gy < gbox_y_pos + gbox_size; gy++) {
                                frame_buf[gy * SCR_W + gbox_x] = box_col;
                                frame_buf[gy * SCR_W + gbox_x + gbox_size - 1] = box_col;
                            }

                            // Draw calibration indicator
                            int cx = gbox_x + gbox_size / 2;
                            int cy = gbox_y_pos + gbox_size / 2;

                            if (!is_unlocked) {
                                // Locked: draw lock icon (simple rectangle)
                                for (int ly = cy - 8; ly < cy + 6; ly++) {
                                    for (int lx = cx - 6; lx < cx + 6; lx++) {
                                        if (lx >= gbox_x && lx < gbox_x + gbox_size &&
                                            ly >= gbox_y_pos && ly < gbox_y_pos + gbox_size) {
                                            frame_buf[ly * SCR_W + lx] = 0x4208;
                                        }
                                    }
                                }
                            } else if (rune->calibrated) {
                                // Calibrated - show checkmark
                                for (int d = 0; d < 8; d++) {
                                    frame_buf[(cy + d - 4) * SCR_W + cx - 4 + d] = COL_GREEN;
                                }
                                for (int d = 0; d < 12; d++) {
                                    frame_buf[(cy + 4 - d) * SCR_W + cx + 4 + d] = COL_GREEN;
                                }
                            } else {
                                // Not calibrated: show "?"
                                draw_string(cx - 4, cy - 8, "?", COL_RED);
                            }
                        }

                        rune_y += rune_row_h;
                    }
                }

                // Item count at bottom
                char count_str[32];
                snprintf(count_str, sizeof(count_str), "%d/%d", pmenu->selected_skill + 1, total_items);
                draw_string(20, SCR_H - 30, count_str, COL_GRAY);

                // Hint text at bottom
                draw_string(100, SCR_H - 30, "Scroll to view", COL_GRAY);
            }
        }
        // Draw Menu (except pet stats which is full screen)
        else if (g_game_state.menu_state != MENU_NONE) {
             int mw = 260; int mh = 300;
             int mx = (SCR_W - mw) / 2; int my = (SCR_H - mh) / 2;
             draw_rect(mx-4, my-4, mw+8, mh+8, COL_WHITE);
             draw_rect(mx, my, mw, mh, COL_DARK);
             draw_string(mx + 80, my + 10, "MENU", COL_WHITE);

             int start_y = my + 40;
             int row_h = 40;

             if (g_game_state.menu_state == MENU_MAIN) {
                 // Scrollable main menu (6 visible items)
                 int visible_count = 6;
                 int small_row_h = 36;
                 int scroll_off = g_game_state.menu_scroll_offset;

                 for (int i = 0; i < visible_count && (i + scroll_off) < OPT_MAIN_COUNT; i++) {
                     int opt_idx = i + scroll_off;
                     uint16_t col = (opt_idx == g_game_state.menu_selection) ? COL_RED : COL_GRAY;
                     draw_string(mx + 40, start_y + i * small_row_h, main_opts[opt_idx], col);
                     if (opt_idx == g_game_state.menu_selection) {
                         draw_string(mx + 20, start_y + i * small_row_h, ">", COL_RED);
                     }
                 }

                 // Show scroll indicators
                 if (scroll_off > 0) {
                     draw_string(mx + 120, my + 28, "^", COL_GRAY);
                 }
                 if (scroll_off + visible_count < OPT_MAIN_COUNT) {
                     draw_string(mx + 120, my + mh - 30, "v", COL_GRAY);
                 }
             } else if (g_game_state.menu_state == MENU_SETTINGS_MAIN) {
                 // Settings menu
                 const char *settings_opts[] = {"BRIGHTNESS", "SOUND", "CALIBRATE", "BACK"};
                 for (int i = 0; i < 4; i++) {
                     uint16_t col = (i == g_game_state.menu_selection) ? COL_RED : COL_GRAY;
                     draw_string(mx + 40, start_y + i * row_h, settings_opts[i], col);
                     if (i == g_game_state.menu_selection) {
                         draw_string(mx + 20, start_y + i * row_h, ">", COL_RED);
                     }
                 }
             } else if (g_game_state.menu_state == MENU_SETTINGS_BRIGHTNESS) {
                 draw_string(mx + 40, start_y, "BRIGHTNESS", COL_WHITE);
                 char bright_str[32];
                 snprintf(bright_str, sizeof(bright_str), "Level: %d", g_game_state.brightness);
                 draw_string(mx + 40, start_y + row_h, bright_str, COL_GRAY);
                 draw_string(mx + 40, start_y + row_h * 2, "Use knob to adjust", COL_GRAY);
                 draw_string(mx + 40, start_y + row_h * 3, "Click to go back", COL_GRAY);
             } else if (g_game_state.menu_state == MENU_SETTINGS_SOUND) {
                 draw_string(mx + 40, start_y, "SOUND", COL_WHITE);
                 draw_string(mx + 40, start_y + row_h, g_game_state.sound_on ? "ON" : "OFF",
                             g_game_state.sound_on ? COL_GREEN : COL_RED);
                 draw_string(mx + 40, start_y + row_h * 2, "Click to toggle", COL_GRAY);
             } else if (g_game_state.menu_state == MENU_TELEPORT) {
                 // Draw teleport menu with scrollable map list
                 draw_string(mx + 60, my + 10, "TELEPORT", COL_WHITE);

                 TeleportState *tp = &g_game_state.teleport;

                 if (tp->loading) {
                     draw_string(mx + 40, start_y, "Loading maps...", COL_GRAY);
                 } else if (tp->map_count == 0) {
                     draw_string(mx + 40, start_y, "No maps discovered!", COL_RED);
                     draw_string(mx + 40, start_y + row_h, "Explore more to", COL_GRAY);
                     draw_string(mx + 40, start_y + row_h * 2, "find new areas.", COL_GRAY);
                 } else {
                     // Draw map list with scrolling (6 visible items)
                     int visible_count = 6;
                     int small_row_h = 36;

                     for (int i = 0; i < visible_count && (i + tp->scroll_offset) < tp->map_count; i++) {
                         int map_idx = i + tp->scroll_offset;
                         DiscoveredMap *map = &tp->maps[map_idx];

                         uint16_t col = (map_idx == tp->selected_index) ? COL_RED : COL_GRAY;
                         char map_str[48];
                         snprintf(map_str, sizeof(map_str), "(%d,%d) %s", map->x, map->y, map->biome);
                         draw_string(mx + 40, start_y + i * small_row_h, map_str, col);

                         if (map_idx == tp->selected_index) {
                             draw_string(mx + 20, start_y + i * small_row_h, ">", COL_RED);
                         }
                     }

                     // Show scroll indicators
                     if (tp->scroll_offset > 0) {
                         draw_string(mx + 120, my + 28, "^", COL_GRAY);
                     }
                     if (tp->scroll_offset + visible_count < tp->map_count) {
                         draw_string(mx + 120, my + mh - 30, "v", COL_GRAY);
                     }

                     // Show count
                     char count_str[32];
                     snprintf(count_str, sizeof(count_str), "%d/%d maps", tp->selected_index + 1, tp->map_count);
                     draw_string(mx + 40, my + mh - 25, count_str, COL_GRAY);
                 }
             } else if (g_game_state.menu_state == MENU_CLONING_CENTER ||
                        g_game_state.menu_state == MENU_DNA_INVENTORY) {
                 // Draw Cloning Center / DNA Inventory
                 CloningState *clone = &g_game_state.cloning;
                 DNAInventoryState *inv = &g_game_state.dna_inventory;

                 // Title
                 if (g_game_state.menu_state == MENU_CLONING_CENTER) {
                     draw_string(mx + 30, my + 10, "CLONING CENTER", COL_WHITE);

                     // Show selection step
                     if (clone->selection_step == 0) {
                         draw_string(mx + 20, my + 40, "Select 1st DNA:", COL_YELLOW);
                     } else if (clone->selection_step == 1) {
                         draw_string(mx + 20, my + 40, "Select 2nd DNA:", COL_YELLOW);
                     } else if (clone->selection_step == 2) {
                         draw_string(mx + 20, my + 40, "Click to clone!", COL_GREEN);
                     }
                 } else {
                     draw_string(mx + 40, my + 10, "DNA INVENTORY", COL_WHITE);
                 }

                 if (inv->loading) {
                     draw_string(mx + 40, start_y, "Loading DNA...", COL_GRAY);
                 } else if (inv->sample_count == 0) {
                     draw_string(mx + 40, start_y, "No DNA samples!", COL_RED);
                     draw_string(mx + 20, start_y + row_h, "Defeat enemies to", COL_GRAY);
                     draw_string(mx + 20, start_y + row_h * 2, "collect DNA.", COL_GRAY);
                 } else {
                     // Draw DNA list
                     int visible_count = 5;
                     int small_row_h = 36;
                     int list_start_y = start_y + 10;

                     for (int i = 0; i < visible_count && (i + inv->scroll_offset) < inv->sample_count; i++) {
                         int idx = i + inv->scroll_offset;
                         DNASample *dna = &inv->samples[idx];

                         // Check if this sample is already selected
                         bool is_selected_1 = (clone->selected_sample_1 == idx);
                         bool is_selected_2 = (clone->selected_sample_2 == idx);
                         uint16_t col = (idx == inv->selected_index) ? COL_RED : COL_GRAY;
                         if (is_selected_1) col = COL_GREEN;
                         if (is_selected_2) col = COL_CYAN;

                         char dna_str[48];
                         snprintf(dna_str, sizeof(dna_str), "%s/%s Lv%d", dna->species, dna->element, dna->level);
                         draw_string(mx + 40, list_start_y + i * small_row_h, dna_str, col);

                         if (idx == inv->selected_index) {
                             draw_string(mx + 20, list_start_y + i * small_row_h, ">", COL_RED);
                         }
                         if (is_selected_1) {
                             draw_string(mx + mw - 40, list_start_y + i * small_row_h, "1", COL_GREEN);
                         }
                         if (is_selected_2) {
                             draw_string(mx + mw - 40, list_start_y + i * small_row_h, "2", COL_CYAN);
                         }
                     }

                     // Show count
                     char dna_count_str[32];
                     snprintf(dna_count_str, sizeof(dna_count_str), "%d samples", inv->sample_count);
                     draw_string(mx + 40, my + mh - 25, dna_count_str, COL_GRAY);
                 }

                 // Show cloning result if available
                 if (clone->show_result) {
                     // Draw result overlay (filled black background)
                     draw_rect(mx + 20, my + 60, mw - 40, 120, COL_BLACK);
                     // Green border
                     for (int i = 0; i < mw - 40; i++) {
                         frame_buf[(my + 60) * SCR_W + mx + 20 + i] = COL_GREEN;
                         frame_buf[(my + 179) * SCR_W + mx + 20 + i] = COL_GREEN;
                     }
                     for (int j = 0; j < 120; j++) {
                         frame_buf[(my + 60 + j) * SCR_W + mx + 20] = COL_GREEN;
                         frame_buf[(my + 60 + j) * SCR_W + mx + 20 + mw - 41] = COL_GREEN;
                     }
                     draw_string(mx + 40, my + 80, "NEW PET!", COL_GREEN);
                     char result_str[48];
                     snprintf(result_str, sizeof(result_str), "%s/%s", clone->result_species, clone->result_element);
                     draw_string(mx + 40, my + 110, result_str, COL_WHITE);
                     char level_str[16];
                     snprintf(level_str, sizeof(level_str), "Level %d", clone->result_level);
                     draw_string(mx + 40, my + 140, level_str, COL_YELLOW);
                     draw_string(mx + 20, my + 165, "Long press to close", COL_GRAY);
                 }

             } else if (g_game_state.menu_state == MENU_PET_YARD) {
                 // Draw Pet Yard (pet selection for switching)
                 draw_string(mx + 60, my + 10, "PET YARD", COL_WHITE);
                 draw_string(mx + 20, my + 40, "Select pet to switch:", COL_YELLOW);

                 PetYardState *yard = &g_game_state.pet_yard;

                 if (yard->loading) {
                     draw_string(mx + 40, start_y, "Loading pets...", COL_GRAY);
                 } else if (yard->pet_count == 0) {
                     draw_string(mx + 40, start_y, "No pets found!", COL_RED);
                 } else {
                     int visible_count = 5;
                     int small_row_h = 36;
                     int list_start_y = start_y + 10;

                     for (int i = 0; i < visible_count && (i + yard->scroll_offset) < yard->pet_count; i++) {
                         int idx = i + yard->scroll_offset;
                         PetListEntry *pet = &yard->pets[idx];

                         // Check if this is the active pet
                         bool is_active = (strcmp(pet->id, g_game_state.active_pet.id) == 0);
                         uint16_t col = (idx == yard->selected_index) ? COL_RED : COL_GRAY;
                         if (is_active) col = COL_GREEN;

                         char pet_str[48];
                         snprintf(pet_str, sizeof(pet_str), "%s Lv%d %d/%d", pet->nickname, pet->level, pet->current_hp, pet->max_hp);
                         draw_string(mx + 40, list_start_y + i * small_row_h, pet_str, col);

                         if (idx == yard->selected_index) {
                             draw_string(mx + 20, list_start_y + i * small_row_h, ">", COL_RED);
                         }
                         if (is_active) {
                             draw_string(mx + mw - 40, list_start_y + i * small_row_h, "*", COL_GREEN);
                         }
                     }

                     // Show count
                     char pet_count_str[32];
                     snprintf(pet_count_str, sizeof(pet_count_str), "%d pets", yard->pet_count);
                     draw_string(mx + 40, my + mh - 25, pet_count_str, COL_GRAY);
                 }
             }
        }

        esp_lcd_panel_draw_bitmap(panel_handle, 0, 0, SCR_W, SCR_H, frame_buf);
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

// ... (Rest of callbacks same as before)
static void on_knob_left(void *arg, void *data) {
    ESP_LOGI(TAG, "Knob LEFT");
    if (g_game_state.menu_state != MENU_NONE || g_game_state.mode == MODE_CALIBRATION) {
        game_handle_knob_rotate(&g_game_state, true);
    }
}

static void on_knob_right(void *arg, void *data) {
    ESP_LOGI(TAG, "Knob RIGHT");
    if (g_game_state.menu_state != MENU_NONE || g_game_state.mode == MODE_CALIBRATION) {
         game_handle_knob_rotate(&g_game_state, false);
    }
}

static void init_knob(void) {
    knob_config_t knob_cfg = {
        .default_direction = 0,
        .gpio_encoder_a = BSP_KNOB_A,
        .gpio_encoder_b = BSP_KNOB_B,
    };
    knob_handle = iot_knob_create(&knob_cfg);
    if (knob_handle) {
        iot_knob_register_cb(knob_handle, KNOB_LEFT, on_knob_left, NULL);
        iot_knob_register_cb(knob_handle, KNOB_RIGHT, on_knob_right, NULL);
        ESP_LOGI(TAG, "Knob initialized successfully");
    } else {
        ESP_LOGE(TAG, "Failed to initialize knob");
    }
}

static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void wifi_init_sta(void) {
    s_wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, NULL);
    wifi_config_t wifi_config = { .sta = { .ssid = WIFI_SSID, .password = WIFI_PASS, .threshold.authmode = WIFI_AUTH_WPA2_PSK } };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

void input_task(void *pvParameters) {
    uint16_t x, y, str;
    uint8_t pts = 0;
    while (1) {
        if (tp_handle) { 
            esp_lcd_touch_read_data(tp_handle);
            esp_lcd_touch_get_coordinates(tp_handle, &x, &y, &str, &pts, 1);
            // Removed: if (pts > 0) { ESP_LOGI(TAG, "Touch detected: x=%d, y=%d, strength=%d", x, y, str); }
            game_handle_touch(&g_game_state, x, y, (pts > 0));
        }
        vTaskDelay(pdMS_TO_TICKS(50)); 
    }
}

void knob_task(void *pvParameters) {
    bool last_level = 1; 
    int64_t press_start_time = 0;
    while(1) {
        int level = bsp_exp_io_get_level(BSP_KNOB_BTN);
        int64_t now = esp_timer_get_time() / 1000; 
        if (last_level == 1 && level == 0) {
            press_start_time = now;
        } else if (last_level == 0 && level == 1) {
            int64_t duration = now - press_start_time;
            if (duration > 50 && duration < 800) {
                if (g_game_state.menu_state != MENU_SLEEP) game_handle_button_click(&g_game_state);
            } else if (duration >= 800) {
                if (g_game_state.menu_state == MENU_SLEEP) g_game_state.menu_state = MENU_MAIN;
                else game_handle_menu_open_combo(&g_game_state);
            }
        }
        last_level = level;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// --- Pet Data Callbacks ---
static char g_pet_species[32] = {0};
static char g_pet_element[32] = {0};

static void on_pet_info(const char *id, const char *nickname, const char *species,
                        const char *element, const char *rarity,
                        int level, int xp, int xp_to_next, int hp, int max_hp, int current_hp,
                        int atk, int def, int spd) {
    game_set_active_pet(&g_game_state, id, nickname, species, element, rarity,
                        level, xp, xp_to_next, hp, max_hp, current_hp, atk, def, spd);

    // Store species/element for sprite loading
    strncpy(g_pet_species, species, sizeof(g_pet_species) - 1);
    strncpy(g_pet_element, element, sizeof(g_pet_element) - 1);
}

static void on_pet_skill(int skill_index, const char *id, const char *name, const char *type,
                         int power, int accuracy, const char *effect, int effect_chance, bool has_gesture,
                         const int8_t *gesture_x, const int8_t *gesture_y, int gesture_count) {
    game_add_pet_skill(&g_game_state, skill_index, id, name, type, power, accuracy,
                       effect, effect_chance, has_gesture, gesture_x, gesture_y, gesture_count);
}

static void on_skill_tree_entry(const char *id, const char *name, const char *type,
                                int power, int unlock_level, bool unlocked, bool has_gesture) {
    game_add_skill_tree_entry(&g_game_state, id, name, type, power, unlock_level, unlocked, has_gesture);
}

// --- Teleport Callback ---
static void discovered_map_callback(int x, int y, const char *biome) {
    game_add_discovered_map(&g_game_state, x, y, biome);
}

// --- Building Callback ---
static void building_callback(const char *name, int tile_x, int tile_y,
                              int width, int height, bool interactive,
                              int zone_x, int zone_y, int zone_w, int zone_h) {
    game_add_building(&g_game_state, name, tile_x, tile_y, width, height, interactive,
                      zone_x, zone_y, zone_w, zone_h);
}

// --- DNA Sample Callback ---
static void dna_sample_callback(const char *id, const char *species,
                                const char *element, const char *rarity, int level) {
    game_add_dna_sample(&g_game_state, id, species, element, rarity, level);
}

// --- Pet List Callback ---
static void pet_list_callback(const char *id, const char *nickname,
                              const char *species, const char *element,
                              int level, int current_hp, int max_hp) {
    game_add_pet_entry(&g_game_state, id, nickname, species, element, level, current_hp, max_hp);
}

// --- Uncalibrated Skill Callback ---
static void uncalibrated_skill_callback(const char *skill_id, const char *name) {
    game_calibration_add_skill(&g_game_state, skill_id, name, 0, false);
}

// --- Rune Calibration Callback ---
static void rune_calibration_callback(const char *rune_id, const char *display_name, int samples, bool calibrated) {
    // Only add uncalibrated runes to the list
    if (!calibrated) {
        game_calibration_add_skill(&g_game_state, rune_id, display_name, samples, calibrated);
    }
}

// --- Available Rune Callback (for battle) ---
static void available_rune_callback(const char *rune_id, const char *name) {
    game_rune_battle_add_available(&g_game_state, rune_id, name);
}

// --- Enemy Chain Callback ---
static void enemy_chain_callback(int index, const char *rune_id, const char *name, int power) {
    game_rune_battle_add_enemy_rune(&g_game_state, index, rune_id, name, power);
}

// --- Rune Tree Callback (for pet stats menu) ---
static void rune_tree_callback(const char *rune_id, const char *name, const char *source,
                               const char *element, int power, int unlock_level,
                               bool unlocked, bool calibrated) {
    game_add_rune_tree_entry(&g_game_state, rune_id, name, source, element,
                             power, unlock_level, unlocked, calibrated);
}

void app_main(void)
{
#ifdef VIDEO_STREAM_MODE
    video_stream_main();
    while(1) { vTaskDelay(pdMS_TO_TICKS(1000)); }
    return;
#endif

    uart_set_baudrate(UART_NUM_0, 115200);
    vTaskDelay(pdMS_TO_TICKS(100));

    // 1. Init Hardware
    bsp_i2c_bus_init();
    bsp_io_expander_init();
    init_knob();  // Initialize rotary encoder

    // Give hardware (Touch/LCD) time to power up
    vTaskDelay(pdMS_TO_TICKS(500));

    // 3. Init Audio/Display
    i2s_std_config_t i2s_config = BSP_I2S_DUPLEX_MONO_CFG(16000);
    bsp_audio_init(&i2s_config);
    bsp_codec_init();
    bsp_lcd_brightness_set(100); 
    
    bsp_lvgl_init(); 
    lvgl_port_lock(0); 

    // Manual Touch Init (After LCD Init)
    vTaskDelay(pdMS_TO_TICKS(200)); // Extra delay for safety
    
    const i2c_config_t i2c_conf_touch = { 
        .mode = I2C_MODE_MASTER,
        .sda_io_num = BSP_TOUCH_I2C_SDA,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_io_num = BSP_TOUCH_I2C_SCL,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = BSP_TOUCH_I2C_CLK 
    };
    // Check if driver is already installed (it shouldn't be if disabled in Kconfig)
    i2c_param_config(BSP_TOUCH_I2C_NUM, &i2c_conf_touch);
    esp_err_t i2c_ret = i2c_driver_install(BSP_TOUCH_I2C_NUM, i2c_conf_touch.mode, 0, 0, ESP_INTR_FLAG_SHARED);
    if (i2c_ret != ESP_OK && i2c_ret != ESP_ERR_INVALID_STATE) { // INVALID_STATE means already installed
        ESP_LOGE(TAG, "Touch I2C Driver Install Failed: %s", esp_err_to_name(i2c_ret));
    }

    const esp_lcd_touch_config_t tp_cfg = {
        .x_max = DRV_LCD_H_RES, .y_max = DRV_LCD_V_RES,
        .flags = { .swap_xy = DRV_LCD_SWAP_XY, .mirror_x = DRV_LCD_MIRROR_X, .mirror_y = DRV_LCD_MIRROR_Y },
    };
    static esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_SPD2010_CONFIG();
    esp_lcd_panel_io_handle_t tp_io_handle = NULL;
    esp_lcd_new_panel_io_i2c(BSP_TOUCH_I2C_NUM, &tp_io_config, &tp_io_handle);
    
    // Try to Init Touch
    esp_err_t tp_ret = esp_lcd_touch_new_i2c_spd2010(tp_io_handle, &tp_cfg, &tp_handle);
    if (tp_ret != ESP_OK) {
         ESP_LOGE(TAG, "Touch Panel Init Failed: %s", esp_err_to_name(tp_ret));
    } else {
         ESP_LOGI(TAG, "Touch Panel Initialized Successfully");
    } 
    
    nvs_flash_init();
    wifi_init_sta();
    
    // Wait for Wi-Fi before loading assets
    xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdFALSE, portMAX_DELAY);

    // Initialize game state BEFORE loading any data (so it doesn't get zeroed out)
    game_init(&g_game_state);

    // Load Assets from Server
    load_assets_from_sd(); // Rename this function later, it's misleading now!
    init_asset_arrays();
    // Load generic spell sprites for enemy attacks and gesture fallback
    fetch_spell_sprites();

    // Fetch active pet data from server
    ESP_LOGI(TAG, "Fetching active pet...");
    int pet_result = fetch_active_pet(on_pet_info, on_pet_skill, on_skill_tree_entry);
    if (pet_result == 0) {
        ESP_LOGI(TAG, "Active pet loaded successfully: %s/%s", g_pet_species, g_pet_element);

        // Load pet sprites based on species/element
        if (g_pet_species[0] != '\0' && g_pet_element[0] != '\0') {
            ESP_LOGI(TAG, "Loading pet sprites for %s_%s...", g_pet_species, g_pet_element);
            int sprite_result = fetch_pet_sprites(g_pet_species, g_pet_element);
            if (sprite_result == 0) {
                ESP_LOGI(TAG, "Pet sprites loaded successfully!");
            } else {
                ESP_LOGW(TAG, "Failed to load pet sprites, using fox fallback");
            }
        }

        // Load skill sprites for pet's active skills
        ActivePetState *pet = &g_game_state.active_pet;
        if (pet->loaded && pet->skill_count > 0) {
            ESP_LOGI(TAG, "Loading skill sprites for %d skills...", pet->skill_count);
            const char *skill_ids[MAX_SKILL_SPRITES];
            int count = (pet->skill_count < MAX_SKILL_SPRITES) ? pet->skill_count : MAX_SKILL_SPRITES;
            for (int i = 0; i < count; i++) {
                skill_ids[i] = pet->skills[i].id;
            }
            int skills_loaded = fetch_skill_sprites(skill_ids, count);
            ESP_LOGI(TAG, "Skill sprites loaded: %d/%d", skills_loaded, count);
        }

        // Fetch rune tree for pet stats menu
        if (pet->loaded) {
            ESP_LOGI(TAG, "Fetching rune tree for %s/%s Lv%d...",
                     pet->species, pet->element, pet->level);
            game_clear_rune_tree(&g_game_state);
            int rune_count = fetch_pet_rune_tree(pet->species, pet->element, pet->level,
                                                  rune_tree_callback);
            ESP_LOGI(TAG, "Rune tree loaded: %d runes", rune_count);
        }
    } else {
        ESP_LOGW(TAG, "Failed to load active pet (will retry later)");
    }

    // 4. Tasks
    xTaskCreatePinnedToCore(game_render_task, "game", 8192, NULL, 5, NULL, 1);
    xTaskCreatePinnedToCore(input_task, "input", 4096, NULL, 5, NULL, 1);
    xTaskCreatePinnedToCore(knob_task, "knob", 4096, NULL, 5, NULL, 1); 
}