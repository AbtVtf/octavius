#pragma once

#include <stdint.h>
#include <stdbool.h>

#define SCR_W 412
#define SCR_H 412

// Expression enum
typedef enum {
    FACE_IDLE = 0,
    FACE_HAPPY,
    FACE_TALK,
    FACE_SAD,
    FACE_ANGRY,
    FACE_EXCITED,
    FACE_SURPRISED,
    FACE_SLEEPY,
    FACE_LOVE,
    FACE_CONFUSED,
    FACE_THINKING,
    FACE_SCARED,
    FACE_WINK,
    FACE_SMUG,
    FACE_DEAD,
    FACE_DIZZY,
    FACE_EXPR_COUNT
} face_expr_t;

// Full face parameter set — defines how the face looks at any instant
typedef struct {
    // Eye geometry
    int16_t eye_w;              // Eye body width (80..130)
    int16_t eye_h;              // Eye body height (usually == eye_w)
    int16_t eye_clip_h;         // Clip height — controls openness (4..70)
    int16_t eye_y;              // Vertical center of eyes on screen
    int16_t eye_spacing;        // Horizontal distance between eye centers

    // Per-eye asymmetry (added to base clip_h)
    int16_t left_clip_offset;   // 0 normally; negative for wink
    int16_t right_clip_offset;

    // Eye color (RGB)
    uint8_t eye_r, eye_g, eye_b;

    // Pupil
    int16_t pupil_r;            // Pupil radius
    int16_t pupil_dx;           // Horizontal offset from eye center
    int16_t pupil_dy;           // Vertical offset
    uint8_t pupil_r_col, pupil_g_col, pupil_b_col;

    // Highlight (reflection dot in pupil)
    int16_t highlight_r;        // Highlight radius
    int16_t highlight_dx;       // Offset from pupil center
    int16_t highlight_dy;

    // Eyebrows
    int16_t brow_w;             // Width
    int16_t brow_h;             // Thickness
    int16_t brow_y_offset;      // Vertical offset above eye center (negative = above)
    int16_t left_brow_angle;    // Rotation in 0.1 degrees
    int16_t right_brow_angle;
    uint8_t brow_opa;           // 0=hidden, 255=visible

    // Mouth
    int16_t mouth_w;            // Width
    int16_t mouth_h;            // Height/openness (0=closed line)
    int16_t mouth_y;            // Y offset below screen center
    int16_t mouth_curve;        // -100..+100: negative=frown, positive=smile
    uint8_t mouth_opa;          // Opacity

    // Global
    int16_t face_y_offset;      // Whole-face vertical shift (breathing)
} face_params_t;

// Interpolation speed categories
#define LERP_FAST    128     // ~50% per tick — blinks
#define LERP_MEDIUM   38     // ~15% per tick — expression transitions
#define LERP_SLOW     13     // ~5% per tick — breathing, drift
