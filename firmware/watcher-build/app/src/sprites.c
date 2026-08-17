#include "lvgl.h"

#ifndef LV_ATTRIBUTE_MEM_ALIGN
#define LV_ATTRIBUTE_MEM_ALIGN
#endif

/* 
 * A simple 12x10 pixel green "Invader" sprite (RGB565 format)
 * 1 pixel = 2 bytes
 * Green = 0x07E0
 * Black = 0x0000
 */
#define C 0x07E0 // Color (Green)
#define _ 0x0000 // Background (Transparent/Black)

/* Frame 1: Arms down / Legs closed */
const uint8_t sprite_walk_left_map[] = {
  _,_,_,_,C,C,C,C,_,_,_,_,
  _,C,C,C,C,C,C,C,C,C,C,_,
  C,C,_,_,C,C,C,C,_,_,C,C,
  C,C,C,C,C,C,C,C,C,C,C,C,
  C,C,C,_,C,C,C,C,_,C,C,C,
  C,C,C,C,C,C,C,C,C,C,C,C,
  _,_,_,C,C,_,_,C,C,_,_,_,
  _,_,C,C,_,_,_,_,C,C,_,_,
  _,C,C,_,_,_,_,_,_,C,C,_,
  C,C,_,_,_,_,_,_,_,_,C,C,
};

/* Frame 2: Arms up / Legs open */
const uint8_t sprite_walk_right_map[] = {
  _,_,_,_,C,C,C,C,_,_,_,_,
  _,C,C,C,C,C,C,C,C,C,C,_,
  C,C,_,_,C,C,C,C,_,_,C,C,
  C,C,C,C,C,C,C,C,C,C,C,C,
  C,C,C,_,C,C,C,C,_,C,C,C,
  C,C,C,C,C,C,C,C,C,C,C,C,
  _,_,_,C,C,_,_,C,C,_,_,_,
  _,_,C,C,_,_,_,_,C,C,_,_,
  _,_,C,C,_,_,_,_,C,C,_,_,
  _,_,_,C,C,_,_,C,C,_,_,_,
};

const lv_img_dsc_t sprite_walk_left = {
  .header.always_zero = 0,
  .header.w = 12,
  .header.h = 10,
  .data_size = 12 * 10 * 2, 
  .header.cf = LV_IMG_CF_TRUE_COLOR, 
  .data = sprite_walk_left_map,
};

const lv_img_dsc_t sprite_walk_right = {
  .header.always_zero = 0,
  .header.w = 12,
  .header.h = 10,
  .data_size = 12 * 10 * 2,
  .header.cf = LV_IMG_CF_TRUE_COLOR,
  .data = sprite_walk_right_map,
};
