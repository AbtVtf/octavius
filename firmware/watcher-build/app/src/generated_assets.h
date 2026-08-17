#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// Exit point structure
typedef struct {
    uint8_t edge;      // 0=top, 1=bottom, 2=left, 3=right
    uint16_t tile_x;
    uint16_t tile_y;
    uint16_t pixel_x;
    uint16_t pixel_y;
} ExitPoint;

#define MAX_EXIT_POINTS 16
#define MAX_MOBS 10
#define MOB_SPRITE_SIZE (104 * 88 * 2)  // 104x88 @ 16bpp = 18304 bytes

// Spell animation constants (128x128 for better visibility)
#define SPELL_FRAME_W 128
#define SPELL_FRAME_H 128
#define SPELL_FRAME_SIZE (SPELL_FRAME_W * SPELL_FRAME_H * 2)  // 128x128 @ 16bpp = 32768 bytes
#define SPELL_FRAME_COUNT 4
#define SPELL_TYPE_COUNT 6  // M, W, S, N, O, BASIC

// Mob structure
typedef struct {
    char species[32];      // e.g., "fox"
    char element[32];      // e.g., "fire"
    int16_t x;             // Pixel position on map
    int16_t y;
    uint16_t *sprite_data; // Pointer to sprite in PSRAM
} Mob;

// Exit points data
extern ExitPoint exit_points[MAX_EXIT_POINTS];
extern uint8_t exit_point_count;
extern int spawn_x;  // Where to spawn player on new map
extern int spawn_y;

// Mob data
extern Mob current_mobs[MAX_MOBS];
extern uint8_t current_mob_count;

// Spell animation frames [spell_type][frame_index]
extern uint16_t *spell_frames[SPELL_TYPE_COUNT][SPELL_FRAME_COUNT];
extern bool spells_loaded;

// Pointers to Asset Data (Loaded in RAM/PSRAM)
extern uint16_t *fox_w1_data;
extern uint16_t *fox_w1m_data;
extern uint16_t *fox_w2_data;
extern uint16_t *fox_w2m_data;
extern uint16_t *fox_w3_data;
extern uint16_t *fox_w3m_data;
extern uint16_t *fox_w4_data;
extern uint16_t *fox_w4m_data;
extern uint16_t *fox_w5_data;
extern uint16_t *fox_w5m_data;
extern uint16_t *fox_w6_data;
extern uint16_t *fox_w6m_data;
extern uint16_t *fox_w7_data;
extern uint16_t *fox_w7m_data;
extern uint16_t *fox_w8_data;
extern uint16_t *fox_w8m_data;

// Sizes
#define SIZE_MAP    169744 // 412 * 412
#define SIZE_FULL_MAP (50*32 * 50*32 * 2) // 1600x1600 @ 16bpp = 5,120,000 bytes
#define SIZE_COLLISION (50*50) // 2500 bytes
#define SIZE_SPRITE 9152   // 104 * 88 pixels
#define SIZE_TILE   6144   // Max tile size (e.g. 64 * 96 for trees)

// Pointers to Asset Data (Loaded in RAM/PSRAM)
extern uint16_t *current_map_image_data; // The big map
extern uint8_t *current_collision_data;  // The collision grid

// Function to load assets
void load_assets_from_sd(void);
void fetch_global_map_data(void);
void fetch_mobs(void);
void fetch_spell_sprites(void);
void send_gesture_data(int16_t *points_x, int16_t *points_y, int point_count);
int send_calibration_gesture(int16_t *points_x, int16_t *points_y, int point_count, int spell_type);
int send_gesture_record(int16_t *points_x, int16_t *points_y, int point_count);
int recognize_gesture_server(int16_t *points_x, int16_t *points_y, int point_count);

// Skill-based calibration API
// Callback type for receiving uncalibrated skill info
typedef void (*uncalibrated_skill_callback_t)(const char *skill_id, const char *name);

// Fetch uncalibrated skills from server
// Returns: number of uncalibrated skills, or -1 on error
// Also sets total_skills and calibrated_count if pointers are not NULL
int fetch_uncalibrated_skills(uncalibrated_skill_callback_t callback, int *total_skills, int *calibrated_count);

// Send calibration sample for a specific skill
// Returns 0 on success, -1 on error
int send_skill_calibration(const char *skill_id, int16_t *points_x, int16_t *points_y, int point_count);

// Recognize gesture against active skills
// active_skills is a comma-separated list of skill IDs
// Returns skill index matched (0+) or -1 if no match
// Also fills out_accuracy (0-100) and out_damage_mult (0.5-1.5)
int recognize_skill_gesture(int16_t *points_x, int16_t *points_y, int point_count,
                            const char *active_skills[], int active_count,
                            char *out_skill_id, int *out_accuracy, float *out_damage_mult);

// Pet system API
// Callback for receiving pet basic info
typedef void (*pet_info_callback_t)(
    const char *id, const char *nickname, const char *species,
    const char *element, const char *rarity,
    int level, int xp, int xp_to_next, int hp, int max_hp, int current_hp,
    int atk, int def, int spd);

// Callback for receiving pet skill info
// gesture_points_x/y are normalized 0-100 arrays, gesture_point_count is the number of points
typedef void (*pet_skill_callback_t)(
    int skill_index, const char *id, const char *name, const char *type,
    int power, int accuracy, const char *effect, int effect_chance, bool has_gesture,
    const int8_t *gesture_points_x, const int8_t *gesture_points_y, int gesture_point_count);

// Callback for receiving skill tree entry
typedef void (*skill_tree_callback_t)(
    const char *id, const char *name, const char *type,
    int power, int unlock_level, bool unlocked, bool has_gesture);

// Fetch active pet from server and call callbacks with data
// Returns 0 on success, -1 on error
int fetch_active_pet(pet_info_callback_t pet_callback, pet_skill_callback_t skill_callback,
                     skill_tree_callback_t tree_callback);

// Pet sprite loading
// Downloads 8 walking sprites (4 right-facing + 4 left-facing)
// Filenames: pet_{species}_{element}_w{0-3}.bin and pet_{species}_{element}_w{0-3}m.bin
// Returns 0 on success, -1 on error
int fetch_pet_sprites(const char *species, const char *element);

// Pet sprites loaded via fetch_pet_sprites (replaces hardcoded fox)
extern uint16_t *pet_walk_sprites[8];   // [0-3] = right, [4-7] = left (mirrored)
extern uint16_t *pet_attack_sprites[8]; // [0-3] = right, [4-7] = left (mirrored)
extern bool pet_sprites_loaded;

// Skill sprite loading
// Downloads 4 animation frames per skill for each of the pet's active skills
// Filenames: skill_{skill_id}_{0-3}.bin
#define MAX_SKILL_SPRITES 5  // Match MAX_ACTIVE_SKILLS
#define SKILL_SPRITE_FRAMES 4
extern uint16_t *skill_sprites[MAX_SKILL_SPRITES][SKILL_SPRITE_FRAMES];
extern char skill_sprite_ids[MAX_SKILL_SPRITES][32];  // Track which skill each slot holds
extern int skill_sprite_count;

// Load skill sprites for the given skill IDs
// Returns number of skills successfully loaded
int fetch_skill_sprites(const char *skill_ids[], int skill_count);

// Get skill sprite frame for a skill ID (returns NULL if not loaded)
uint16_t *get_skill_sprite(const char *skill_id, int frame);

// Free all loaded skill sprites
void free_skill_sprites(void);

// Teleport system - discovered maps
// Callback for receiving discovered map info
typedef void (*discovered_map_callback_t)(int x, int y, const char *biome);

// Fetch discovered maps from server and call callback for each
// Returns number of maps found, -1 on error
int fetch_discovered_maps(discovered_map_callback_t callback);

// Building interaction system
// Callback for receiving building info
typedef void (*building_callback_t)(const char *name, int tile_x, int tile_y,
                                    int width, int height, bool interactive,
                                    int zone_x, int zone_y, int zone_w, int zone_h);

// Fetch buildings for chunk at (x, y)
// Returns: 1 if home base, 0 if not home base, -1 on error
int fetch_buildings(int x, int y, building_callback_t callback);

// DNA Inventory system
// Callback for receiving DNA sample info
typedef void (*dna_sample_callback_t)(const char *id, const char *species,
                                      const char *element, const char *rarity, int level);

// Fetch DNA inventory from server
// Returns number of samples, -1 on error
int fetch_dna_inventory(dna_sample_callback_t callback);

// Pet list system
// Callback for receiving pet list entry
typedef void (*pet_list_callback_t)(const char *id, const char *nickname,
                                    const char *species, const char *element,
                                    int level, int current_hp, int max_hp);

// Fetch all pets from server
// Returns number of pets, -1 on error
int fetch_pet_list(pet_list_callback_t callback);

// Switch active pet
// Returns 0 on success, -1 on error
int switch_active_pet(const char *pet_id);

// Clone pets
// Returns 0 on success and fills out_species/out_element/out_level, -1 on error
int clone_pets(const char *dna_id_1, const char *dna_id_2,
               char *out_species, char *out_element, int *out_level);

// Battle end - report victory/loss to server and collect rewards
// Returns 0 on success, -1 on error
// If player_won, fills out_dna_species and out_dna_element with collected DNA info
int send_battle_end(bool player_won, const char *enemy_species, const char *enemy_element,
                    int enemy_level, char *out_dna_species, char *out_dna_element, int *out_xp_gained);

// Rune calibration API
// Callback for receiving rune info from /runes/list
// display_name is formatted like "Fox - Lv1" or "Fire - Lv3"
typedef void (*rune_callback_t)(const char *rune_id, const char *display_name, int samples, bool calibrated);

// Fetch all runes from server for calibration
// Returns: number of runes, or -1 on error
// Also sets total_runes and calibrated_count if pointers are not NULL
int fetch_runes_for_calibration(rune_callback_t callback, int *total_runes, int *calibrated_count);

// Send calibration sample for a specific rune
// Returns 0 on success, -1 on error
int send_rune_calibration(const char *rune_id, int16_t *points_x, int16_t *points_y, int point_count);

// =============================================================================
// RUNE BATTLE API
// =============================================================================

// Callback for receiving available runes for a pet in battle
typedef void (*available_rune_callback_t)(const char *rune_id, const char *name);

// Fetch available runes for battle based on pet species/element/level
// Returns number of available runes, -1 on error
int fetch_available_runes(const char *species, const char *element, int level,
                          available_rune_callback_t callback);

// Recognize a gesture against available runes
// Returns 0 on success (fills out params), -1 on error/no match
int recognize_rune_gesture(int16_t *points_x, int16_t *points_y, int point_count,
                           const char *available_runes[], int rune_count,
                           char *out_rune_id, char *out_rune_name,
                           int *out_accuracy, int *out_power, bool *out_chain_bonus);

// Execute player's rune chain on server
// Returns total damage dealt, -1 on error
// Also fills enemy HP after damage
int execute_rune_chain(const char *rune_ids[], int chain_count,
                       const char *enemy_species, const char *enemy_element, int enemy_level,
                       int *out_enemy_hp_after);

// Get enemy's rune chain from server AI
// Callback is called for each rune in enemy's chain
typedef void (*enemy_chain_callback_t)(int index, const char *rune_id, const char *name, int power);
int fetch_enemy_rune_chain(const char *enemy_species, const char *enemy_element, int enemy_level,
                           int player_hp, int enemy_hp,
                           enemy_chain_callback_t callback, int *out_chain_count);

// Callback for receiving rune tree entries (for pet stats menu)
typedef void (*rune_tree_callback_t)(const char *rune_id, const char *name, const char *source,
                                     const char *element, int power, int unlock_level,
                                     bool unlocked, bool calibrated);

// Fetch full rune tree for a pet (including locked runes)
// Returns number of runes in tree, -1 on error
int fetch_pet_rune_tree(const char *species, const char *element, int level,
                        rune_tree_callback_t callback);

// Execute enemy's chain and get damage to player
// Returns damage dealt to player, -1 on error
int execute_enemy_rune_chain(const char *enemy_species, const char *enemy_element, int enemy_level,
                             const char *player_species, const char *player_element,
                             int *out_player_hp_after);
