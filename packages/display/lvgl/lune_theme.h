// Genereret af lune-design-system tools/lds_display.py fra tokens/tokens.json — redigér ikke.
// Lune Design System · vægskærm 1024×600 · RGB565
#pragma once
#include "lvgl.h"

// ---- dark ----
#define LDS_DARK_BG            lv_color_hex(0x101010)   /* 565: 0x1082 */
#define LDS_DARK_CARD          lv_color_hex(0x212021)   /* 565: 0x2104 */
#define LDS_DARK_RAISED        lv_color_hex(0x292829)   /* 565: 0x2945 */
#define LDS_DARK_FIELD         lv_color_hex(0x191819)   /* 565: 0x18C3 */
#define LDS_DARK_LINE          lv_color_hex(0x313131)   /* 565: 0x3186 */
#define LDS_DARK_SEG_OFF       lv_color_hex(0x313131)   /* 565: 0x3186 */
#define LDS_DARK_FG            lv_color_hex(0xf7f3f7)   /* 565: 0xF79E */
#define LDS_DARK_MUTED         lv_color_hex(0xa5a6ad)   /* 565: 0xA535 */
#define LDS_DARK_FAINT         lv_color_hex(0x8c8e94)   /* 565: 0x8C72 */
#define LDS_DARK_ACCENT        lv_color_hex(0xff8a3a)   /* 565: 0xFC47 */
#define LDS_DARK_ACCENT_INK    lv_color_hex(0xff9a5a)   /* 565: 0xFCCB */
#define LDS_DARK_ON_ACCENT     lv_color_hex(0x101010)   /* 565: 0x1082 */
#define LDS_DARK_INFO          lv_color_hex(0x6bb2ff)   /* 565: 0x6D9F */
#define LDS_DARK_OK            lv_color_hex(0x63ce7b)   /* 565: 0x666F */
#define LDS_DARK_WARN          lv_color_hex(0xefca4a)   /* 565: 0xEE49 */
#define LDS_DARK_DANGER        lv_color_hex(0xff7973)   /* 565: 0xFBCE */
#define LDS_DARK_VIOLET        lv_color_hex(0xb59eff)   /* 565: 0xB4FF */
#define LDS_DARK_INFO_BG       lv_color_hex(0x102431)   /* 565: 0x1126 */
#define LDS_DARK_OK_BG         lv_color_hex(0x102819)   /* 565: 0x1143 */
#define LDS_DARK_WARN_BG       lv_color_hex(0x292010)   /* 565: 0x2902 */
#define LDS_DARK_DANGER_BG     lv_color_hex(0x311c19)   /* 565: 0x30E3 */
#define LDS_DARK_VIOLET_BG     lv_color_hex(0x212031)   /* 565: 0x2106 */
#define LDS_DARK_INV_BG        lv_color_hex(0xf7f3f7)   /* 565: 0xF79E */
#define LDS_DARK_INV_FG        lv_color_hex(0x101010)   /* 565: 0x1082 */

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

// ---- typografi (px) — 2XL, HERO: kun cifre , ° − ----
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
#define LDS_NIGHT_CLOCK   lv_color_hex(0x8a8a8a)
#define LDS_NIGHT_MUTED   lv_color_hex(0x585858)
#define LDS_NIGHT_CALL    lv_color_hex(0x8a3f1c)
#define LDS_NIGHT_FAULT   lv_color_hex(0x8a4a47)
#define LDS_NIGHT_IDLE    lv_color_hex(0x2c2c2c)

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
