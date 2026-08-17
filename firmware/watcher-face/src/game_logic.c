#include "game_logic.h"
#include "generated_assets.h" // Include this for current_collision_data
#include <string.h>
#include <stdlib.h>
#include <stdio.h>  

// Constants
#define WORLD_WIDTH (MAP_PX_W) // Use TileMap width
#define ANIM_SPEED 3 
#define JUMP_SWIPE_DIST 40 

// Settings Range
#define BRIGHTNESS_MIN 10
#define BRIGHTNESS_MAX 255
#define BRIGHTNESS_STEP 10

// Helper: Clamp value
static int32_t clamp(int32_t val, int32_t min, int32_t max) {
    if (val < min) return min;
    if (val > max) return max;
    return val;
}



void game_init(GameState *state) {
    memset(state, 0, sizeof(GameState));
    
    // Initialize World Coordinates
    state->world_grid_x = 0;
    state->world_grid_y = 0;
    
    // generate_procedural_map(state); // Removed client-side procedural map generation
    state->mode = MODE_OVERWORLD;
    state->player.vy = 0;
    state->player.facing_right = true;
    
    // Defaults
    state->brightness = BRIGHTNESS_MAX; // Full brightness
    state->sound_on = true;
    state->menu_state = MENU_NONE;
}

// --- Menu Logic ---

static void update_menu(GameState *state) {
    InputState *in = &state->input;

    // Handle button click (Select)
    if (in->btn_clicked) {
        switch (state->menu_state) {
            case MENU_NONE:
            case MENU_SLEEP:
                break;  // Do nothing for these states
            case MENU_MAIN:
                switch(state->menu_selection) {
                    case OPT_MAIN_PET: game_open_pet_stats(state); break;
                    case OPT_MAIN_SWITCH_PET: game_open_pet_yard(state); break;
                    case OPT_MAIN_CLONING: game_open_cloning_center(state); break;
                    case OPT_MAIN_INVENTORY: state->menu_state = MENU_INVENTORY; state->menu_selection = 0; break;
                    case OPT_MAIN_COLLECTION: state->menu_state = MENU_COLLECTION; state->menu_selection = 0; break;
                    case OPT_MAIN_TELEPORT: game_open_teleport(state); break;
                    case OPT_MAIN_SLEEP: state->menu_state = MENU_SLEEP; break; // Handled by renderer
                    case OPT_MAIN_SETTINGS: state->menu_state = MENU_SETTINGS_MAIN; state->menu_selection = 0; break;
                    case OPT_MAIN_BACK: state->menu_state = MENU_NONE; break;
                }
                break;
            case MENU_SETTINGS_MAIN:
                switch(state->menu_selection) {
                    case OPT_SETTINGS_BRIGHTNESS: state->menu_state = MENU_SETTINGS_BRIGHTNESS; break;
                    case OPT_SETTINGS_SOUND: state->menu_state = MENU_SETTINGS_SOUND; break;
                    case OPT_SETTINGS_CALIBRATE:
                        // Start calibration mode
                        game_start_calibration(state);
                        break;
                    case OPT_SETTINGS_BACK: state->menu_state = MENU_MAIN; state->menu_selection = OPT_MAIN_SETTINGS; break; // Go back to main menu
                }
                break;
            case MENU_SETTINGS_BRIGHTNESS:
                // Nothing to select here, only adjust with knob. Back returns to main settings.
                state->menu_state = MENU_SETTINGS_MAIN; state->menu_selection = OPT_SETTINGS_BRIGHTNESS;
                break;
            case MENU_SETTINGS_SOUND:
                // Toggle sound
                state->sound_on = !state->sound_on;
                // Back to main settings
                state->menu_state = MENU_SETTINGS_MAIN; state->menu_selection = OPT_SETTINGS_SOUND;
                break;
            case MENU_INVENTORY:
            case MENU_COLLECTION:
                state->menu_state = MENU_MAIN; state->menu_selection = OPT_MAIN_INVENTORY; // Back to main menu
                break;
            case MENU_PET_STATS:
                game_update_pet_stats_menu(state);
                break;
            case MENU_TELEPORT:
                game_teleport_select(state);
                break;
            case MENU_CLONING_CENTER:
                // In cloning center, select confirms current action
                if (state->cloning.show_result) {
                    // Click on result dismisses and returns to game
                    game_cloning_back(state);
                } else if (state->cloning.selection_step == 2) {
                    game_cloning_confirm(state);
                } else {
                    game_cloning_select_sample(state);
                }
                break;
            case MENU_PET_YARD:
                game_pet_yard_select(state);
                break;
            case MENU_DNA_INVENTORY:
                // In standalone DNA view, click goes back
                game_dna_inventory_back(state);
                break;
        }
    }

    // Handle long press to go back (menu open combo)
    if (in->btn_menu_open_combo) {
        switch (state->menu_state) {
            case MENU_TELEPORT:
                game_teleport_back(state);
                break;
            case MENU_SETTINGS_BRIGHTNESS:
            case MENU_SETTINGS_SOUND:
                state->menu_state = MENU_SETTINGS_MAIN;
                break;
            case MENU_SETTINGS_MAIN:
            case MENU_INVENTORY:
            case MENU_COLLECTION:
                state->menu_state = MENU_MAIN;
                break;
            case MENU_CLONING_CENTER:
                game_cloning_back(state);
                break;
            case MENU_PET_YARD:
                game_pet_yard_back(state);
                break;
            case MENU_DNA_INVENTORY:
                game_dna_inventory_back(state);
                break;
            default:
                break;
        }
    }

    // Handle knob rotation (Navigate)
    if (in->knob_rotated_left) {
        state->menu_selection--;
    } else if (in->knob_rotated_right) {
        state->menu_selection++;
    }

    // Clamp menu selection based on current menu state
    int max_selection = 0;
    switch (state->menu_state) {
        case MENU_MAIN:
            max_selection = OPT_MAIN_COUNT - 1;
            // Clamp selection first to prevent overflow/underflow
            if (state->menu_selection < 0) state->menu_selection = 0;
            if (state->menu_selection > max_selection) state->menu_selection = max_selection;
            // Update scroll offset to keep selection visible (6 visible items)
            if (state->menu_selection < state->menu_scroll_offset) {
                state->menu_scroll_offset = state->menu_selection;
            }
            if (state->menu_selection >= state->menu_scroll_offset + 6) {
                state->menu_scroll_offset = state->menu_selection - 5;
            }
            // Clamp scroll offset too
            if (state->menu_scroll_offset < 0) state->menu_scroll_offset = 0;
            break;
        case MENU_SETTINGS_MAIN: max_selection = OPT_SETTINGS_COUNT - 1; break;
        case MENU_SETTINGS_BRIGHTNESS: // Brightness only scrolls, no selection.
        {
            if (in->knob_rotated_left) state->brightness -= BRIGHTNESS_STEP;
            if (in->knob_rotated_right) state->brightness += BRIGHTNESS_STEP;
            state->brightness = clamp(state->brightness, BRIGHTNESS_MIN, BRIGHTNESS_MAX);
            break;
        }
        case MENU_SETTINGS_SOUND:
            // No scrolling, only toggle
            break;
        case MENU_INVENTORY:
        case MENU_COLLECTION:
            // Placeholder: Assume 1 option ("Back") for now.
            max_selection = 0;
            break;
        case MENU_PET_STATS:
            // Pet stats menu uses pet_menu.selected_skill for navigation
            // rune_tree_count items + 1 BACK option (shows full rune tree)
            {
                int max_item = state->active_pet.rune_tree_count; // BACK is at index rune_tree_count
                if (in->knob_rotated_left && state->pet_menu.selected_skill > 0) {
                    state->pet_menu.selected_skill--;
                } else if (in->knob_rotated_right && state->pet_menu.selected_skill < max_item) {
                    state->pet_menu.selected_skill++;
                }
                // Update scroll offset (3 visible items)
                if (state->pet_menu.selected_skill < state->pet_menu.scroll_offset) {
                    state->pet_menu.scroll_offset = state->pet_menu.selected_skill;
                }
                if (state->pet_menu.selected_skill >= state->pet_menu.scroll_offset + 3) {
                    state->pet_menu.scroll_offset = state->pet_menu.selected_skill - 2;
                }
                if (state->pet_menu.scroll_offset < 0) state->pet_menu.scroll_offset = 0;
            }
            break;
        case MENU_TELEPORT:
            // Teleport menu uses teleport.selected_index for navigation
            if (in->knob_rotated_left && state->teleport.selected_index > 0) {
                state->teleport.selected_index--;
                // Adjust scroll offset to keep selection visible
                if (state->teleport.selected_index < state->teleport.scroll_offset) {
                    state->teleport.scroll_offset = state->teleport.selected_index;
                }
            } else if (in->knob_rotated_right && state->teleport.selected_index < state->teleport.map_count - 1) {
                state->teleport.selected_index++;
                // Adjust scroll offset to keep selection visible (show 6 items at a time)
                if (state->teleport.selected_index >= state->teleport.scroll_offset + 6) {
                    state->teleport.scroll_offset = state->teleport.selected_index - 5;
                }
            }
            break;
        case MENU_CLONING_CENTER:
        case MENU_DNA_INVENTORY:
            // Navigate DNA sample list
            if (in->knob_rotated_left && state->dna_inventory.selected_index > 0) {
                state->dna_inventory.selected_index--;
                if (state->dna_inventory.selected_index < state->dna_inventory.scroll_offset) {
                    state->dna_inventory.scroll_offset = state->dna_inventory.selected_index;
                }
            } else if (in->knob_rotated_right && state->dna_inventory.selected_index < state->dna_inventory.sample_count - 1) {
                state->dna_inventory.selected_index++;
                if (state->dna_inventory.selected_index >= state->dna_inventory.scroll_offset + 6) {
                    state->dna_inventory.scroll_offset = state->dna_inventory.selected_index - 5;
                }
            }
            break;
        case MENU_PET_YARD:
            // Navigate pet list
            if (in->knob_rotated_left && state->pet_yard.selected_index > 0) {
                state->pet_yard.selected_index--;
                if (state->pet_yard.selected_index < state->pet_yard.scroll_offset) {
                    state->pet_yard.scroll_offset = state->pet_yard.selected_index;
                }
            } else if (in->knob_rotated_right && state->pet_yard.selected_index < state->pet_yard.pet_count - 1) {
                state->pet_yard.selected_index++;
                if (state->pet_yard.selected_index >= state->pet_yard.scroll_offset + 6) {
                    state->pet_yard.scroll_offset = state->pet_yard.selected_index - 5;
                }
            }
            break;
        default: break;
    }
    state->menu_selection = clamp(state->menu_selection, 0, max_selection);
}

// --- Platformer Logic ---
static void update_platformer(GameState *state) {
    PlayerState *p = &state->player;
    InputState *in = &state->input;

    int move_dir = 0;

    // Check for Menu Open Combo
    if (in->btn_menu_open_combo) {
        state->menu_state = MENU_MAIN;
        state->menu_selection = 0; // Reset selection to first item
        state->menu_scroll_offset = 0; // Reset scroll
        return;
    }

    // --- Input Processing ---
    // Only process touch for game if not in menu
    if (in->is_touching) {
        // Horizontal Move
        int diff_x = in->touch_x - in->start_x;
        if (diff_x > 20) {
            move_dir = 1;
            p->facing_right = true;
        } else if (diff_x < -20) {
            move_dir = -1;
            p->facing_right = false;
        }

        // Jump (Swipe Up)
        int diff_y = in->touch_y - in->start_y;
        if (diff_y < -JUMP_SWIPE_DIST) {
             if (p->y >= FLOOR_Y) {
                p->vy = JUMP_FORCE;
                in->start_y = in->touch_y; 
            }
        }
    }

    // --- Physics ---
    p->x += move_dir * PLAYER_SPEED;
    p->x = clamp(p->x, 0, WORLD_WIDTH - FOX_W);

    p->vy += GRAVITY;
    p->y += p->vy;

    if (p->y >= FLOOR_Y) {
        p->y = FLOOR_Y;
        p->vy = 0;
    }

    // --- Animation ---
    if (move_dir != 0) {
        p->anim_timer++;
        if (p->anim_timer >= ANIM_SPEED) {
            p->frame_index = (p->frame_index + 1) % 8;
            p->anim_timer = 0;
        }
    } else {
        p->frame_index = 0;
        p->anim_timer = 0;
    }

    // --- Camera ---
    int target_cam_x = p->x - (SCR_W / 2) + (FOX_W / 2);
    state->cam_x = clamp(target_cam_x, 0, WORLD_WIDTH - SCR_W);
}

// Helper: Check if player is near an edge and on a valid walkable tile
static int check_valid_exit(int px, int py) {
    // Returns edge index (0-3) or -1 if no valid exit found
    
    // Define edge threshold (how close to edge to be considered "at exit")
    int threshold = 120; // Generous threshold
    
    int map_w = 1600;
    int map_h = 1600;
    
    int edge = -1;
    
    // Check which edge we are closest to
    if (py < threshold) edge = 0; // Top
    else if (py > map_h - threshold - FOX_H) edge = 1; // Bottom
    else if (px < threshold) edge = 2; // Left
    else if (px > map_w - threshold - FOX_W) edge = 3; // Right
    
    if (edge != -1) {
        // Check if walkable
        // Use center of feet for collision check
        int cx = px + FOX_W/2;
        int cy = py + FOX_H - 20;
        
        int tx = cx / TILE_SIZE; // 32
        int ty = cy / TILE_SIZE;
        
        if (current_collision_data) {
            if (tx >= 0 && tx < 50 && ty >= 0 && ty < 50) {
                // 0 is walkable, 1 is blocked
                if (current_collision_data[ty * 50 + tx] == 0) {
                    return edge;
                }
            }
        } else {
            // If no collision data, assume valid
            return edge;
        }
    }
    
    return -1;
}

// --- Overworld Logic (Top Down) ---
static void update_overworld(GameState *state) {
    PlayerState *p = &state->player;
    InputState *in = &state->input;

    // Check for Menu Open Combo
    if (in->btn_menu_open_combo) {
        state->menu_state = MENU_MAIN;
        state->menu_selection = 0; // Reset selection to first item
        state->menu_scroll_offset = 0; // Reset scroll
        return;
    }

    // Check for Map Transition (Click near map edge on walkable path)
    if (in->btn_clicked) {
        int exit_edge = check_valid_exit(p->x, p->y);
        if (exit_edge >= 0) {
            state->request_new_map = true;
            state->request_new_map_direction = 1; 
            state->exit_edge_used = exit_edge;
            
            // Update World Coordinates based on exit edge
            switch (state->exit_edge_used) {
                case 0: state->world_grid_y--; break; // Top (North)
                case 1: state->world_grid_y++; break; // Bottom (South)
                case 2: state->world_grid_x--; break; // Left (West)
                case 3: state->world_grid_x++; break; // Right (East)
            }
            return;
        }
    }

    if (in->is_touching) {
        // Virtual Joystick logic: Center of screen is (206, 206)
        // or Start Point is the center
        int dx = in->touch_x - in->start_x;
        int dy = in->touch_y - in->start_y;
        
        int new_x = p->x;
        int new_y = p->y;
        
        if (abs(dx) > 20) new_x += (dx > 0 ? PLAYER_SPEED : -PLAYER_SPEED);
        if (abs(dy) > 20) new_y += (dy > 0 ? PLAYER_SPEED : -PLAYER_SPEED);

        // Collision Detection (Center point of player)
        // Fox size ~140x118. Let's use a smaller hitbox for world nav (e.g. 64x64 at feet)
        int cx = new_x + FOX_W/2;
        int cy = new_y + FOX_H - 20;
        
        int tx = cx / TILE_SIZE;
        int ty = cy / TILE_SIZE;
        
        bool collision = false;
        if (tx >= 0 && tx < MAP_W_TILES && ty >= 0 && ty < MAP_H_TILES) {
             // New Collision Logic: Use downloaded collision grid
             if (current_collision_data) {
                 if (current_collision_data[ty * MAP_W_TILES + tx] == 1) collision = true;
             }
             // If no collision data, allow free movement (collision stays false)
        } else {
            // Only block if outside map bounds
            collision = true;
        }
        
        if (!collision) {
            p->x = new_x;
            p->y = new_y;
            
            if (dx > 20) p->facing_right = true;
            if (dx < -20) p->facing_right = false;
            
            // Animation
            p->anim_timer++;
            if (p->anim_timer >= ANIM_SPEED) {
                p->frame_index = (p->frame_index + 1) % 8;
                p->anim_timer = 0;
            }
        }
    } else {
        // Idle
        p->frame_index = 0; 
    }

    // Camera Follow
    int target_cam_x = p->x - (SCR_W / 2) + (FOX_W / 2);
    int target_cam_y = p->y - (SCR_H / 2) + (FOX_H / 2);
    
    state->cam_x = clamp(target_cam_x, 0, MAP_PX_W - SCR_W);
    state->cam_y = clamp(target_cam_y, 0, MAP_PX_H - SCR_H);
}

// --- Main Update Loop ---
void game_update(GameState *state) {
    if (state->menu_state != MENU_NONE) {
        update_menu(state);
    } else {
        if (state->mode == MODE_PLATFORMER) {
            update_platformer(state);
        } else if (state->mode == MODE_OVERWORLD) {
            update_overworld(state);
        }
    }

    // Clear "Just" flags AFTER processing
    state->input.just_pressed = false;
    state->input.just_released = false;
    state->input.btn_clicked = false;
    state->input.btn_menu_open_combo = false;
    state->input.knob_rotated_left = false;
    state->input.knob_rotated_right = false;
}

void game_handle_touch(GameState *state, int16_t x, int16_t y, bool is_pressed) {
    InputState *in = &state->input;
    GestureState *gest = &state->gesture;

    // Only record gestures during battle player turn, rune battle chain phase, or calibration mode
    bool can_draw = (state->mode == MODE_BATTLE && state->battle.phase == BATTLE_PHASE_PLAYER_TURN) ||
                    (state->mode == MODE_RUNE_BATTLE && state->rune_battle.phase == RUNE_PHASE_PLAYER_CHAIN) ||
                    (state->mode == MODE_CALIBRATION);  // Always allow drawing in calibration mode

    if (is_pressed && !in->is_touching) {
        // Finger just touched down - start new stroke
        in->just_pressed = true;
        in->start_x = x;
        in->start_y = y;

        if (can_draw) {
            gest->is_recording = true;
            // Mark the start of a new stroke
            if (gest->stroke_count < MAX_STROKES) {
                gest->stroke_starts[gest->stroke_count] = gest->point_count;
                gest->stroke_count++;
            }
        }
    }
    if (!is_pressed && in->is_touching) {
        // Finger just lifted - end current stroke
        in->just_released = true;

        if (gest->is_recording) {
            gest->is_recording = false;
            if (gest->point_count > 0) {
                gest->has_strokes = true;
            }
        }
    }

    in->is_touching = is_pressed;
    if (is_pressed) {
        in->touch_x = x;
        in->touch_y = y;

        // Record gesture point
        if (can_draw && gest->is_recording && gest->point_count < MAX_GESTURE_POINTS) {
            // Only add point if it moved enough from last point in current stroke
            bool should_add = true;

            // Get the start of current stroke to check distance
            int current_stroke_start = (gest->stroke_count > 0) ?
                gest->stroke_starts[gest->stroke_count - 1] : 0;

            // Only check distance within current stroke (not across strokes)
            if (gest->point_count > current_stroke_start) {
                int dx = x - gest->points[gest->point_count - 1].x;
                int dy = y - gest->points[gest->point_count - 1].y;
                if (dx * dx + dy * dy < 64) {  // Less than 8 pixels movement
                    should_add = false;
                }
            }

            if (should_add) {
                gest->points[gest->point_count].x = x;
                gest->points[gest->point_count].y = y;
                gest->point_count++;
            }
        }
    }
}

void game_confirm_gesture(GameState *state) {
    // Called when wheel is clicked - confirms gesture for sending
    GestureState *gest = &state->gesture;
    if (gest->has_strokes && gest->point_count > 2) {
        gest->gesture_ready = true;
    }
}

void game_clear_gesture(GameState *state) {
    // Clear all gesture data (after sending or to cancel)
    GestureState *gest = &state->gesture;
    gest->point_count = 0;
    gest->stroke_count = 0;
    gest->is_recording = false;
    gest->gesture_ready = false;
    gest->has_strokes = false;
}

void game_handle_button_click(GameState *state) {
    state->input.btn_clicked = true;
}

void game_handle_menu_open_combo(GameState *state) {
    state->input.btn_menu_open_combo = true;
}

void game_handle_knob_rotate(GameState *state, bool left) {
    if (left) {
        state->input.knob_rotated_left = true;
    } else {
        state->input.knob_rotated_right = true;
    }
}

// --- GESTURE RECOGNITION ---
// Simple shape matching based on direction changes and bounding box ratio

SpellType game_recognize_gesture(GestureState *gest) {
    if (gest->point_count < 5) return SPELL_BASIC;

    // Calculate bounding box
    int min_x = 9999, max_x = -9999;
    int min_y = 9999, max_y = -9999;
    for (int i = 0; i < gest->point_count; i++) {
        if (gest->points[i].x < min_x) min_x = gest->points[i].x;
        if (gest->points[i].x > max_x) max_x = gest->points[i].x;
        if (gest->points[i].y < min_y) min_y = gest->points[i].y;
        if (gest->points[i].y > max_y) max_y = gest->points[i].y;
    }

    int width = max_x - min_x;
    int height = max_y - min_y;
    if (width < 30 || height < 30) return SPELL_BASIC;  // Too small

    // Normalize points to 0-100 range for analysis
    int norm_points[MAX_GESTURE_POINTS][2];
    for (int i = 0; i < gest->point_count; i++) {
        norm_points[i][0] = (gest->points[i].x - min_x) * 100 / (width > 0 ? width : 1);
        norm_points[i][1] = (gest->points[i].y - min_y) * 100 / (height > 0 ? height : 1);
    }

    // Count direction changes (peaks and valleys)
    int x_direction_changes = 0;
    int y_direction_changes = 0;
    int last_x_dir = 0;
    int last_y_dir = 0;

    for (int i = 1; i < gest->point_count; i++) {
        int dx = norm_points[i][0] - norm_points[i-1][0];
        int dy = norm_points[i][1] - norm_points[i-1][1];

        int x_dir = (dx > 5) ? 1 : (dx < -5) ? -1 : 0;
        int y_dir = (dy > 5) ? 1 : (dy < -5) ? -1 : 0;

        if (x_dir != 0 && x_dir != last_x_dir && last_x_dir != 0) x_direction_changes++;
        if (y_dir != 0 && y_dir != last_y_dir && last_y_dir != 0) y_direction_changes++;

        if (x_dir != 0) last_x_dir = x_dir;
        if (y_dir != 0) last_y_dir = y_dir;
    }

    // Check if it's roughly circular (O) - start and end points close together
    int start_x = norm_points[0][0];
    int start_y = norm_points[0][1];
    int end_x = norm_points[gest->point_count - 1][0];
    int end_y = norm_points[gest->point_count - 1][1];
    int close_dist = (end_x - start_x) * (end_x - start_x) + (end_y - start_y) * (end_y - start_y);

    // Check aspect ratio
    float aspect = (float)width / (float)(height > 0 ? height : 1);

    // O: Circular, start/end close, similar width/height
    if (close_dist < 900 && aspect > 0.6 && aspect < 1.6 && x_direction_changes >= 2 && y_direction_changes >= 2) {
        return SPELL_O;
    }

    // M: Wide, 4 x-direction changes (peaks at top), starts/ends at bottom
    // Pattern: down-right, up-right, down-right, up-right, down-right
    if (x_direction_changes >= 3 && start_y > 60 && end_y > 60) {
        // Check for M pattern: multiple peaks
        int peaks = 0;
        for (int i = 2; i < gest->point_count - 2; i++) {
            if (norm_points[i][1] < norm_points[i-2][1] && norm_points[i][1] < norm_points[i+2][1]) {
                peaks++;
            }
        }
        if (peaks >= 2) return SPELL_M;
    }

    // W: Wide, 4 x-direction changes, starts/ends at top
    if (x_direction_changes >= 3 && start_y < 40 && end_y < 40) {
        // Check for W pattern: multiple valleys
        int valleys = 0;
        for (int i = 2; i < gest->point_count - 2; i++) {
            if (norm_points[i][1] > norm_points[i-2][1] && norm_points[i][1] > norm_points[i+2][1]) {
                valleys++;
            }
        }
        if (valleys >= 2) return SPELL_W;
    }

    // S: Serpentine, multiple y-direction changes, roughly vertical
    if (y_direction_changes >= 2 && aspect < 1.2) {
        return SPELL_S;
    }

    // N: Diagonal, 2 direction changes, starts bottom-left ends bottom-right or similar
    if (x_direction_changes >= 1 && x_direction_changes <= 3 && aspect > 0.5 && aspect < 2.0) {
        // N pattern: up, diagonal down, up
        bool goes_up_first = norm_points[gest->point_count/4][1] < norm_points[0][1];
        bool diagonal_middle = true;  // Simplified
        if (goes_up_first) return SPELL_N;
    }

    return SPELL_BASIC;
}

// --- BATTLE SYSTEM ---

// Check if player is near any mob (returns mob index or -1)
int game_check_nearby_mob(GameState *state) {
    // Need access to current_mobs from generated_assets
    // This will be called from helloworld.c which has access
    return -1;  // Placeholder - actual check done in helloworld.c
}

void game_start_battle(GameState *state, int mob_index) {
    state->mode = MODE_BATTLE;
    state->battle.phase = BATTLE_PHASE_INTRO;
    state->battle.player_hp = 100;
    state->battle.player_max_hp = 100;
    state->battle.enemy_hp = 80;
    state->battle.enemy_max_hp = 80;
    state->battle.enemy_mob_index = mob_index;
    state->battle.enemy_level = 1;  // Default level, can be set by caller
    state->battle.last_player_spell = SPELL_NONE;
    state->battle.last_enemy_spell = SPELL_NONE;
    state->battle.phase_timer = 60;  // ~2 seconds at 30fps
    state->battle.cast_attempts = 0;
    state->battle.player_attack_frame = -1;  // Not attacking
    state->battle.enemy_attack_frame = -1;   // Not attacking
    state->battle.last_skill_name[0] = '\0'; // Clear skill name
    state->battle.last_skill_id[0] = '\0';   // Clear skill ID

    // Clear any existing gesture
    game_clear_gesture(state);
}

void game_update_battle(GameState *state) {
    BattleState *b = &state->battle;

    // Decrement timer
    if (b->phase_timer > 0) {
        b->phase_timer--;
        return;  // Wait for timer
    }

    switch (b->phase) {
        case BATTLE_PHASE_INTRO:
            // Intro done, player's turn
            b->phase = BATTLE_PHASE_PLAYER_TURN;
            b->cast_attempts = 0;
            break;

        case BATTLE_PHASE_PLAYER_TURN:
            // Handled by gesture input in helloworld.c
            // When gesture confirmed, it will call recognize and move to PLAYER_CAST
            break;

        case BATTLE_PHASE_PLAYER_CAST:
            // Apply damage based on spell
            {
                int damage = 5;  // Basic
                switch (b->last_player_spell) {
                    case SPELL_M: damage = 20; break;
                    case SPELL_W: damage = 18; break;
                    case SPELL_S: damage = 15; break;
                    case SPELL_N: damage = 22; break;
                    case SPELL_O: damage = 12; break;  // Healing? Or attack
                    default: damage = 5; break;
                }
                b->enemy_hp -= damage;
                if (b->enemy_hp <= 0) {
                    b->enemy_hp = 0;
                    b->phase = BATTLE_PHASE_WIN;
                    b->phase_timer = 90;
                } else {
                    b->phase = BATTLE_PHASE_ENEMY_TURN;
                    b->phase_timer = 30;
                }
            }
            break;

        case BATTLE_PHASE_ENEMY_TURN:
            // Enemy picks random spell
            b->last_enemy_spell = (SpellType)(rand() % 5);
            b->phase = BATTLE_PHASE_ENEMY_CAST;
            b->phase_timer = 45;
            break;

        case BATTLE_PHASE_ENEMY_CAST:
            // Apply enemy damage
            {
                int damage = 8 + (rand() % 10);
                b->player_hp -= damage;
                if (b->player_hp <= 0) {
                    b->player_hp = 0;
                    b->phase = BATTLE_PHASE_LOSE;
                    b->phase_timer = 90;
                } else {
                    b->phase = BATTLE_PHASE_PLAYER_TURN;
                    b->cast_attempts = 0;
                    game_clear_gesture(state);
                }
            }
            break;

        case BATTLE_PHASE_WIN:
        case BATTLE_PHASE_LOSE:
            // Return to overworld (handled in helloworld.c after timer)
            break;
    }
}

// --- CALIBRATION SYSTEM ---

void game_start_calibration(GameState *state) {
    state->mode = MODE_CALIBRATION;
    state->menu_state = MENU_NONE;  // Exit menu

    CalibrationState *cal = &state->calibration;
    cal->selected_skill_index = 0;
    cal->skill_count = 0;
    cal->total_skills = 0;
    cal->calibrated_count = 0;
    cal->scroll_offset = 0;
    cal->is_drawing = false;
    cal->sample_sent = false;
    cal->feedback_timer = 0;
    cal->loading_skills = true;   // Will be set to false when skills are loaded
    cal->show_skill_list = true;  // Start with skill selection

    // Clear skill list
    memset(cal->skills, 0, sizeof(cal->skills));

    // Clear any existing gesture
    game_clear_gesture(state);

    printf("[CALIBRATION] Started calibration mode\n");
}

void game_calibration_add_skill(GameState *state, const char *skill_id, const char *name, int samples, bool calibrated) {
    CalibrationState *cal = &state->calibration;

    if (cal->skill_count >= MAX_CALIBRATION_SKILLS) return;

    SkillCalibrationInfo *skill = &cal->skills[cal->skill_count];
    strncpy(skill->skill_id, skill_id, SKILL_ID_MAX_LEN - 1);
    skill->skill_id[SKILL_ID_MAX_LEN - 1] = '\0';
    strncpy(skill->name, name, SKILL_NAME_MAX_LEN - 1);
    skill->name[SKILL_NAME_MAX_LEN - 1] = '\0';
    skill->samples_count = samples;
    skill->is_calibrated = calibrated;

    cal->skill_count++;
}

void game_calibration_select_skill(GameState *state) {
    // Called when user clicks on a skill to start calibrating it
    CalibrationState *cal = &state->calibration;

    if (cal->skill_count == 0 || cal->selected_skill_index >= cal->skill_count) return;

    cal->show_skill_list = false;  // Switch to drawing mode
    cal->is_drawing = false;
    game_clear_gesture(state);
}

void game_calibration_back(GameState *state) {
    CalibrationState *cal = &state->calibration;

    if (!cal->show_skill_list) {
        // Go back to skill list from drawing mode
        cal->show_skill_list = true;
        cal->is_drawing = false;
        game_clear_gesture(state);
    } else {
        // Exit calibration entirely
        state->mode = MODE_OVERWORLD;
        state->menu_state = MENU_NONE;
    }
}

void game_update_calibration(GameState *state) {
    CalibrationState *cal = &state->calibration;
    InputState *in = &state->input;

    // Update feedback timer
    if (cal->feedback_timer > 0) {
        cal->feedback_timer--;
        if (cal->feedback_timer == 0) {
            cal->sample_sent = false;
        }
    }

    // Handle navigation in skill list
    if (cal->show_skill_list && !cal->loading_skills) {
        if (in->knob_rotated_left && cal->selected_skill_index > 0) {
            cal->selected_skill_index--;
        }
        if (in->knob_rotated_right && cal->selected_skill_index < cal->skill_count - 1) {
            cal->selected_skill_index++;
        }
    }

    // Clear input flags (since game_update() won't run in calibration mode)
    in->knob_rotated_left = false;
    in->knob_rotated_right = false;
    in->just_pressed = false;
    in->just_released = false;
}

// --- PET SYSTEM ---

void game_set_active_pet(GameState *state, const char *id, const char *nickname,
                         const char *species, const char *element, const char *rarity,
                         int level, int xp, int xp_to_next, int hp, int max_hp, int current_hp,
                         int atk, int def, int spd) {
    ActivePetState *pet = &state->active_pet;

    strncpy(pet->id, id, sizeof(pet->id) - 1);
    strncpy(pet->nickname, nickname, sizeof(pet->nickname) - 1);
    strncpy(pet->species, species, sizeof(pet->species) - 1);
    strncpy(pet->element, element, sizeof(pet->element) - 1);
    strncpy(pet->rarity, rarity, sizeof(pet->rarity) - 1);

    pet->level = level;
    pet->xp = xp;
    pet->xp_to_next_level = xp_to_next > 0 ? xp_to_next : 100;  // Default 100 if not provided
    pet->hp = hp;
    pet->max_hp = max_hp;
    pet->current_hp = current_hp;
    pet->atk = atk;
    pet->def = def;
    pet->spd = spd;
    pet->skill_count = 0;
    pet->skill_tree_count = 0;
    pet->loaded = true;

    printf("[PET] Set active pet: %s (%s/%s) Lv%d XP:%d/%d HP:%d/%d ATK:%d DEF:%d SPD:%d\n",
           nickname, species, element, level, xp, xp_to_next, current_hp, max_hp, atk, def, spd);
}

void game_add_skill_tree_entry(GameState *state, const char *id, const char *name,
                               const char *type, int power, int unlock_level,
                               bool unlocked, bool has_gesture) {
    ActivePetState *pet = &state->active_pet;

    if (pet->skill_tree_count >= MAX_SKILL_TREE_ENTRIES) {
        printf("[PET] Skill tree full, cannot add %s\n", name);
        return;
    }

    SkillTreeEntry *entry = &pet->skill_tree[pet->skill_tree_count];
    strncpy(entry->id, id, sizeof(entry->id) - 1);
    strncpy(entry->name, name, sizeof(entry->name) - 1);
    strncpy(entry->type, type, sizeof(entry->type) - 1);
    entry->power = power;
    entry->unlock_level = unlock_level;
    entry->unlocked = unlocked;
    entry->has_gesture = has_gesture;

    pet->skill_tree_count++;
}

void game_add_rune_tree_entry(GameState *state, const char *id, const char *name,
                              const char *source, const char *element, int power,
                              int unlock_level, bool unlocked, bool calibrated) {
    ActivePetState *pet = &state->active_pet;

    if (pet->rune_tree_count >= MAX_RUNE_TREE_ENTRIES) {
        printf("[PET] Rune tree full, cannot add %s\n", name);
        return;
    }

    RuneTreeEntry *entry = &pet->rune_tree[pet->rune_tree_count];
    strncpy(entry->id, id, sizeof(entry->id) - 1);
    strncpy(entry->name, name, sizeof(entry->name) - 1);
    strncpy(entry->source, source, sizeof(entry->source) - 1);
    strncpy(entry->element, element, sizeof(entry->element) - 1);
    entry->power = power;
    entry->unlock_level = unlock_level;
    entry->unlocked = unlocked;
    entry->calibrated = calibrated;

    pet->rune_tree_count++;
    printf("[PET] Added rune %d: %s (%s) Pwr:%d Lv%d %s %s\n",
           pet->rune_tree_count, name, source, power, unlock_level,
           unlocked ? "unlocked" : "locked",
           calibrated ? "calibrated" : "");
}

void game_clear_rune_tree(GameState *state) {
    state->active_pet.rune_tree_count = 0;
}

void game_add_pet_skill(GameState *state, int skill_index, const char *id, const char *name,
                        const char *type, int power, int accuracy,
                        const char *effect, int effect_chance, bool has_gesture,
                        const int8_t *gesture_x, const int8_t *gesture_y, int gesture_count) {
    if (skill_index >= MAX_ACTIVE_SKILLS) return;

    ActivePetState *pet = &state->active_pet;
    PetSkillInfo *skill = &pet->skills[skill_index];

    strncpy(skill->id, id, sizeof(skill->id) - 1);
    strncpy(skill->name, name, sizeof(skill->name) - 1);
    strncpy(skill->type, type, sizeof(skill->type) - 1);
    skill->power = power;
    skill->accuracy = accuracy;
    strncpy(skill->effect, effect ? effect : "none", sizeof(skill->effect) - 1);
    skill->effect_chance = effect_chance;
    skill->has_gesture = has_gesture;

    // Copy gesture points
    skill->gesture_point_count = 0;
    if (gesture_x && gesture_y && gesture_count > 0) {
        int copy_count = (gesture_count < MAX_GESTURE_PREVIEW_POINTS) ? gesture_count : MAX_GESTURE_PREVIEW_POINTS;
        for (int i = 0; i < copy_count; i++) {
            skill->gesture_points_x[i] = gesture_x[i];
            skill->gesture_points_y[i] = gesture_y[i];
        }
        skill->gesture_point_count = copy_count;
    }

    if (skill_index >= pet->skill_count) {
        pet->skill_count = skill_index + 1;
    }

    printf("[PET] Added skill %d: %s (%s) Power:%d Acc:%d Effect:%s(%d%%) Gesture:%s (%d pts)\n",
           skill_index, name, type, power, accuracy, skill->effect, effect_chance,
           has_gesture ? "yes" : "no", skill->gesture_point_count);
}

void game_open_pet_stats(GameState *state) {
    state->menu_state = MENU_PET_STATS;
    state->pet_menu.selected_skill = 0;
    state->pet_menu.scroll_offset = 0;
    state->pet_menu.show_skill_detail = false;
    state->pet_menu.loading = false;

    printf("[PET] Opened pet stats menu - loaded=%d, name=%s, level=%d, skills=%d\n",
           state->active_pet.loaded, state->active_pet.nickname,
           state->active_pet.level, state->active_pet.skill_count);
}

void game_update_pet_stats_menu(GameState *state) {
    InputState *in = &state->input;
    PetStatsMenuState *menu = &state->pet_menu;
    int tree_count = state->active_pet.rune_tree_count;

    // Button click handling is in update_menu switch case
    // Here we handle the click action

    if (in->btn_clicked) {
        if (menu->selected_skill == tree_count) {
            // BACK option selected - go back to main menu
            state->menu_state = MENU_MAIN;
            state->menu_selection = OPT_MAIN_PET;
            state->menu_scroll_offset = 0;
        } else if (tree_count == 0) {
            // No runes, go back to main menu
            state->menu_state = MENU_MAIN;
            state->menu_selection = OPT_MAIN_PET;
            state->menu_scroll_offset = 0;
        }
        // Clicking on a rune does nothing (info is already visible)
    }

    // Long press to exit
    if (in->btn_menu_open_combo) {
        state->menu_state = MENU_MAIN;
        state->menu_selection = OPT_MAIN_PET;
    }
}

// --- TELEPORT SYSTEM ---

void game_open_teleport(GameState *state) {
    state->menu_state = MENU_TELEPORT;

    TeleportState *tp = &state->teleport;
    tp->selected_index = 0;
    tp->scroll_offset = 0;
    tp->map_count = 0;
    tp->loading = true;
    tp->teleport_requested = false;
    tp->teleport_x = 0;
    tp->teleport_y = 0;

    // Clear map list
    memset(tp->maps, 0, sizeof(tp->maps));

    printf("[TELEPORT] Opened teleport menu\n");
}

void game_add_discovered_map(GameState *state, int x, int y, const char *biome) {
    TeleportState *tp = &state->teleport;

    if (tp->map_count >= MAX_DISCOVERED_MAPS) return;

    DiscoveredMap *map = &tp->maps[tp->map_count];
    map->x = x;
    map->y = y;
    strncpy(map->biome, biome, sizeof(map->biome) - 1);
    map->biome[sizeof(map->biome) - 1] = '\0';

    tp->map_count++;
}

void game_teleport_select(GameState *state) {
    TeleportState *tp = &state->teleport;

    if (tp->loading || tp->map_count == 0) {
        // Nothing to select yet, go back
        game_teleport_back(state);
        return;
    }

    if (tp->selected_index >= tp->map_count) return;

    // Set teleport request
    tp->teleport_requested = true;
    tp->teleport_x = tp->maps[tp->selected_index].x;
    tp->teleport_y = tp->maps[tp->selected_index].y;

    printf("[TELEPORT] Teleporting to (%d, %d) - %s\n",
           tp->teleport_x, tp->teleport_y, tp->maps[tp->selected_index].biome);
}

void game_teleport_back(GameState *state) {
    state->menu_state = MENU_MAIN;
    state->menu_selection = OPT_MAIN_TELEPORT;
    state->teleport.teleport_requested = false;
}

// --- BUILDING INTERACTION SYSTEM ---

void game_set_buildings(GameState *state, bool is_home_base) {
    BuildingsState *bldg = &state->buildings;
    bldg->is_home_base = is_home_base;
    bldg->building_count = 0;
    bldg->current_building_index = -1;
    bldg->loading = false;
    memset(bldg->buildings, 0, sizeof(bldg->buildings));

    printf("[BUILDINGS] Set home base=%d\n", is_home_base);
}

void game_add_building(GameState *state, const char *name, int tile_x, int tile_y,
                       int width, int height, bool interactive,
                       int zone_x, int zone_y, int zone_w, int zone_h) {
    BuildingsState *bldg = &state->buildings;

    if (bldg->building_count >= MAX_BUILDINGS) return;

    BuildingInfo *b = &bldg->buildings[bldg->building_count];
    strncpy(b->name, name, sizeof(b->name) - 1);
    b->tile_x = tile_x;
    b->tile_y = tile_y;
    b->width = width;
    b->height = height;
    b->interactive = interactive;
    b->zone_x = zone_x;
    b->zone_y = zone_y;
    b->zone_w = zone_w;
    b->zone_h = zone_h;

    bldg->building_count++;

    printf("[BUILDINGS] Added building: %s at (%d,%d) interactive=%d zone=(%d,%d,%d,%d)\n",
           name, tile_x, tile_y, interactive, zone_x, zone_y, zone_w, zone_h);
}

int game_check_building_interaction(GameState *state) {
    BuildingsState *bldg = &state->buildings;

    if (!bldg->is_home_base) return -1;

    // Get player tile position
    int player_tile_x = state->player.x / TILE_SIZE;
    int player_tile_y = state->player.y / TILE_SIZE;

    for (int i = 0; i < bldg->building_count; i++) {
        BuildingInfo *b = &bldg->buildings[i];

        if (!b->interactive) continue;

        // Check if player is in interaction zone
        if (player_tile_x >= b->zone_x && player_tile_x < b->zone_x + b->zone_w &&
            player_tile_y >= b->zone_y && player_tile_y < b->zone_y + b->zone_h) {
            bldg->current_building_index = i;
            return i;
        }
    }

    bldg->current_building_index = -1;
    return -1;
}

void game_enter_building(GameState *state, int building_index) {
    BuildingsState *bldg = &state->buildings;

    if (building_index < 0 || building_index >= bldg->building_count) return;

    BuildingInfo *b = &bldg->buildings[building_index];

    printf("[BUILDINGS] Entering building: %s\n", b->name);

    // Open appropriate menu based on building name
    if (strcmp(b->name, "cloning_center") == 0) {
        game_open_cloning_center(state);
    } else if (strcmp(b->name, "pet_yard") == 0) {
        game_open_pet_yard(state);
    }
}

// --- DNA INVENTORY SYSTEM ---

void game_open_dna_inventory(GameState *state) {
    state->menu_state = MENU_DNA_INVENTORY;

    DNAInventoryState *inv = &state->dna_inventory;
    inv->selected_index = 0;
    inv->scroll_offset = 0;
    inv->sample_count = 0;
    inv->loading = true;
    memset(inv->samples, 0, sizeof(inv->samples));

    printf("[DNA] Opened DNA inventory\n");
}

void game_add_dna_sample(GameState *state, const char *id, const char *species,
                         const char *element, const char *rarity, int level) {
    DNAInventoryState *inv = &state->dna_inventory;

    if (inv->sample_count >= MAX_DNA_SAMPLES) return;

    DNASample *s = &inv->samples[inv->sample_count];
    strncpy(s->id, id, sizeof(s->id) - 1);
    strncpy(s->species, species, sizeof(s->species) - 1);
    strncpy(s->element, element, sizeof(s->element) - 1);
    strncpy(s->rarity, rarity, sizeof(s->rarity) - 1);
    s->level = level;

    inv->sample_count++;
}

void game_dna_inventory_back(GameState *state) {
    // If we're in cloning center, go back to cloning, otherwise go to main menu
    if (state->cloning.selection_step > 0) {
        state->menu_state = MENU_CLONING_CENTER;
    } else {
        state->menu_state = MENU_MAIN;
        state->menu_selection = OPT_MAIN_INVENTORY;
    }
}

// --- CLONING CENTER SYSTEM ---

void game_open_cloning_center(GameState *state) {
    state->menu_state = MENU_CLONING_CENTER;

    CloningState *clone = &state->cloning;
    clone->selected_sample_1 = -1;
    clone->selected_sample_2 = -1;
    clone->selection_step = 0;
    clone->cloning_in_progress = false;
    clone->cloning_complete = false;
    clone->show_result = false;
    memset(clone->result_species, 0, sizeof(clone->result_species));
    memset(clone->result_element, 0, sizeof(clone->result_element));
    clone->result_level = 0;

    // Also reset DNA inventory for selection
    state->dna_inventory.selected_index = 0;
    state->dna_inventory.scroll_offset = 0;
    state->dna_inventory.sample_count = 0;
    state->dna_inventory.loading = true;

    printf("[CLONING] Opened cloning center\n");
}

void game_cloning_select_sample(GameState *state) {
    CloningState *clone = &state->cloning;
    DNAInventoryState *inv = &state->dna_inventory;

    if (inv->sample_count == 0 || inv->selected_index >= inv->sample_count) {
        printf("[CLONING] No sample to select\n");
        return;
    }

    if (clone->selection_step == 0) {
        // Selecting first sample
        clone->selected_sample_1 = inv->selected_index;
        clone->selection_step = 1;
        printf("[CLONING] Selected first sample: %s (%s)\n",
               inv->samples[inv->selected_index].species,
               inv->samples[inv->selected_index].element);
    } else if (clone->selection_step == 1) {
        // Selecting second sample (must be different)
        if (inv->selected_index == clone->selected_sample_1) {
            printf("[CLONING] Cannot select the same sample twice\n");
            return;
        }
        clone->selected_sample_2 = inv->selected_index;
        clone->selection_step = 2;
        printf("[CLONING] Selected second sample: %s (%s)\n",
               inv->samples[inv->selected_index].species,
               inv->samples[inv->selected_index].element);
    }
}

void game_cloning_confirm(GameState *state) {
    CloningState *clone = &state->cloning;

    if (clone->selection_step != 2) {
        printf("[CLONING] Not ready to clone\n");
        return;
    }

    clone->cloning_in_progress = true;
    printf("[CLONING] Starting cloning process...\n");
}

void game_cloning_back(GameState *state) {
    CloningState *clone = &state->cloning;

    if (clone->show_result) {
        // Close result and exit cloning back to game
        clone->show_result = false;
        clone->cloning_complete = false;
        clone->selection_step = 0;
        clone->selected_sample_1 = -1;
        clone->selected_sample_2 = -1;
        state->menu_state = MENU_NONE;
        state->mode = MODE_OVERWORLD;
        printf("[CLONING] Result dismissed, returning to game\n");
    } else if (clone->selection_step == 2) {
        // Go back to selecting second sample
        clone->selected_sample_2 = -1;
        clone->selection_step = 1;
    } else if (clone->selection_step == 1) {
        // Go back to selecting first sample
        clone->selected_sample_1 = -1;
        clone->selection_step = 0;
    } else {
        // Exit cloning center
        state->menu_state = MENU_NONE;
        state->mode = MODE_OVERWORLD;
    }
}

void game_cloning_set_result(GameState *state, const char *species, const char *element, int level) {
    CloningState *clone = &state->cloning;

    strncpy(clone->result_species, species, sizeof(clone->result_species) - 1);
    strncpy(clone->result_element, element, sizeof(clone->result_element) - 1);
    clone->result_level = level;
    clone->cloning_in_progress = false;
    clone->cloning_complete = true;
    clone->show_result = true;

    printf("[CLONING] Cloning complete! New pet: %s/%s Lv%d\n", species, element, level);
}

// --- PET YARD SYSTEM ---

void game_open_pet_yard(GameState *state) {
    state->menu_state = MENU_PET_YARD;

    PetYardState *yard = &state->pet_yard;
    yard->pet_count = 0;
    yard->selected_index = 0;
    yard->scroll_offset = 0;
    yard->loading = true;
    yard->switch_requested = false;
    memset(yard->switch_pet_id, 0, sizeof(yard->switch_pet_id));
    memset(yard->pets, 0, sizeof(yard->pets));

    printf("[PET YARD] Opened pet yard\n");
}

void game_add_pet_entry(GameState *state, const char *id, const char *nickname,
                        const char *species, const char *element, int level,
                        int current_hp, int max_hp) {
    PetYardState *yard = &state->pet_yard;

    if (yard->pet_count >= MAX_PETS) return;

    PetListEntry *p = &yard->pets[yard->pet_count];
    strncpy(p->id, id, sizeof(p->id) - 1);
    strncpy(p->nickname, nickname, sizeof(p->nickname) - 1);
    strncpy(p->species, species, sizeof(p->species) - 1);
    strncpy(p->element, element, sizeof(p->element) - 1);
    p->level = level;
    p->current_hp = current_hp;
    p->max_hp = max_hp;

    yard->pet_count++;
}

void game_pet_yard_select(GameState *state) {
    PetYardState *yard = &state->pet_yard;

    if (yard->pet_count == 0 || yard->selected_index >= yard->pet_count) {
        printf("[PET YARD] No pet to select\n");
        return;
    }

    // Mark as switch requested
    yard->switch_requested = true;
    strncpy(yard->switch_pet_id, yard->pets[yard->selected_index].id, sizeof(yard->switch_pet_id) - 1);

    printf("[PET YARD] Requested switch to: %s\n", yard->pets[yard->selected_index].nickname);
}

void game_pet_yard_back(GameState *state) {
    state->menu_state = MENU_NONE;
    state->mode = MODE_OVERWORLD;
    state->pet_yard.switch_requested = false;
}

// =============================================================================
// RUNE BATTLE SYSTEM
// =============================================================================

void game_start_rune_battle(GameState *state, int mob_index) {
    printf("[RUNE BATTLE] Starting battle against mob %d\n", mob_index);

    RuneBattleState *rb = &state->rune_battle;
    Mob *enemy = &current_mobs[mob_index];  // Use global from generated_assets.h

    // Initialize battle state
    rb->phase = RUNE_PHASE_INTRO;
    rb->player_hp = state->active_pet.current_hp;
    rb->player_max_hp = state->active_pet.max_hp;

    // Calculate enemy HP based on level
    int enemy_level = state->active_pet.level + (rand() % 5 - 2);
    if (enemy_level < 1) enemy_level = 1;
    rb->enemy_level = enemy_level;
    rb->enemy_hp = 80 + (enemy_level * 5);
    rb->enemy_max_hp = rb->enemy_hp;
    rb->enemy_mob_index = mob_index;

    strncpy(rb->enemy_species, enemy->species, sizeof(rb->enemy_species) - 1);
    strncpy(rb->enemy_element, enemy->element, sizeof(rb->enemy_element) - 1);

    // Clear chains
    rb->player_chain_count = 0;
    rb->enemy_chain_count = 0;
    memset(rb->player_rune_used, 0, sizeof(rb->player_rune_used));

    // Timer setup
    rb->chain_timer = RUNE_TIMER_FRAMES;
    rb->chain_timer_max = RUNE_TIMER_FRAMES;
    rb->momentum = 1.0f;

    // Animation state
    rb->execute_index = 0;
    rb->execute_timer = 0;
    rb->total_damage_dealt = 0;

    // Clear available runes (will be populated by server call)
    rb->available_count = 0;

    // Recognition feedback
    rb->show_recognition_feedback = false;
    rb->feedback_timer = 0;
    rb->last_recognized_id[0] = '\0';
    rb->last_recognized_name[0] = '\0';
    rb->last_accuracy = 0;

    state->mode = MODE_RUNE_BATTLE;

    printf("[RUNE BATTLE] Enemy: %s (%s) Lv%d, HP %d\n",
           rb->enemy_species, rb->enemy_element, rb->enemy_level, rb->enemy_hp);
}

void game_update_rune_battle(GameState *state) {
    RuneBattleState *rb = &state->rune_battle;

    // Update feedback timer
    if (rb->feedback_timer > 0) {
        rb->feedback_timer--;
        if (rb->feedback_timer == 0) {
            rb->show_recognition_feedback = false;
        }
    }

    switch (rb->phase) {
        case RUNE_PHASE_INTRO:
            // Intro phase - show "Battle Start!" briefly
            // Transition handled by main loop after delay
            break;

        case RUNE_PHASE_PLAYER_CHAIN:
            // Timer countdown
            if (rb->chain_timer > 0) {
                rb->chain_timer--;
            }

            // Timer expired - auto execute chain
            if (rb->chain_timer == 0) {
                printf("[RUNE BATTLE] Timer expired, executing chain with %d runes\n", rb->player_chain_count);
                rb->phase = RUNE_PHASE_PLAYER_EXECUTE;
                rb->execute_index = 0;
                rb->execute_timer = 30; // Animation time per rune

                // Calculate total damage
                int total = 0;
                for (int i = 0; i < rb->player_chain_count; i++) {
                    total += rb->player_chain[i].power;
                }
                total = (int)(total * rb->momentum);
                rb->total_damage_dealt = total;
                rb->enemy_hp -= total;
                if (rb->enemy_hp < 0) rb->enemy_hp = 0;
                printf("[RUNE BATTLE] Dealt %d damage! Enemy HP: %d/%d\n", total, rb->enemy_hp, rb->enemy_max_hp);
            }

            // Update momentum display based on chain length
            rb->momentum = 1.0f + (rb->player_chain_count * 0.5f);
            break;

        case RUNE_PHASE_PLAYER_EXECUTE:
            // Animate each rune in chain
            if (rb->execute_timer > 0) {
                rb->execute_timer--;
            } else {
                rb->execute_index++;
                if (rb->execute_index < rb->player_chain_count) {
                    rb->execute_timer = 20; // Animation for next rune
                } else {
                    // Done executing, go to enemy turn
                    if (rb->enemy_hp <= 0) {
                        rb->phase = RUNE_PHASE_WIN;
                    } else {
                        rb->phase = RUNE_PHASE_ENEMY_CHAIN;
                    }
                }
            }
            break;

        case RUNE_PHASE_ENEMY_CHAIN:
            // Enemy chain is built by server, this is quick display
            // Main loop will fetch enemy chain and transition
            break;

        case RUNE_PHASE_ENEMY_EXECUTE:
            // Animate enemy's chain
            if (rb->execute_timer > 0) {
                rb->execute_timer--;
            } else {
                rb->execute_index++;
                if (rb->execute_index < rb->enemy_chain_count) {
                    rb->execute_timer = 20;
                } else {
                    // Done, check for death or next turn
                    if (rb->player_hp <= 0) {
                        rb->phase = RUNE_PHASE_LOSE;
                    } else {
                        // Reset for next player turn
                        rb->player_chain_count = 0;
                        memset(rb->player_rune_used, 0, sizeof(rb->player_rune_used));
                        rb->chain_timer = rb->chain_timer_max;
                        rb->momentum = 1.0f;
                        rb->phase = RUNE_PHASE_PLAYER_CHAIN;
                    }
                }
            }
            break;

        case RUNE_PHASE_WIN:
        case RUNE_PHASE_LOSE:
            // End states - handled by main loop
            break;
    }
}

void game_rune_battle_add_available(GameState *state, const char *rune_id, const char *name) {
    RuneBattleState *rb = &state->rune_battle;

    if (rb->available_count >= 20) return;

    strncpy(rb->available_runes[rb->available_count], rune_id, RUNE_ID_LEN - 1);
    strncpy(rb->available_names[rb->available_count], name, RUNE_NAME_LEN - 1);
    rb->available_count++;

    printf("[RUNE BATTLE] Added available rune: %s (%s)\n", rune_id, name);
}

void game_rune_battle_add_to_chain(GameState *state, const char *rune_id, const char *name,
                                    int power, int accuracy, bool chain_bonus) {
    RuneBattleState *rb = &state->rune_battle;

    if (rb->player_chain_count >= MAX_RUNE_CHAIN) {
        printf("[RUNE BATTLE] Chain full!\n");
        return;
    }

    // Check if already used
    for (int i = 0; i < rb->player_chain_count; i++) {
        if (strcmp(rb->player_chain[i].id, rune_id) == 0) {
            printf("[RUNE BATTLE] Rune %s already in chain\n", rune_id);
            return;
        }
    }

    ChainedRune *rune = &rb->player_chain[rb->player_chain_count];
    strncpy(rune->id, rune_id, RUNE_ID_LEN - 1);
    strncpy(rune->name, name, RUNE_NAME_LEN - 1);
    rune->power = power;
    rune->accuracy = accuracy;
    rune->chain_bonus = chain_bonus;

    rb->player_chain_count++;
    rb->momentum = 1.0f + (rb->player_chain_count * 0.5f);

    // Reset timer on successful rune add
    rb->chain_timer = rb->chain_timer_max;

    printf("[RUNE BATTLE] Added to chain: %s (power %d, accuracy %d%%, momentum %.1fx)\n",
           name, power, accuracy, rb->momentum);
}

void game_rune_battle_clear_chain(GameState *state) {
    RuneBattleState *rb = &state->rune_battle;
    rb->player_chain_count = 0;
    rb->momentum = 1.0f;
    memset(rb->player_rune_used, 0, sizeof(rb->player_rune_used));
    printf("[RUNE BATTLE] Chain cleared\n");
}

void game_rune_battle_set_enemy_chain(GameState *state, int count) {
    RuneBattleState *rb = &state->rune_battle;
    rb->enemy_chain_count = 0; // Will be populated by add_enemy_rune calls
    printf("[RUNE BATTLE] Enemy chain will have %d runes\n", count);
}

void game_rune_battle_add_enemy_rune(GameState *state, int index, const char *rune_id,
                                      const char *name, int power) {
    RuneBattleState *rb = &state->rune_battle;

    if (index >= MAX_RUNE_CHAIN) return;

    ChainedRune *rune = &rb->enemy_chain[index];
    strncpy(rune->id, rune_id, RUNE_ID_LEN - 1);
    strncpy(rune->name, name, RUNE_NAME_LEN - 1);
    rune->power = power;
    rune->accuracy = 100; // Enemy always has perfect accuracy
    rune->chain_bonus = false; // TODO: Calculate from chain

    if (index >= rb->enemy_chain_count) {
        rb->enemy_chain_count = index + 1;
    }

    printf("[RUNE BATTLE] Enemy rune %d: %s (power %d)\n", index, name, power);
}