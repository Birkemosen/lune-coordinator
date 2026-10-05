// Genereret af tools/lds_display.py fra tokens.json — redigér ikke.
// Lune Design System · vægskærm 1024×600 · RGB565
#pragma once
#include "lvgl.h"

// ---- dark ----
#define LDS_DARK_BG           lv_color_hex(0x101010)   /* 565: 0x1082 */
#define LDS_DARK_CARD         lv_color_hex(0x212019)   /* 565: 0x2103 */
#define LDS_DARK_RAISED       lv_color_hex(0x292821)   /* 565: 0x2944 */
#define LDS_DARK_FIELD        lv_color_hex(0x191819)   /* 565: 0x18C3 */
#define LDS_DARK_LINE         lv_color_hex(0x313129)   /* 565: 0x3185 */
#define LDS_DARK_SEG_OFF      lv_color_hex(0x313129)   /* 565: 0x3185 */
#define LDS_DARK_FG           lv_color_hex(0xf7f3ef)   /* 565: 0xF79D */
#define LDS_DARK_MUTED        lv_color_hex(0xa5a29c)   /* 565: 0xA513 */
#define LDS_DARK_FAINT        lv_color_hex(0x8c8a84)   /* 565: 0x8C50 */
#define LDS_DARK_ACCENT       lv_color_hex(0xde7131)   /* 565: 0xDB86 */
#define LDS_DARK_ACCENT_INK   lv_color_hex(0xe6824a)   /* 565: 0xE409 */
#define LDS_DARK_ON_ACCENT    lv_color_hex(0x101010)   /* 565: 0x1082 */
#define LDS_DARK_INFO         lv_color_hex(0x6b9ece)   /* 565: 0x6CF9 */
#define LDS_DARK_OK           lv_color_hex(0x73b25a)   /* 565: 0x758B */
#define LDS_DARK_WARN         lv_color_hex(0xceaa42)   /* 565: 0xCD48 */
#define LDS_DARK_DANGER       lv_color_hex(0xd67973)   /* 565: 0xD3CE */
#define LDS_DARK_VIOLET       lv_color_hex(0x9479ce)   /* 565: 0x93D9 */
#define LDS_DARK_INFO_BG      lv_color_hex(0x192029)   /* 565: 0x1905 */
#define LDS_DARK_OK_BG        lv_color_hex(0x192819)   /* 565: 0x1943 */
#define LDS_DARK_WARN_BG      lv_color_hex(0x292419)   /* 565: 0x2923 */
#define LDS_DARK_DANGER_BG    lv_color_hex(0x291819)   /* 565: 0x28C3 */
#define LDS_DARK_VIOLET_BG    lv_color_hex(0x211c31)   /* 565: 0x20E6 */
#define LDS_DARK_INV_BG       lv_color_hex(0xf7f3ef)   /* 565: 0xF79D */
#define LDS_DARK_INV_FG       lv_color_hex(0x101010)   /* 565: 0x1082 */

// ---- light ----
#define LDS_LIGHT_BG           lv_color_hex(0xfffbf7)   /* 565: 0xFFDE */
#define LDS_LIGHT_CARD         lv_color_hex(0xe6e3de)   /* 565: 0xE71B */
#define LDS_LIGHT_RAISED       lv_color_hex(0xd6d2ce)   /* 565: 0xD699 */
#define LDS_LIGHT_FIELD        lv_color_hex(0xffffff)   /* 565: 0xFFFF */
#define LDS_LIGHT_LINE         lv_color_hex(0xc5c6bd)   /* 565: 0xC637 */
#define LDS_LIGHT_SEG_OFF      lv_color_hex(0xc5c6bd)   /* 565: 0xC637 */
#define LDS_LIGHT_FG           lv_color_hex(0x101010)   /* 565: 0x1082 */
#define LDS_LIGHT_MUTED        lv_color_hex(0x52514a)   /* 565: 0x5289 */
#define LDS_LIGHT_FAINT        lv_color_hex(0x635d5a)   /* 565: 0x62EB */
#define LDS_LIGHT_ACCENT       lv_color_hex(0xe66108)   /* 565: 0xE301 */
#define LDS_LIGHT_ACCENT_INK   lv_color_hex(0xa53d08)   /* 565: 0xA1E1 */
#define LDS_LIGHT_ON_ACCENT    lv_color_hex(0xffffff)   /* 565: 0xFFFF */
#define LDS_LIGHT_INFO         lv_color_hex(0x21618c)   /* 565: 0x2311 */
#define LDS_LIGHT_OK           lv_color_hex(0x296121)   /* 565: 0x2B04 */
#define LDS_LIGHT_WARN         lv_color_hex(0x6b4d00)   /* 565: 0x6A60 */
#define LDS_LIGHT_DANGER       lv_color_hex(0xa53131)   /* 565: 0xA186 */
#define LDS_LIGHT_VIOLET       lv_color_hex(0x6341a5)   /* 565: 0x6214 */
#define LDS_LIGHT_INFO_BG      lv_color_hex(0xcedbde)   /* 565: 0xCEDB */
#define LDS_LIGHT_OK_BG        lv_color_hex(0xcedbc5)   /* 565: 0xCED8 */
#define LDS_LIGHT_WARN_BG      lv_color_hex(0xded7bd)   /* 565: 0xDEB7 */
#define LDS_LIGHT_DANGER_BG    lv_color_hex(0xe6d2c5)   /* 565: 0xE698 */
#define LDS_LIGHT_VIOLET_BG    lv_color_hex(0xd6d2de)   /* 565: 0xD69B */
#define LDS_LIGHT_INV_BG       lv_color_hex(0x101010)   /* 565: 0x1082 */
#define LDS_LIGHT_INV_FG       lv_color_hex(0xfffbf7)   /* 565: 0xFFDE */

// ---- typografi (px) — 2XL og HERO: kun cifre , ° − ----
#define LDS_FS_XS     16
#define LDS_FS_SM     20
#define LDS_FS_MD     24
#define LDS_FS_LG     32
#define LDS_FS_XL     48
#define LDS_FS_2XL    72
#define LDS_FS_HERO   144
#define LDS_FS_D28    28
#define LDS_FS_D36    36
#define LDS_FS_D44    44
#define LDS_FS_D168   168

// ---- night (dæmpet, uden for paletten) ----
#define LDS_NIGHT_BG      lv_color_hex(0x000000)
#define LDS_NIGHT_CLOCK   lv_color_hex(0x8c8a84)
#define LDS_NIGHT_MUTED   lv_color_hex(0x5a5853)
#define LDS_NIGHT_CALL    lv_color_hex(0x8a3f1c)
#define LDS_NIGHT_FAULT   lv_color_hex(0x8a4a47)
#define LDS_NIGHT_IDLE    lv_color_hex(0x2d2c27)

// ---- mål (px) ----
#define LDS_STATUSBAR    64
#define LDS_PAD          16
#define LDS_GAP          12
#define LDS_HOUSE_COL    296
#define LDS_HIT          64
#define LDS_HIT_LG       96
#define LDS_PILL_H       64
#define LDS_R_CARD       20
#define LDS_R_TILE       14
#define LDS_R_TILE_LG    16
#define LDS_R_MSG        12
#define LDS_SEG_W        8
#define LDS_SEG_GAP      3

// ---- tider ----
#define LDS_RETURN_HOME_S  60
#define LDS_DIM_S          120
#define LDS_REFRESH_S      5
