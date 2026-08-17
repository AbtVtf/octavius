#ifndef GAME_LOGIC_H
#define GAME_LOGIC_H

#include <stdint.h>
#include <stdbool.h>

// Game Constants
#define SCR_W 412
#define SCR_H 412
#define FOX_W 104
#define FOX_H 88

// Map Constants
#define TILE_SIZE 32
#define MAP_W_TILES 50
#define MAP_H_TILES 50
#define MAP_PX_W (MAP_W_TILES * TILE_SIZE)
#define MAP_PX_H (MAP_H_TILES * TILE_SIZE)

// Physics Constants
#define PLAYER_SPEED 8
#define GRAVITY 2
#define JUMP_FORCE -22
#define FLOOR_Y 130 

// Tile Types
typedef enum {
    TILE_EMPTY = 0,
    TILE_GRASS,
    TILE_TREE,
    TILE_ROCK,
    TILE_WATER,
    TILE_OAK_TREE,
    TILE_PINE_TREE,
    TILE_BIRCH_TREE,
    TILE_MOSSY_ROCK,
    TILE_SMALL_ROCK,
    TILE_BUSH_BERRIES,
    TILE_STUMP,
    TILE_TALL_GRASS,
    TILE_FLOWERS,
    TILE_DIRT_PATCH,
    TILE_COUNT // Total number of tile types
} TileType;

// Tile Map
typedef struct {
    TileType tiles[MAP_H_TILES][MAP_W_TILES];
} TileMap;

// Menu States
typedef enum {
    MENU_NONE = 0,
    MENU_MAIN,
    MENU_INVENTORY,
    MENU_COLLECTION,
    MENU_SETTINGS_MAIN,
    MENU_SETTINGS_BRIGHTNESS,
    MENU_SETTINGS_SOUND,
    MENU_SLEEP, // Special state to turn off screen
    MENU_PET_STATS, // Pet stats and skills view
    MENU_TELEPORT,  // Teleport to discovered maps
    MENU_CLONING_CENTER,  // Cloning center UI
    MENU_PET_YARD,  // Pet yard for switching pets
    MENU_DNA_INVENTORY  // DNA sample inventory view
} MenuState;

// Main Menu Options
typedef enum {
    OPT_MAIN_PET = 0,      // View pet stats/skills
    OPT_MAIN_SWITCH_PET,   // Switch active pet (pet yard)
    OPT_MAIN_CLONING,      // Cloning center
    OPT_MAIN_INVENTORY,
    OPT_MAIN_COLLECTION,
    OPT_MAIN_TELEPORT,     // Teleport to discovered maps
    OPT_MAIN_SLEEP,
    OPT_MAIN_SETTINGS,
    OPT_MAIN_BACK,
    OPT_MAIN_COUNT // Total number of main options
} MainMenuOption;

// Settings Menu Options
typedef enum {
    OPT_SETTINGS_BRIGHTNESS = 0,
    OPT_SETTINGS_SOUND,
    OPT_SETTINGS_CALIBRATE,  // Gesture calibration
    OPT_SETTINGS_BACK,
    OPT_SETTINGS_COUNT
} SettingsMenuOption;

// Game Modes
typedef enum {
    MODE_PLATFORMER,
    MODE_OVERWORLD,
    MODE_BATTLE,          // Legacy battle system
    MODE_RUNE_BATTLE,     // New rune chain battle system
    MODE_CALIBRATION      // Gesture calibration mode
} GameMode;

// Maximum skills that can be loaded for calibration
#define MAX_CALIBRATION_SKILLS 100  // All 94 skills + buffer
#define SKILL_ID_MAX_LEN 32
#define SKILL_NAME_MAX_LEN 48
#define SAMPLES_NEEDED 10

// Pet data structures
#define MAX_ACTIVE_SKILLS 5
#define MAX_GESTURE_STROKES 10
#define MAX_GESTURE_POINTS_PER_STROKE 50
#define MAX_GESTURE_PREVIEW_POINTS 32  // Simplified gesture for display

// Skill info for pet display
typedef struct {
    char id[SKILL_ID_MAX_LEN];
    char name[SKILL_NAME_MAX_LEN];
    char type[16];           // fire, water, etc.
    int power;
    int accuracy;
    char effect[32];
    int effect_chance;
    bool has_gesture;
    // Gesture preview points (normalized 0-100 range for display)
    int gesture_point_count;
    int8_t gesture_points_x[MAX_GESTURE_PREVIEW_POINTS];
    int8_t gesture_points_y[MAX_GESTURE_PREVIEW_POINTS];
} PetSkillInfo;

// Skill tree entry (for showing all skills) - LEGACY
#define MAX_SKILL_TREE_ENTRIES 20

typedef struct {
    char id[SKILL_ID_MAX_LEN];
    char name[SKILL_NAME_MAX_LEN];
    char type[16];           // fire, water, etc.
    int power;
    int unlock_level;        // Level required to unlock (0 = already unlocked)
    bool unlocked;           // True if pet has this skill
    bool has_gesture;        // True if gesture is calibrated
} SkillTreeEntry;

// Rune tree entry (for showing all runes)
#define MAX_RUNE_TREE_ENTRIES 30  // 5 species + 5 element + possibly more for hybrids
#define RUNE_ID_LEN 32
#define RUNE_NAME_LEN 32

typedef struct {
    char id[RUNE_ID_LEN];
    char name[RUNE_NAME_LEN];
    char source[16];         // "species" or "element" or parent species name
    char element[16];        // fire, water, etc.
    int power;
    int unlock_level;        // Level required to unlock (1, 3, 5, 7, 10)
    bool unlocked;           // True if pet level >= unlock_level
    bool calibrated;         // True if gesture is calibrated
} RuneTreeEntry;

// Active pet state (loaded from server)
typedef struct {
    char id[64];
    char nickname[32];
    char species[32];
    char element[32];
    char rarity[16];
    int level;
    int xp;
    int xp_to_next_level;    // XP needed to reach next level
    int hp;
    int max_hp;
    int current_hp;
    int atk;
    int def;
    int spd;
    int skill_count;
    PetSkillInfo skills[MAX_ACTIVE_SKILLS];
    // Full skill tree (LEGACY)
    int skill_tree_count;
    SkillTreeEntry skill_tree[MAX_SKILL_TREE_ENTRIES];
    // Rune tree (NEW)
    int rune_tree_count;
    RuneTreeEntry rune_tree[MAX_RUNE_TREE_ENTRIES];
    bool loaded;              // True if pet data has been fetched
} ActivePetState;

// Pet stats menu state
typedef struct {
    int selected_skill;       // Which skill is highlighted (0 to skill_count, where skill_count = BACK)
    int scroll_offset;        // Scroll offset for skill list
    bool show_skill_detail;   // True to show detailed skill info
    bool loading;             // True while fetching data
} PetStatsMenuState;

// Skill info for calibration UI
typedef struct {
    char skill_id[SKILL_ID_MAX_LEN];
    char name[SKILL_NAME_MAX_LEN];
    int samples_count;       // How many samples recorded
    bool is_calibrated;      // Has a valid template
} SkillCalibrationInfo;

// Calibration state
typedef struct {
    int selected_skill_index;       // Index into loaded skills array
    int skill_count;                // Number of skills loaded
    int total_skills;               // Total skills in database
    int calibrated_count;           // How many already calibrated
    int scroll_offset;              // For scrolling the list
    SkillCalibrationInfo skills[MAX_CALIBRATION_SKILLS];
    bool is_drawing;                // True if in drawing mode
    bool sample_sent;               // True if just sent a sample (show feedback)
    int feedback_timer;             // Timer for feedback display
    bool loading_skills;            // True while fetching skill list from server
    bool show_skill_list;           // True to show skill selection, false for drawing
} CalibrationState;

// Teleport system
#define MAX_DISCOVERED_MAPS 50

typedef struct {
    int x;
    int y;
    char biome[16];
} DiscoveredMap;

typedef struct {
    int selected_index;             // Currently selected map in list
    int map_count;                  // Number of discovered maps
    DiscoveredMap maps[MAX_DISCOVERED_MAPS];
    bool loading;                   // True while fetching from server
    int scroll_offset;              // For scrolling the list view
    bool teleport_requested;        // True when user confirms teleport
    int teleport_x;                 // Target world coordinates
    int teleport_y;
} TeleportState;

// DNA Inventory system
#define MAX_DNA_SAMPLES 50

typedef struct {
    char id[64];
    char species[32];
    char element[32];
    char rarity[16];
    int level;
} DNASample;

typedef struct {
    int sample_count;
    DNASample samples[MAX_DNA_SAMPLES];
    int selected_index;
    int scroll_offset;
    bool loading;
} DNAInventoryState;

// Building interaction system
#define MAX_BUILDINGS 10

typedef struct {
    char name[32];
    int tile_x;
    int tile_y;
    int width;
    int height;
    bool interactive;
    // Interaction zone (where player can trigger)
    int zone_x;
    int zone_y;
    int zone_w;
    int zone_h;
} BuildingInfo;

typedef struct {
    bool is_home_base;
    int building_count;
    BuildingInfo buildings[MAX_BUILDINGS];
    int current_building_index;   // -1 if not near any building
    bool loading;
} BuildingsState;

// Cloning Center state
typedef struct {
    int selected_sample_1;        // First DNA sample index (-1 if not selected)
    int selected_sample_2;        // Second DNA sample index (-1 if not selected)
    int selection_step;           // 0 = selecting first, 1 = selecting second, 2 = confirm
    bool cloning_in_progress;
    bool cloning_complete;
    char result_species[32];
    char result_element[32];
    int result_level;
    bool show_result;
} CloningState;

// Pet Yard state (for pet switching)
#define MAX_PETS 20

typedef struct {
    char id[64];
    char nickname[32];
    char species[32];
    char element[32];
    int level;
    int current_hp;
    int max_hp;
} PetListEntry;

typedef struct {
    int pet_count;
    PetListEntry pets[MAX_PETS];
    int selected_index;
    int scroll_offset;
    bool loading;
    bool switch_requested;
    char switch_pet_id[64];
} PetYardState;

// Battle phases
typedef enum {
    BATTLE_PHASE_INTRO,        // Show "Battle Start!"
    BATTLE_PHASE_PLAYER_TURN,  // Player draws gesture
    BATTLE_PHASE_PLAYER_CAST,  // Show player's spell effect
    BATTLE_PHASE_ENEMY_TURN,   // Enemy casts (auto)
    BATTLE_PHASE_ENEMY_CAST,   // Show enemy's spell effect
    BATTLE_PHASE_WIN,          // Player won
    BATTLE_PHASE_LOSE          // Player lost
} BattlePhase;

// Spell types (matched by gesture)
typedef enum {
    SPELL_NONE = -1,
    SPELL_M = 0,   // M gesture
    SPELL_W = 1,   // W gesture
    SPELL_S = 2,   // S gesture
    SPELL_N = 3,   // N gesture
    SPELL_O = 4,   // O gesture (circle)
    SPELL_BASIC = 5 // Failed gesture = weak attack
} SpellType;

// Battle state (legacy - keeping for transition)
typedef struct {
    BattlePhase phase;
    int player_hp;
    int player_max_hp;
    int enemy_hp;
    int enemy_max_hp;
    int enemy_mob_index;      // Index into current_mobs array
    int enemy_level;          // Enemy level for XP calculation
    SpellType last_player_spell;
    SpellType last_enemy_spell;
    int phase_timer;          // For animations/delays
    int cast_attempts;        // 3 attempts per turn
    char enemy_species[32];
    char enemy_element[32];
    int player_attack_frame;  // Animation frame for player attack (0-3, -1 = not attacking)
    int enemy_attack_frame;   // Animation frame for enemy attack (0-3, -1 = not attacking)
    char last_skill_name[32]; // Name of the skill that was cast (for display)
    char last_skill_id[32];   // ID of the skill that was cast (for sprite lookup)
} BattleState;

// =============================================================================
// NEW RUNE BATTLE SYSTEM
// =============================================================================

// Rune chain constants
#define MAX_RUNE_CHAIN 8        // Max runes in a single chain
#define RUNE_TIMER_FRAMES 300   // 5 seconds at 60fps (adjustable)

// Rune battle phases
typedef enum {
    RUNE_PHASE_INTRO,           // Show "Battle Start!"
    RUNE_PHASE_PLAYER_CHAIN,    // Player drawing runes, timer running
    RUNE_PHASE_PLAYER_EXECUTE,  // Executing player's chain (animations)
    RUNE_PHASE_ENEMY_CHAIN,     // Enemy building chain (quick display)
    RUNE_PHASE_ENEMY_EXECUTE,   // Executing enemy's chain (animations)
    RUNE_PHASE_WIN,             // Player won
    RUNE_PHASE_LOSE             // Player lost
} RuneBattlePhase;

// Single rune in a chain
typedef struct {
    char id[RUNE_ID_LEN];       // e.g., "fox_1", "fire_3"
    char name[RUNE_NAME_LEN];   // e.g., "Quick Nip", "Ember"
    int power;                  // Base power of this rune
    int accuracy;               // Recognition accuracy (0-100)
    bool chain_bonus;           // True if chain bonus triggered
} ChainedRune;

// Rune battle state
typedef struct {
    RuneBattlePhase phase;

    // HP
    int player_hp;
    int player_max_hp;
    int enemy_hp;
    int enemy_max_hp;

    // Enemy info
    int enemy_mob_index;
    int enemy_level;
    char enemy_species[32];
    char enemy_element[32];

    // Player's current chain
    ChainedRune player_chain[MAX_RUNE_CHAIN];
    int player_chain_count;
    bool player_rune_used[MAX_RUNE_CHAIN * 2];  // Track which runes used this chain (by index in available)

    // Enemy's chain (built by server)
    ChainedRune enemy_chain[MAX_RUNE_CHAIN];
    int enemy_chain_count;

    // Timer for player turn (counts down)
    int chain_timer;            // Frames remaining
    int chain_timer_max;        // Starting value (for percentage display)

    // Current momentum multiplier (displayed to player)
    float momentum;             // 1.0 base + 0.5 per rune

    // Animation state
    int execute_index;          // Which rune in chain is being animated
    int execute_timer;          // Animation timer for current rune
    int total_damage_dealt;     // Accumulated damage for display

    // Available runes (loaded from server based on pet)
    char available_runes[20][RUNE_ID_LEN];   // IDs of runes pet can use
    char available_names[20][RUNE_NAME_LEN]; // Display names
    int available_count;

    // Last recognized rune (for feedback)
    char last_recognized_id[RUNE_ID_LEN];
    char last_recognized_name[RUNE_NAME_LEN];
    int last_accuracy;
    bool show_recognition_feedback;
    int feedback_timer;
} RuneBattleState;

// Gesture Recording
#define MAX_GESTURE_POINTS 200  // Increased for multi-stroke
#define MAX_STROKES 10          // Maximum separate strokes per gesture
#define STROKE_BREAK_MARKER -1  // Special value to mark stroke breaks

typedef struct {
    int16_t x;
    int16_t y;
} GesturePoint;

typedef struct {
    GesturePoint points[MAX_GESTURE_POINTS];
    int point_count;
    int stroke_starts[MAX_STROKES];  // Index where each stroke starts
    int stroke_count;                 // Number of strokes
    bool is_recording;
    bool gesture_ready;    // True when wheel clicked to confirm gesture
    bool has_strokes;      // True if any strokes have been drawn
} GestureState;

// Input State
typedef struct {
    int16_t touch_x;
    int16_t touch_y;
    bool is_touching;
    bool just_pressed;
    bool just_released;
    
    // Swipe Detection
    int16_t start_x;
    int16_t start_y;
    
    // Button Input (Physical)
    bool btn_clicked;        // Single Short Click (Interact / Select in Menu)
    bool btn_menu_open_combo; // Long Click (Open Menu)
    bool knob_rotated_left;  // Knob rotated left
    bool knob_rotated_right; // Knob rotated right
} InputState;

// Player State
typedef struct {
    int32_t x;          
    int32_t y;          
    int32_t vy;         
    bool facing_right;
    int frame_index;
    int anim_timer;     
} PlayerState;

// Main Game State
typedef struct {
    GameMode mode;
    MenuState menu_state;
    int menu_selection; // Current highlighted option (index depends on current menu_state)
    int menu_scroll_offset; // Scroll offset for main menu (to handle overflow)

    PlayerState player;
    int32_t cam_x;      
    int32_t cam_y;      
    InputState input;
    
    TileMap map; // Procedural Map Data
    
    // Settings
    uint8_t brightness;
    bool sound_on;

    // Map Transition
    bool request_new_map;
    int request_new_map_direction; // 1 for next, -1 for previous
    int exit_edge_used;  // 0=top, 1=bottom, 2=left, 3=right (which edge player exited from)
    
    // World Coordinates (Minecraft-style)
    int world_grid_x;
    int world_grid_y;

    // Gesture Recording
    GestureState gesture;

    // Battle (legacy)
    BattleState battle;

    // Rune Battle (new system)
    RuneBattleState rune_battle;

    // Calibration
    CalibrationState calibration;

    // Active Pet (from server)
    ActivePetState active_pet;
    PetStatsMenuState pet_menu;

    // Teleport
    TeleportState teleport;

    // Home Base Systems
    BuildingsState buildings;
    DNAInventoryState dna_inventory;
    CloningState cloning;
    PetYardState pet_yard;
} GameState;

// Public API
void game_init(GameState *state);
void game_update(GameState *state);
void generate_procedural_map(GameState *state); // New Generator
void game_handle_touch(GameState *state, int16_t x, int16_t y, bool is_pressed);
void game_handle_button_click(GameState *state);
void game_handle_menu_open_combo(GameState *state);
void game_handle_knob_rotate(GameState *state, bool left);
void game_confirm_gesture(GameState *state);
void game_clear_gesture(GameState *state);

// Battle functions (legacy)
void game_start_battle(GameState *state, int mob_index);
void game_update_battle(GameState *state);
SpellType game_recognize_gesture(GestureState *gest);
int game_check_nearby_mob(GameState *state);  // Returns mob index or -1

// Rune Battle functions (new system)
void game_start_rune_battle(GameState *state, int mob_index);
void game_update_rune_battle(GameState *state);
void game_rune_battle_add_available(GameState *state, const char *rune_id, const char *name);
void game_rune_battle_add_to_chain(GameState *state, const char *rune_id, const char *name, int power, int accuracy, bool chain_bonus);
void game_rune_battle_clear_chain(GameState *state);
void game_rune_battle_set_enemy_chain(GameState *state, int count);  // Prepare enemy chain array
void game_rune_battle_add_enemy_rune(GameState *state, int index, const char *rune_id, const char *name, int power);

// Calibration functions
void game_start_calibration(GameState *state);
void game_update_calibration(GameState *state);
void game_calibration_select_skill(GameState *state);
void game_calibration_back(GameState *state);
void game_calibration_add_skill(GameState *state, const char *skill_id, const char *name, int samples, bool calibrated);

// Pet functions (data populated by helloworld.c from server)
void game_set_active_pet(GameState *state, const char *id, const char *nickname,
                         const char *species, const char *element, const char *rarity,
                         int level, int xp, int xp_to_next, int hp, int max_hp, int current_hp,
                         int atk, int def, int spd);
void game_add_pet_skill(GameState *state, int skill_index, const char *id, const char *name,
                        const char *type, int power, int accuracy,
                        const char *effect, int effect_chance, bool has_gesture,
                        const int8_t *gesture_x, const int8_t *gesture_y, int gesture_count);
void game_add_skill_tree_entry(GameState *state, const char *id, const char *name,
                               const char *type, int power, int unlock_level,
                               bool unlocked, bool has_gesture);
void game_add_rune_tree_entry(GameState *state, const char *id, const char *name,
                              const char *source, const char *element, int power,
                              int unlock_level, bool unlocked, bool calibrated);
void game_clear_rune_tree(GameState *state);
void game_open_pet_stats(GameState *state);
void game_update_pet_stats_menu(GameState *state);

// Teleport functions (data populated by helloworld.c from server)
void game_open_teleport(GameState *state);
void game_add_discovered_map(GameState *state, int x, int y, const char *biome);
void game_teleport_select(GameState *state);  // Confirm teleport to selected map
void game_teleport_back(GameState *state);    // Go back to main menu

// Building interaction functions
void game_set_buildings(GameState *state, bool is_home_base);
void game_add_building(GameState *state, const char *name, int tile_x, int tile_y,
                       int width, int height, bool interactive,
                       int zone_x, int zone_y, int zone_w, int zone_h);
int game_check_building_interaction(GameState *state);  // Returns building index or -1
void game_enter_building(GameState *state, int building_index);

// DNA Inventory functions
void game_open_dna_inventory(GameState *state);
void game_add_dna_sample(GameState *state, const char *id, const char *species,
                         const char *element, const char *rarity, int level);
void game_dna_inventory_back(GameState *state);

// Cloning Center functions
void game_open_cloning_center(GameState *state);
void game_cloning_select_sample(GameState *state);  // Select current highlighted sample
void game_cloning_confirm(GameState *state);        // Confirm and start cloning
void game_cloning_back(GameState *state);           // Cancel/go back
void game_cloning_set_result(GameState *state, const char *species, const char *element, int level);

// Pet Yard functions
void game_open_pet_yard(GameState *state);
void game_add_pet_entry(GameState *state, const char *id, const char *nickname,
                        const char *species, const char *element, int level,
                        int current_hp, int max_hp);
void game_pet_yard_select(GameState *state);  // Switch to selected pet
void game_pet_yard_back(GameState *state);

#endif // GAME_LOGIC_H