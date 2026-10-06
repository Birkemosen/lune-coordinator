// Genereret af lune-design-system tools/lds_display.py fra tokens/tokens.json — redigér ikke.
// Lune Design System · vægskærm 1024×600 · RGB565
#pragma once
#include "lvgl.h"

// ---- dark ----
#define LDS_DARK_BG            lv_color_hex(0x101010)   /* 565: 0x1082 */
#define LDS_DARK_CARD          lv_color_hex(0x191819)   /* 565: 0x18C3 */
#define LDS_DARK_RAISED        lv_color_hex(0x212429)   /* 565: 0x2125 */
#define LDS_DARK_FIELD         lv_color_hex(0x101010)   /* 565: 0x1082 */
#define LDS_DARK_LINE          lv_color_hex(0x292d31)   /* 565: 0x2966 */
#define LDS_DARK_SEG_OFF       lv_color_hex(0x3a393a)   /* 565: 0x39C7 */
#define LDS_DARK_FG            lv_color_hex(0xeff3f7)   /* 565: 0xEF9E */
#define LDS_DARK_MUTED         lv_color_hex(0xa5a6ad)   /* 565: 0xA535 */
#define LDS_DARK_FAINT         lv_color_hex(0x8c8e94)   /* 565: 0x8C72 */
#define LDS_DARK_ACCENT        lv_color_hex(0xf77500)   /* 565: 0xF3A0 */
#define LDS_DARK_ACCENT_INK    lv_color_hex(0xff9a4a)   /* 565: 0xFCC9 */
#define LDS_DARK_ON_ACCENT     lv_color_hex(0x101010)   /* 565: 0x1082 */
#define LDS_DARK_INFO          lv_color_hex(0x63b2ef)   /* 565: 0x659D */
#define LDS_DARK_OK            lv_color_hex(0x73ca6b)   /* 565: 0x764D */
#define LDS_DARK_WARN          lv_color_hex(0xffb242)   /* 565: 0xFD88 */
#define LDS_DARK_DANGER        lv_color_hex(0xff6163)   /* 565: 0xFB0C */
#define LDS_DARK_VIOLET        lv_color_hex(0xb596f7)   /* 565: 0xB4BE */
#define LDS_DARK_INFO_BG       lv_color_hex(0x082842)   /* 565: 0x0948 */
#define LDS_DARK_OK_BG         lv_color_hex(0x102d10)   /* 565: 0x1162 */
#define LDS_DARK_WARN_BG       lv_color_hex(0x312000)   /* 565: 0x3100 */
#define LDS_DARK_DANGER_BG     lv_color_hex(0x3a1819)   /* 565: 0x38C3 */
#define LDS_DARK_VIOLET_BG     lv_color_hex(0x291c42)   /* 565: 0x28E8 */
#define LDS_DARK_INV_BG        lv_color_hex(0xeff3f7)   /* 565: 0xEF9E */
#define LDS_DARK_INV_FG        lv_color_hex(0x101010)   /* 565: 0x1082 */
#define LDS_DARK_HEAT_FILL     lv_color_hex(0xe66108)   /* 565: 0xE301 */
#define LDS_DARK_WATER_FILL    lv_color_hex(0x007973)   /* 565: 0x03CE */
#define LDS_DARK_WATER         lv_color_hex(0x4ac2bd)   /* 565: 0x4E17 */
#define LDS_DARK_ON_FILL       lv_color_hex(0xffffff)   /* 565: 0xFFFF */
#define LDS_DARK_ON_WARN_FILL  lv_color_hex(0x101010)   /* 565: 0x1082 */
#define LDS_DARK_DANGER_FILL   lv_color_hex(0xb5393a)   /* 565: 0xB1C7 */
#define LDS_DARK_WARN_FILL     lv_color_hex(0xffaa29)   /* 565: 0xFD45 */
#define LDS_DARK_VIOLET_FILL   lv_color_hex(0x7351b5)   /* 565: 0x7296 */
#define LDS_DARK_OK_FILL       lv_color_hex(0x318229)   /* 565: 0x3405 */
#define LDS_DARK_INFO_FILL     lv_color_hex(0x31719c)   /* 565: 0x3393 */
#define LDS_DARK_DEV_1         lv_color_hex(0x318ece)   /* 565: 0x3479 */
#define LDS_DARK_ON_DEV_1      lv_color_hex(0x101010)   /* 565: 0x1082 */
#define LDS_DARK_DEV_2         lv_color_hex(0x73a2ce)   /* 565: 0x7519 */
#define LDS_DARK_DEV_3         lv_color_hex(0x42494a)   /* 565: 0x4249 */
#define LDS_DARK_DEV_4         lv_color_hex(0xf7aa63)   /* 565: 0xF54C */
#define LDS_DARK_DEV_5         lv_color_hex(0xf77500)   /* 565: 0xF3A0 */

// ---- light ----
#define LDS_LIGHT_BG           lv_color_hex(0xf7f7f7)   /* 565: 0xF7BE */
#define LDS_LIGHT_CARD         lv_color_hex(0xdedfde)   /* 565: 0xDEFB */
#define LDS_LIGHT_RAISED       lv_color_hex(0xd6d7d6)   /* 565: 0xD6BA */
#define LDS_LIGHT_FIELD        lv_color_hex(0xffffff)   /* 565: 0xFFFF */
#define LDS_LIGHT_LINE         lv_color_hex(0xbdbebd)   /* 565: 0xBDF7 */
#define LDS_LIGHT_SEG_OFF      lv_color_hex(0xb5b6b5)   /* 565: 0xB5B6 */
#define LDS_LIGHT_FG           lv_color_hex(0x191819)   /* 565: 0x18C3 */
#define LDS_LIGHT_MUTED        lv_color_hex(0x525152)   /* 565: 0x528A */
#define LDS_LIGHT_FAINT        lv_color_hex(0x636163)   /* 565: 0x630C */
#define LDS_LIGHT_ACCENT       lv_color_hex(0xe66108)   /* 565: 0xE301 */
#define LDS_LIGHT_ACCENT_INK   lv_color_hex(0xa54108)   /* 565: 0xA201 */
#define LDS_LIGHT_ON_ACCENT    lv_color_hex(0xffffff)   /* 565: 0xFFFF */
#define LDS_LIGHT_INFO         lv_color_hex(0x215d8c)   /* 565: 0x22F1 */
#define LDS_LIGHT_OK           lv_color_hex(0x296121)   /* 565: 0x2B04 */
#define LDS_LIGHT_WARN         lv_color_hex(0x6b4d00)   /* 565: 0x6A60 */
#define LDS_LIGHT_DANGER       lv_color_hex(0xa53131)   /* 565: 0xA186 */
#define LDS_LIGHT_VIOLET       lv_color_hex(0x6341a5)   /* 565: 0x6214 */
#define LDS_LIGHT_INFO_BG      lv_color_hex(0xc5dfef)   /* 565: 0xC6FD */
#define LDS_LIGHT_OK_BG        lv_color_hex(0xcee3c5)   /* 565: 0xCF18 */
#define LDS_LIGHT_WARN_BG      lv_color_hex(0xefd7b5)   /* 565: 0xEEB6 */
#define LDS_LIGHT_DANGER_BG    lv_color_hex(0xf7d2c5)   /* 565: 0xF698 */
#define LDS_LIGHT_VIOLET_BG    lv_color_hex(0xded7ef)   /* 565: 0xDEBD */
#define LDS_LIGHT_INV_BG       lv_color_hex(0x191819)   /* 565: 0x18C3 */
#define LDS_LIGHT_INV_FG       lv_color_hex(0xf7f7f7)   /* 565: 0xF7BE */
#define LDS_LIGHT_HEAT_FILL    lv_color_hex(0xe66108)   /* 565: 0xE301 */
#define LDS_LIGHT_WATER_FILL   lv_color_hex(0x007973)   /* 565: 0x03CE */
#define LDS_LIGHT_WATER        lv_color_hex(0x007973)   /* 565: 0x03CE */
#define LDS_LIGHT_ON_FILL      lv_color_hex(0xffffff)   /* 565: 0xFFFF */
#define LDS_LIGHT_ON_WARN_FILL lv_color_hex(0x101010)   /* 565: 0x1082 */
#define LDS_LIGHT_DANGER_FILL  lv_color_hex(0xb5393a)   /* 565: 0xB1C7 */
#define LDS_LIGHT_WARN_FILL    lv_color_hex(0xffaa29)   /* 565: 0xFD45 */
#define LDS_LIGHT_VIOLET_FILL  lv_color_hex(0x7351b5)   /* 565: 0x7296 */
#define LDS_LIGHT_OK_FILL      lv_color_hex(0x318229)   /* 565: 0x3405 */
#define LDS_LIGHT_INFO_FILL    lv_color_hex(0x31719c)   /* 565: 0x3393 */
#define LDS_LIGHT_DEV_1        lv_color_hex(0x196da5)   /* 565: 0x1B74 */
#define LDS_LIGHT_ON_DEV_1     lv_color_hex(0xffffff)   /* 565: 0xFFFF */
#define LDS_LIGHT_DEV_2        lv_color_hex(0x7ba2c5)   /* 565: 0x7D18 */
#define LDS_LIGHT_DEV_3        lv_color_hex(0xcecece)   /* 565: 0xCE79 */
#define LDS_LIGHT_DEV_4        lv_color_hex(0xde9a73)   /* 565: 0xDCCE */
#define LDS_LIGHT_DEV_5        lv_color_hex(0xde5500)   /* 565: 0xDAA0 */

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
