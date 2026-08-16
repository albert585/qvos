/* Minimal LVGL v9 configuration for qvos.
 *
 * The qvos framebuffer is 32bpp B8G8R8X8, which matches LV_COLOR_FORMAT_XRGB8888
 * in little-endian memory order.  LVGL runs with no OS: lvdemo drives the tick
 * counter manually and calls lv_timer_handler() between coarse sleeps.
 */
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH      32
#define LV_COLOR_16_SWAP    0

/* qvos has no C standard library at runtime: use LVGL's built-in shims. */
#define LV_USE_STDLIB_MALLOC    LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_STRING    LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_SPRINTF   LV_STDLIB_BUILTIN
#define LV_MEM_SIZE             (256U * 1024U)

#define LV_USE_OS   LV_OS_NONE

#define LV_USE_LOG              0
#define LV_USE_ASSERT_NULL      0
#define LV_USE_ASSERT_MALLOC    0
#define LV_USE_ASSERT_STYLE     0
#define LV_USE_ASSERT_MEM_INTEGRITY 0
#define LV_USE_ASSERT_OBJ       0

/* Software renderer only; everything else is off by default in the template. */
#define LV_USE_DRAW_SW  1

#define LV_USE_THEME_DEFAULT   1
#define LV_USE_THEME_SIMPLE    0
#define LV_USE_THEME_MONO      0

#define LV_FONT_MONTSERRAT_14  1
#define LV_FONT_DEFAULT        &lv_font_montserrat_14

#endif /* LV_CONF_H */
