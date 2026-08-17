#include "face_anim.h"
#include "lvgl.h"
#include "sensecap-watcher.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include <string.h>
#include <math.h>

static const char *TAG = "FaceAnim";

// External from face_expressions.c
extern const face_params_t* face_expr_get(face_expr_t expr);

// ---- State ----
static face_params_t current;
static face_params_t target;
static face_params_t base;       // expression home (micro-anims return here)
static face_params_t prev;       // last applied — for dirty tracking
static face_expr_t   current_expr = FACE_IDLE;
static bool          transition_active = false;
static bool          talk_active = false;
static bool          blink_closing = false;
static uint32_t      blink_reopen_at = 0;
static bool          double_blink_pending = false;

// Micro-animation timers
static uint32_t next_blink_ms = 0;
static uint32_t next_saccade_ms = 0;
static uint32_t next_drift_ms = 0;
static uint32_t saccade_return_ms = 0;
static uint32_t drift_return_ms = 0;
static bool     saccade_active = false;
static bool     drift_active = false;

// Breathing
static int16_t breath_phase = 0;  // 0..628 (2*PI*100)

// Talk animation
static int16_t talk_phase = 0;
static int16_t talk_base_mouth_h = 0;

// ---- LVGL Objects ----
static lv_obj_t *face_bg = NULL;
static lv_obj_t *left_eye_clip = NULL;
static lv_obj_t *right_eye_clip = NULL;
static lv_obj_t *left_eye_body = NULL;
static lv_obj_t *right_eye_body = NULL;
static lv_obj_t *left_pupil = NULL;
static lv_obj_t *right_pupil = NULL;
static lv_obj_t *left_highlight = NULL;
static lv_obj_t *right_highlight = NULL;
static lv_obj_t *left_brow = NULL;
static lv_obj_t *right_brow = NULL;

// Mouth canvas
static lv_obj_t *mouth_canvas = NULL;
#define MOUTH_CW 160
#define MOUTH_CH 80
static lv_color_t *mouth_buf = NULL;

// LVGL timers
static lv_timer_t *update_timer = NULL;

// ---- Helpers ----

static inline int16_t lerp16(int16_t cur, int16_t tgt, int16_t alpha) {
    int32_t diff = tgt - cur;
    if (diff == 0) return cur;
    int32_t step = (diff * alpha) >> 8;
    if (step == 0) step = (diff > 0) ? 1 : -1;
    return (int16_t)(cur + step);
}

static inline uint8_t lerp8(uint8_t cur, uint8_t tgt, int16_t alpha) {
    int16_t diff = (int16_t)tgt - (int16_t)cur;
    if (diff == 0) return cur;
    int16_t step = (diff * alpha) >> 8;
    if (step == 0) step = (diff > 0) ? 1 : -1;
    return (uint8_t)(cur + step);
}

static void interpolate_params(face_params_t *c, const face_params_t *t, int16_t alpha) {
    c->eye_w = lerp16(c->eye_w, t->eye_w, alpha);
    c->eye_h = lerp16(c->eye_h, t->eye_h, alpha);
    c->eye_clip_h = lerp16(c->eye_clip_h, t->eye_clip_h, alpha);
    c->eye_y = lerp16(c->eye_y, t->eye_y, alpha);
    c->eye_spacing = lerp16(c->eye_spacing, t->eye_spacing, alpha);
    c->left_clip_offset = lerp16(c->left_clip_offset, t->left_clip_offset, alpha);
    c->right_clip_offset = lerp16(c->right_clip_offset, t->right_clip_offset, alpha);
    c->eye_r = lerp8(c->eye_r, t->eye_r, alpha);
    c->eye_g = lerp8(c->eye_g, t->eye_g, alpha);
    c->eye_b = lerp8(c->eye_b, t->eye_b, alpha);
    c->pupil_r = lerp16(c->pupil_r, t->pupil_r, alpha);
    c->pupil_dx = lerp16(c->pupil_dx, t->pupil_dx, alpha);
    c->pupil_dy = lerp16(c->pupil_dy, t->pupil_dy, alpha);
    c->pupil_r_col = lerp8(c->pupil_r_col, t->pupil_r_col, alpha);
    c->pupil_g_col = lerp8(c->pupil_g_col, t->pupil_g_col, alpha);
    c->pupil_b_col = lerp8(c->pupil_b_col, t->pupil_b_col, alpha);
    c->highlight_r = lerp16(c->highlight_r, t->highlight_r, alpha);
    c->highlight_dx = lerp16(c->highlight_dx, t->highlight_dx, alpha);
    c->highlight_dy = lerp16(c->highlight_dy, t->highlight_dy, alpha);
    c->brow_w = lerp16(c->brow_w, t->brow_w, alpha);
    c->brow_h = lerp16(c->brow_h, t->brow_h, alpha);
    c->brow_y_offset = lerp16(c->brow_y_offset, t->brow_y_offset, alpha);
    c->left_brow_angle = lerp16(c->left_brow_angle, t->left_brow_angle, alpha);
    c->right_brow_angle = lerp16(c->right_brow_angle, t->right_brow_angle, alpha);
    c->brow_opa = lerp8(c->brow_opa, t->brow_opa, alpha);
    c->mouth_w = lerp16(c->mouth_w, t->mouth_w, alpha);
    c->mouth_h = lerp16(c->mouth_h, t->mouth_h, alpha);
    c->mouth_y = lerp16(c->mouth_y, t->mouth_y, alpha);
    c->mouth_curve = lerp16(c->mouth_curve, t->mouth_curve, alpha);
    c->mouth_opa = lerp8(c->mouth_opa, t->mouth_opa, alpha);
    c->face_y_offset = lerp16(c->face_y_offset, t->face_y_offset, alpha);
}

static bool params_equal(const face_params_t *a, const face_params_t *b) {
    return memcmp(a, b, sizeof(face_params_t)) == 0;
}

static bool params_close(const face_params_t *c, const face_params_t *t) {
    // Check if current is close enough to target that transition is done
    if (abs(c->eye_w - t->eye_w) > 2) return false;
    if (abs(c->eye_clip_h - t->eye_clip_h) > 2) return false;
    if (abs(c->eye_y - t->eye_y) > 2) return false;
    if (abs(c->pupil_dx - t->pupil_dx) > 2) return false;
    if (abs(c->pupil_dy - t->pupil_dy) > 2) return false;
    if (abs(c->mouth_h - t->mouth_h) > 2) return false;
    if (abs(c->mouth_curve - t->mouth_curve) > 3) return false;
    if (abs(c->left_brow_angle - t->left_brow_angle) > 5) return false;
    return true;
}

// ---- Mouth Rendering ----

static void render_mouth(const face_params_t *p) {
    if (!mouth_canvas || !mouth_buf) return;
    if (p->mouth_opa < 30) {
        // Nearly invisible — just clear
        lv_canvas_fill_bg(mouth_canvas, lv_color_black(), LV_OPA_TRANSP);
        return;
    }

    // Clear canvas
    lv_canvas_fill_bg(mouth_canvas, lv_color_black(), LV_OPA_TRANSP);

    lv_draw_rect_dsc_t rect_dsc;
    lv_draw_rect_dsc_init(&rect_dsc);
    rect_dsc.bg_color = lv_color_white();
    rect_dsc.bg_opa = p->mouth_opa;

    lv_draw_line_dsc_t line_dsc;
    lv_draw_line_dsc_init(&line_dsc);
    line_dsc.color = lv_color_white();
    line_dsc.opa = p->mouth_opa;
    line_dsc.width = 4;

    int cx = MOUTH_CW / 2;
    int cy = MOUTH_CH / 2;

    if (p->mouth_h > 3) {
        // Open mouth — rounded rectangle
        int mw = p->mouth_w;
        int mh = p->mouth_h;
        if (mw < 10) mw = 10;
        if (mh < 4) mh = 4;
        rect_dsc.radius = mh / 2;
        if (rect_dsc.radius < 4) rect_dsc.radius = 4;

        // If smiling, curve top edge by shifting rect up slightly
        int y_shift = (p->mouth_curve > 0) ? -(p->mouth_curve / 20) : ((-p->mouth_curve) / 25);

        lv_area_t area = {
            .x1 = cx - mw / 2,
            .y1 = cy - mh / 2 + y_shift,
            .x2 = cx + mw / 2,
            .y2 = cy + mh / 2 + y_shift
        };
        lv_canvas_draw_rect(mouth_canvas, area.x1, area.y1, mw, mh, &rect_dsc);

        // Dark inner mouth for larger openings
        if (mh > 10) {
            lv_draw_rect_dsc_t inner;
            lv_draw_rect_dsc_init(&inner);
            inner.bg_color = lv_color_make(40, 20, 30);
            inner.bg_opa = LV_OPA_COVER;
            inner.radius = (mh - 8) / 2;
            if (inner.radius < 2) inner.radius = 2;
            lv_canvas_draw_rect(mouth_canvas, cx - mw/2 + 4, cy - mh/2 + 4 + y_shift,
                                mw - 8, mh - 8, &inner);
        }
    } else if (p->mouth_curve != 0) {
        // Smile or frown line — draw as a series of points forming a curve
        int hw = p->mouth_w / 2;
        float curve_strength = (float)p->mouth_curve / 100.0f;

        for (int x = -hw; x <= hw; x += 2) {
            float t = (float)x / (float)hw;  // -1..1
            float y_off = curve_strength * (1.0f - t * t) * 15.0f;
            int px = cx + x;
            int py = cy - (int)y_off;
            if (px >= 0 && px < MOUTH_CW && py >= 0 && py < MOUTH_CH) {
                // Draw a small filled area for thickness
                for (int dy = -1; dy <= 1; dy++) {
                    for (int dx = -1; dx <= 1; dx++) {
                        int fx = px + dx, fy = py + dy;
                        if (fx >= 0 && fx < MOUTH_CW && fy >= 0 && fy < MOUTH_CH) {
                            lv_canvas_set_px_color(mouth_canvas, fx, fy, lv_color_white());
                            lv_canvas_set_px_opa(mouth_canvas, fx, fy, p->mouth_opa);
                        }
                    }
                }
            }
        }
    } else {
        // Neutral line
        lv_point_t pts[2] = {
            {cx - p->mouth_w / 2, cy},
            {cx + p->mouth_w / 2, cy}
        };
        lv_canvas_draw_line(mouth_canvas, pts, 2, &line_dsc);
    }
}

// ---- Apply to LVGL Objects ----

static void apply_to_objects(const face_params_t *p) {
    int16_t y_off = p->face_y_offset;

    // -- Left eye --
    int16_t l_clip_h = p->eye_clip_h + p->left_clip_offset;
    if (l_clip_h < 4) l_clip_h = 4;
    lv_obj_set_size(left_eye_clip, p->eye_w, l_clip_h);
    lv_obj_set_pos(left_eye_clip,
                   SCR_W/2 - p->eye_spacing/2 - p->eye_w/2,
                   p->eye_y - l_clip_h/2 + y_off);

    lv_obj_set_size(left_eye_body, p->eye_w, p->eye_h);
    lv_obj_set_pos(left_eye_body, 0, 0);
    lv_obj_set_style_bg_color(left_eye_body, lv_color_make(p->eye_r, p->eye_g, p->eye_b), 0);
    lv_obj_set_style_radius(left_eye_body, p->eye_w / 2, 0);

    // -- Right eye --
    int16_t r_clip_h = p->eye_clip_h + p->right_clip_offset;
    if (r_clip_h < 4) r_clip_h = 4;
    lv_obj_set_size(right_eye_clip, p->eye_w, r_clip_h);
    lv_obj_set_pos(right_eye_clip,
                   SCR_W/2 + p->eye_spacing/2 - p->eye_w/2,
                   p->eye_y - r_clip_h/2 + y_off);

    lv_obj_set_size(right_eye_body, p->eye_w, p->eye_h);
    lv_obj_set_pos(right_eye_body, 0, 0);
    lv_obj_set_style_bg_color(right_eye_body, lv_color_make(p->eye_r, p->eye_g, p->eye_b), 0);
    lv_obj_set_style_radius(right_eye_body, p->eye_w / 2, 0);

    // -- Pupils --
    if (p->pupil_r > 0) {
        int pr2 = p->pupil_r * 2;
        lv_obj_set_size(left_pupil, pr2, pr2);
        lv_obj_set_pos(left_pupil,
                       p->eye_w/2 - p->pupil_r + p->pupil_dx,
                       p->eye_h/2 - p->pupil_r + p->pupil_dy);
        lv_obj_set_style_bg_color(left_pupil,
            lv_color_make(p->pupil_r_col, p->pupil_g_col, p->pupil_b_col), 0);
        lv_obj_set_style_radius(left_pupil, p->pupil_r, 0);
        lv_obj_clear_flag(left_pupil, LV_OBJ_FLAG_HIDDEN);

        lv_obj_set_size(right_pupil, pr2, pr2);
        lv_obj_set_pos(right_pupil,
                       p->eye_w/2 - p->pupil_r + p->pupil_dx,
                       p->eye_h/2 - p->pupil_r + p->pupil_dy);
        lv_obj_set_style_bg_color(right_pupil,
            lv_color_make(p->pupil_r_col, p->pupil_g_col, p->pupil_b_col), 0);
        lv_obj_set_style_radius(right_pupil, p->pupil_r, 0);
        lv_obj_clear_flag(right_pupil, LV_OBJ_FLAG_HIDDEN);

        // Highlights
        if (p->highlight_r > 0) {
            int hr2 = p->highlight_r * 2;
            lv_obj_set_size(left_highlight, hr2, hr2);
            lv_obj_set_pos(left_highlight,
                           p->pupil_r - p->highlight_r + p->highlight_dx,
                           p->pupil_r - p->highlight_r + p->highlight_dy);
            lv_obj_clear_flag(left_highlight, LV_OBJ_FLAG_HIDDEN);

            lv_obj_set_size(right_highlight, hr2, hr2);
            lv_obj_set_pos(right_highlight,
                           p->pupil_r - p->highlight_r + p->highlight_dx,
                           p->pupil_r - p->highlight_r + p->highlight_dy);
            lv_obj_clear_flag(right_highlight, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(left_highlight, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(right_highlight, LV_OBJ_FLAG_HIDDEN);
        }
    } else {
        lv_obj_add_flag(left_pupil, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(right_pupil, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(left_highlight, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(right_highlight, LV_OBJ_FLAG_HIDDEN);
    }

    // -- Eyebrows --
    if (p->brow_opa > 20) {
        lv_obj_set_size(left_brow, p->brow_w, p->brow_h);
        lv_obj_set_pos(left_brow,
                       SCR_W/2 - p->eye_spacing/2 - p->brow_w/2,
                       p->eye_y + p->brow_y_offset + y_off);
        lv_obj_set_style_bg_opa(left_brow, p->brow_opa, 0);
        lv_obj_set_style_transform_angle(left_brow, p->left_brow_angle, 0);
        lv_obj_clear_flag(left_brow, LV_OBJ_FLAG_HIDDEN);

        lv_obj_set_size(right_brow, p->brow_w, p->brow_h);
        lv_obj_set_pos(right_brow,
                       SCR_W/2 + p->eye_spacing/2 - p->brow_w/2,
                       p->eye_y + p->brow_y_offset + y_off);
        lv_obj_set_style_bg_opa(right_brow, p->brow_opa, 0);
        lv_obj_set_style_transform_angle(right_brow, p->right_brow_angle, 0);
        lv_obj_clear_flag(right_brow, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(left_brow, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(right_brow, LV_OBJ_FLAG_HIDDEN);
    }

    // -- Mouth --
    if (mouth_canvas) {
        lv_obj_set_pos(mouth_canvas,
                       SCR_W/2 - MOUTH_CW/2,
                       p->eye_y + p->mouth_y - MOUTH_CH/2 + y_off);
        if (p->mouth_opa > 20) {
            lv_obj_clear_flag(mouth_canvas, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(mouth_canvas, LV_OBJ_FLAG_HIDDEN);
        }
    }

}

// ---- Micro-Animations ----

static uint32_t millis_now(void) {
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static uint32_t rand_range(uint32_t lo, uint32_t hi) {
    return lo + (esp_random() % (hi - lo + 1));
}

static void micro_animations(void) {
    uint32_t now = millis_now();

    // Don't run micro-anims during expression transitions
    if (transition_active) return;

    // ---- Blink ----
    if (blink_closing) {
        if (now >= blink_reopen_at) {
            // Reopen: restore clip_h to base
            target.eye_clip_h = base.eye_clip_h;
            target.left_clip_offset = base.left_clip_offset;
            target.right_clip_offset = base.right_clip_offset;
            blink_closing = false;

            // Double blink?
            if (double_blink_pending) {
                double_blink_pending = false;
                next_blink_ms = now + 180;
            } else {
                next_blink_ms = now + rand_range(2500, 5500);
            }
        }
    } else if (now >= next_blink_ms) {
        // Close eyes
        target.eye_clip_h = 4;
        target.left_clip_offset = 0;
        target.right_clip_offset = 0;
        blink_closing = true;
        blink_reopen_at = now + 120;
        double_blink_pending = (rand_range(0, 100) < 15);
    }

    // ---- Pupil Saccade ----
    if (saccade_active) {
        if (now >= saccade_return_ms) {
            target.pupil_dx = base.pupil_dx;
            target.pupil_dy = base.pupil_dy;
            saccade_active = false;
            next_saccade_ms = now + rand_range(3000, 7000);
        }
    } else if (now >= next_saccade_ms && !talk_active) {
        target.pupil_dx = base.pupil_dx + (int16_t)rand_range(0, 30) - 15;
        target.pupil_dy = base.pupil_dy + (int16_t)rand_range(0, 20) - 10;
        saccade_active = true;
        saccade_return_ms = now + rand_range(400, 1200);
    }

    // ---- Eye Drift ----
    if (drift_active) {
        if (now >= drift_return_ms) {
            target.eye_y = base.eye_y;
            drift_active = false;
            next_drift_ms = now + rand_range(8000, 15000);
        }
    } else if (now >= next_drift_ms) {
        target.eye_y = base.eye_y + (int16_t)rand_range(0, 8) - 4;
        drift_active = true;
        drift_return_ms = now + rand_range(600, 1500);
    }

    // ---- Breathing (disabled during debug — re-enable once rendering is stable) ----
    // breath_phase += 2;
    // if (breath_phase > 628) breath_phase = 0;
    // float breath_sin = sinf((float)breath_phase / 100.0f);
    // target.face_y_offset = base.face_y_offset + (int16_t)(breath_sin * 2.0f);

    // ---- Talk mouth cycling ----
    if (talk_active) {
        talk_phase += 18;  // Fast cycle
        if (talk_phase > 628) talk_phase = 0;
        float talk_sin = sinf((float)talk_phase / 100.0f);
        float open = (talk_sin + 1.0f) / 2.0f;  // 0..1
        target.mouth_h = talk_base_mouth_h + (int16_t)(open * 20.0f);
        // Small pupil jitter during talk
        if (rand_range(0, 100) < 8) {
            target.pupil_dx = base.pupil_dx + (int16_t)rand_range(0, 10) - 5;
        }
    }
}

// ---- 30Hz Update Tick ----

static uint8_t skip_counter = 0;

static void face_update_tick(lv_timer_t *t) {
    // Run micro-animations every tick (modifies target)
    micro_animations();

    // Interpolate current toward target
    interpolate_params(&current, &target, LERP_MEDIUM);

    // Fast blink interpolation override
    if (blink_closing) {
        current.eye_clip_h = lerp16(current.eye_clip_h, target.eye_clip_h, LERP_FAST);
    }

    // Check if transition is done
    if (transition_active && params_close(&current, &target)) {
        transition_active = false;
    }

    // Only update LVGL objects every 3rd tick (~10Hz) unless transitioning or blinking
    skip_counter++;
    if (!transition_active && !blink_closing && (skip_counter % 3 != 0)) return;

    // Dirty check — only update LVGL if something actually changed by a pixel
    if (params_equal(&current, &prev)) return;

    // Check if mouth needs redraw
    bool mouth_dirty = (current.mouth_w != prev.mouth_w ||
                        current.mouth_h != prev.mouth_h ||
                        current.mouth_curve != prev.mouth_curve ||
                        current.mouth_opa != prev.mouth_opa);

    // Apply to LVGL objects
    apply_to_objects(&current);

    if (mouth_dirty) {
        render_mouth(&current);
    }

    prev = current;
}

// ---- Object Creation ----

static lv_obj_t* create_face_obj(lv_obj_t *parent) {
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

static void create_objects(void) {
    // Use the EXACT same pattern as the old working code

    // Background
    face_bg = lv_obj_create(lv_scr_act());
    lv_obj_set_size(face_bg, SCR_W, SCR_H);
    lv_obj_set_pos(face_bg, 0, 0);
    lv_obj_set_style_bg_color(face_bg, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(face_bg, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(face_bg, 0, 0);
    lv_obj_set_style_border_width(face_bg, 0, 0);
    lv_obj_set_style_pad_all(face_bg, 0, 0);
    lv_obj_clear_flag(face_bg, LV_OBJ_FLAG_SCROLLABLE);

    // Left eye clip — exact same as old working code
    left_eye_clip = lv_obj_create(face_bg);
    lv_obj_set_style_bg_opa(left_eye_clip, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(left_eye_clip, 0, 0);
    lv_obj_set_style_pad_all(left_eye_clip, 0, 0);
    lv_obj_set_style_clip_corner(left_eye_clip, true, 0);
    lv_obj_clear_flag(left_eye_clip, LV_OBJ_FLAG_SCROLLABLE);

    // Left eye body
    left_eye_body = lv_obj_create(left_eye_clip);
    lv_obj_set_style_border_width(left_eye_body, 0, 0);
    lv_obj_set_style_bg_color(left_eye_body, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(left_eye_body, LV_OPA_COVER, 0);
    lv_obj_clear_flag(left_eye_body, LV_OBJ_FLAG_SCROLLABLE);

    // Left pupil (child of eye body)
    left_pupil = lv_obj_create(left_eye_body);
    lv_obj_set_style_border_width(left_pupil, 0, 0);
    lv_obj_set_style_bg_opa(left_pupil, LV_OPA_COVER, 0);
    lv_obj_clear_flag(left_pupil, LV_OBJ_FLAG_SCROLLABLE);

    // Left highlight (child of pupil)
    left_highlight = lv_obj_create(left_pupil);
    lv_obj_set_style_border_width(left_highlight, 0, 0);
    lv_obj_set_style_bg_color(left_highlight, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(left_highlight, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(left_highlight, LV_RADIUS_CIRCLE, 0);
    lv_obj_clear_flag(left_highlight, LV_OBJ_FLAG_SCROLLABLE);

    // Right eye clip
    right_eye_clip = lv_obj_create(face_bg);
    lv_obj_set_style_bg_opa(right_eye_clip, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(right_eye_clip, 0, 0);
    lv_obj_set_style_pad_all(right_eye_clip, 0, 0);
    lv_obj_set_style_clip_corner(right_eye_clip, true, 0);
    lv_obj_clear_flag(right_eye_clip, LV_OBJ_FLAG_SCROLLABLE);

    // Right eye body
    right_eye_body = lv_obj_create(right_eye_clip);
    lv_obj_set_style_border_width(right_eye_body, 0, 0);
    lv_obj_set_style_bg_color(right_eye_body, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(right_eye_body, LV_OPA_COVER, 0);
    lv_obj_clear_flag(right_eye_body, LV_OBJ_FLAG_SCROLLABLE);

    // Right pupil
    right_pupil = lv_obj_create(right_eye_body);
    lv_obj_set_style_border_width(right_pupil, 0, 0);
    lv_obj_set_style_bg_opa(right_pupil, LV_OPA_COVER, 0);
    lv_obj_clear_flag(right_pupil, LV_OBJ_FLAG_SCROLLABLE);

    // Right highlight
    right_highlight = lv_obj_create(right_pupil);
    lv_obj_set_style_border_width(right_highlight, 0, 0);
    lv_obj_set_style_bg_color(right_highlight, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(right_highlight, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(right_highlight, LV_RADIUS_CIRCLE, 0);
    lv_obj_clear_flag(right_highlight, LV_OBJ_FLAG_SCROLLABLE);

    // Eyebrows — start hidden
    left_brow = lv_obj_create(face_bg);
    lv_obj_set_style_border_width(left_brow, 0, 0);
    lv_obj_set_style_bg_color(left_brow, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(left_brow, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(left_brow, 4, 0);
    lv_obj_clear_flag(left_brow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(left_brow, LV_OBJ_FLAG_HIDDEN);

    right_brow = lv_obj_create(face_bg);
    lv_obj_set_style_border_width(right_brow, 0, 0);
    lv_obj_set_style_bg_color(right_brow, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(right_brow, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(right_brow, 4, 0);
    lv_obj_clear_flag(right_brow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(right_brow, LV_OBJ_FLAG_HIDDEN);

    // Mouth canvas
    mouth_buf = heap_caps_malloc(MOUTH_CW * MOUTH_CH * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    if (mouth_buf) {
        mouth_canvas = lv_canvas_create(face_bg);
        lv_canvas_set_buffer(mouth_canvas, mouth_buf, MOUTH_CW, MOUTH_CH, LV_IMG_CF_TRUE_COLOR_ALPHA);
        lv_canvas_fill_bg(mouth_canvas, lv_color_black(), LV_OPA_TRANSP);
        ESP_LOGI(TAG, "Mouth canvas created (%dx%d)", MOUTH_CW, MOUTH_CH);
    } else {
        ESP_LOGW(TAG, "Failed to allocate mouth canvas in PSRAM");
    }
}

// ---- Public API ----

void face_init(void) {
    ESP_LOGI(TAG, "Face animation system init");

    lvgl_port_lock(0);

    // Disable all input devices — the knob polls every tick causing constant redraws
    lv_indev_t *indev = lv_indev_get_next(NULL);
    while (indev) {
        lv_indev_enable(indev, false);
        indev = lv_indev_get_next(indev);
    }
    ESP_LOGI(TAG, "Input devices disabled");

    create_objects();
    lvgl_port_unlock();

    // Initialize to idle
    const face_params_t *idle = face_expr_get(FACE_IDLE);
    memcpy(&current, idle, sizeof(face_params_t));
    memcpy(&target, idle, sizeof(face_params_t));
    memcpy(&base, idle, sizeof(face_params_t));
    memcpy(&prev, idle, sizeof(face_params_t));

    // Apply initial state
    lvgl_port_lock(0);
    apply_to_objects(&current);
    render_mouth(&current);
    lvgl_port_unlock();

    // Seed micro-animation timers
    uint32_t now = millis_now();
    next_blink_ms = now + rand_range(2000, 4000);
    next_saccade_ms = now + rand_range(2000, 5000);
    next_drift_ms = now + rand_range(5000, 10000);

    // TEMPORARILY DISABLED: animation timer — testing static render
    // lvgl_port_lock(0);
    // update_timer = lv_timer_create(face_update_tick, 33, NULL);
    // lvgl_port_unlock();

    ESP_LOGI(TAG, "Face init done (static mode — no animation timer)");
}

void face_set_expression(face_expr_t expr) {
    if (expr >= FACE_EXPR_COUNT) expr = FACE_IDLE;
    current_expr = expr;

    const face_params_t *e = face_expr_get(expr);
    memcpy(&target, e, sizeof(face_params_t));
    memcpy(&base, e, sizeof(face_params_t));
    transition_active = true;

    // Reset micro-anim state so they don't fight the transition
    saccade_active = false;
    drift_active = false;
    blink_closing = false;
    uint32_t now = millis_now();
    next_blink_ms = now + rand_range(1500, 3000);
    next_saccade_ms = now + rand_range(2000, 4000);
    next_drift_ms = now + rand_range(5000, 10000);
    breath_phase = 0;

    // Handle talk mode
    if (expr == FACE_TALK) {
        face_set_talking(true);
    } else if (talk_active) {
        face_set_talking(false);
    }
}

face_expr_t face_get_current_expression(void) {
    return current_expr;
}

void face_set_talking(bool talking) {
    talk_active = talking;
    if (talking) {
        talk_phase = 0;
        talk_base_mouth_h = base.mouth_h;
    } else {
        target.mouth_h = base.mouth_h;
    }
}

void face_set_look(int16_t dx, int16_t dy) {
    // Map -100..100 to pixel offsets
    target.pupil_dx = base.pupil_dx + (dx * 25) / 100;
    target.pupil_dy = base.pupil_dy + (dy * 15) / 100;
    saccade_active = false;
}

void face_blink(void) {
    if (!blink_closing) {
        target.eye_clip_h = 4;
        target.left_clip_offset = 0;
        target.right_clip_offset = 0;
        blink_closing = true;
        blink_reopen_at = millis_now() + 120;
        double_blink_pending = false;
    }
}
