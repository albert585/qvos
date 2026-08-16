/* /bin/lvdemo — LVGL v9 running on the qvos userspace framebuffer.
 *
 * The display driver copies LVGL's XRGB8888 draw buffer straight into the
 * shared virtio-gpu backing store and flushes once per frame.  qvos has no
 * clock syscall yet, so the LVGL tick is advanced manually each frame.
 */
#include <lvgl.h>
#include "libc.h"

static volatile uint32_t *fb;
static uint32_t W, H;
static uint32_t tick_ms;

static int mouse_fd = -1;
static int mouse_x, mouse_y, mouse_pressed;
static int quit;                   /* set by the button's LV_EVENT_CLICKED */

#define EV_SYN    0
#define EV_KEY    1
#define EV_ABS    3
#define ABS_X     0x00
#define ABS_Y     0x01
#define BTN_LEFT  0x110

static uint32_t lv_tick(void)
{
    return tick_ms;
}

static void mouse_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    struct qvos_input_event evs[16];
    int n = (mouse_fd >= 0)
            ? (int)read(mouse_fd, evs, sizeof(evs)) / (int)sizeof(evs[0]) : 0;

    for (int i = 0; i < n; i++) {
        if (evs[i].type == EV_ABS && evs[i].code == ABS_X)
            /* QEMU reports ABS axes as 0..32767; map to pixels. */
            mouse_x = (int)((uint64_t)evs[i].value * W / 32767U);
        else if (evs[i].type == EV_ABS && evs[i].code == ABS_Y)
            mouse_y = (int)((uint64_t)evs[i].value * H / 32767U);
        else if (evs[i].type == EV_KEY && evs[i].code == BTN_LEFT)
            mouse_pressed = evs[i].value != 0;
    }
    data->point.x = mouse_x;
    data->point.y = mouse_y;
    data->state = mouse_pressed ? LV_INDEV_STATE_PRESSED
                                : LV_INDEV_STATE_RELEASED;
}

static void flush_cb(lv_display_t *disp, const lv_area_t *area,
                     uint8_t *px_map)
{
    uint32_t w = lv_area_get_width(area);
    uint32_t h = lv_area_get_height(area);
    uint32_t *src = (uint32_t *)px_map;

    for (uint32_t y = 0; y < h; y++) {
        for (uint32_t x = 0; x < w; x++) {
            uint32_t px = src[y * w + x];
            fb[(uint64_t)(area->y1 + y) * W + (area->x1 + x)] = px;
        }
    }
    fbflush();                 /* guest memory -> virtio-gpu -> scanout */
    lv_display_flush_ready(disp);
}

static void btn_clicked_cb(lv_event_t *e)
{
    (void)e;
    quit = 1;
}

static void build_ui(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x101820), 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "qvos + LVGL v9.4");
    lv_obj_set_style_text_color(title, lv_color_hex(0x00ffd0), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 24);

    lv_obj_t *sub = lv_label_create(scr);
    lv_label_set_text(sub, "AI port: userspace framebuffer");
    lv_obj_set_style_text_color(sub, lv_color_hex(0x8098a8), 0);
    lv_obj_align(sub, LV_ALIGN_TOP_MID, 0, 56);

    lv_obj_t *arc = lv_arc_create(scr);
    lv_obj_set_size(arc, 180, 180);
    lv_arc_set_value(arc, 35);
    lv_obj_align(arc, LV_ALIGN_CENTER, 0, -40);

    lv_obj_t *btn = lv_button_create(scr);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -40);
    lv_obj_add_event_cb(btn, btn_clicked_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bl = lv_label_create(btn);
    lv_label_set_text(bl, "OK");

    lv_obj_t *slider = lv_slider_create(scr);
    lv_obj_set_size(slider, 240, 12);
    lv_slider_set_value(slider, 50, LV_ANIM_OFF);
    lv_obj_align(slider, LV_ALIGN_BOTTOM_MID, 0, -96);

    /* Animation loop drives the arc/slider; tick is a manual counter. */
    for (uint32_t f = 0; f < 240; f++) {
        tick_ms += 16;
        lv_arc_set_value(arc, (int32_t)(f % 101));
        lv_slider_set_value(slider, (int32_t)((f * 3) % 101), LV_ANIM_ON);
        lv_timer_handler();
        nanosleep();          /* coarse ~10 ms sleep */
    }

    /* Keep the GUI alive until the mouse clicks the OK button. */
    while (!quit) {
        tick_ms += 16;
        lv_timer_handler();
        nanosleep();
    }
}

int main(void)
{
    struct qvos_fb_info info;

    if (fbinfo(&info) != 0) {
        printf("[lvdemo] no framebuffer\n");
        return 1;
    }
    fb = (volatile uint32_t *)fbmap();
    if (!fb) {
        printf("[lvdemo] fbmap failed\n");
        return 1;
    }
    W = info.width;
    H = info.height;
    printf("[lvdemo] fb %ux%u, initializing LVGL\n", W, H);

    lv_init();
    lv_tick_set_cb(lv_tick);

    lv_display_t *disp = lv_display_create((int32_t)W, (int32_t)H);
    if (!disp) {
        printf("[lvdemo] lv_display_create failed\n");
        return 1;
    }
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_NATIVE);
    lv_display_set_flush_cb(disp, flush_cb);

    /* One full-screen draw buffer (XRGB8888 = 4 bytes/pixel). */
    size_t buf_bytes = (size_t)W * H * 4;
    void *buf1 = malloc(buf_bytes);
    if (!buf1) {
        printf("[lvdemo] draw buffer malloc failed (%u bytes)\n",
               (unsigned)buf_bytes);
        return 1;
    }
    lv_display_set_buffers(disp, buf1, NULL, (uint32_t)buf_bytes,
                           LV_DISPLAY_RENDER_MODE_FULL);

    /* virtio tablet/mouse events arrive through /dev/mouse. */
    mouse_fd = open("/dev/mouse", 0);
    if (mouse_fd >= 0) {
        lv_indev_t *indev = lv_indev_create();
        lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_display(indev, disp);
        lv_indev_set_read_cb(indev, mouse_read_cb);

        /* Visible cursor: a small cyan square with a white border. */
        lv_obj_t *cursor = lv_obj_create(lv_screen_active());
        lv_obj_set_size(cursor, 14, 14);
        lv_obj_set_style_bg_color(cursor, lv_color_hex(0x00ffd0), 0);
        lv_obj_set_style_bg_opa(cursor, LV_OPA_70, 0);
        lv_obj_set_style_border_color(cursor, lv_color_hex(0xffffff), 0);
        lv_obj_set_style_border_width(cursor, 2, 0);
        lv_obj_set_style_radius(cursor, 3, 0);
        lv_indev_set_cursor(indev, cursor);

        printf("[lvdemo] /dev/mouse attached to LVGL pointer indev\n");
    } else {
        printf("[lvdemo] /dev/mouse unavailable (no virtio-input)\n");
    }

    build_ui();
    printf("[lvdemo] UI running; click the OK button (mouse) to exit\n");
    return 0;
}
