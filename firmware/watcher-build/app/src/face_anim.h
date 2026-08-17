#pragma once

#include "face_types.h"

// Initialize the face system. Call after bsp_lvgl_init().
void face_init(void);

// Set expression (smooth transition). Safe to call from any task.
void face_set_expression(face_expr_t expr);

// Get current expression.
face_expr_t face_get_current_expression(void);

// Start/stop talk mode mouth animation.
void face_set_talking(bool talking);

// Set pupil look direction. dx,dy in -100..+100.
void face_set_look(int16_t dx, int16_t dy);

// Trigger an immediate blink.
void face_blink(void);
