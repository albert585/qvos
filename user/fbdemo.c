/* /bin/fbdemo — userspace framebuffer demo.
 *
 * Maps the virtio-gpu backing store with SYS_fbmap, then draws straight
 * into the shared 32bpp pixels from U-mode.  A SYS_fbflush after each
 * frame makes the rectangle visible on the scanout.
 */
#include "libc.h"

#define FRAMES 48

static volatile uint32_t *fb;
static uint32_t W, H;

static uint32_t rgb(uint32_t r, uint32_t g, uint32_t b)
{
    return (r << 16) | (g << 8) | b;
}

static void px(uint32_t x, uint32_t y, uint32_t c)
{
    if (x < W && y < H)
        fb[(uint64_t)y * W + x] = c;
}

static void fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t c)
{
    for (uint32_t yy = 0; yy < h; yy++)
        for (uint32_t xx = 0; xx < w; xx++)
            px(x + xx, y + yy, c);
}

static void disc(int cx, int cy, int r, uint32_t c)
{
    for (int dy = -r; dy <= r; dy++)
        for (int dx = -r; dx <= r; dx++)
            if (dx * dx + dy * dy <= r * r)
                px((uint32_t)(cx + dx), (uint32_t)(cy + dy), c);
}

static void draw_frame(uint32_t f)
{
    /* Vertical gradient background. */
    for (uint32_t y = 0; y < H; y++) {
        uint32_t c = rgb(0x10, 0x0a + (y * 0x30) / H,
                         0x20 + (y * 0x60) / H);
        for (uint32_t x = 0; x < W; x++)
            px(x, y, c);
    }

    /* RGBYCMW colour bars. */
    static const uint32_t bars[] = {
        0x00ff0000, 0x0000ff00, 0x000000ff, 0x00ffff00,
        0x0000ffff, 0x00ff00ff, 0x00ffffff,
    };
    uint32_t bw = W / 7;
    for (uint32_t i = 0; i < 7; i++)
        fill(i * bw, 0, bw, H / 16, bars[i]);

    /* Checkerboard strip. */
    for (uint32_t y = H - H / 8; y < H; y += 16)
        for (uint32_t x = 0; x < W; x += 16)
            if (((x / 16 + y / 16) & 1) == 0)
                fill(x, y, 16, 16, 0x00202020);

    /* Bouncing ball. */
    uint32_t span = (W > 200) ? W - 200 : W;
    int cx = (int)((f * 13) % span) + 100;
    int cy = (int)(H / 2 + (int)((f * 7) % (H / 4)) - H / 8);
    disc(cx, cy, 40, 0x00ff8000);
    disc(cx - 10, cy - 12, 10, 0x00ffffc0);
}

int main(void)
{
    struct qvos_fb_info info;

    if (fbinfo(&info) != 0) {
        printf("[fbdemo] no framebuffer: is virtio-gpu attached?\n");
        return 1;
    }

    fb = (volatile uint32_t *)fbmap();
    if (!fb) {
        printf("[fbdemo] fbmap failed\n");
        return 1;
    }
    W = info.width;
    H = info.height;
    printf("[fbdemo] fbmap=%x %ux%u %ubpp pitch=%u phys=%x\n",
           (unsigned long)fb, info.width, info.height,
           info.bpp, info.pitch, (unsigned long)info.phys);

    fbclear(0x00000000);
    const char *banner = "qvos /bin/fbdemo: userspace framebuffer\n";
    fbtext(banner, strlen(banner));

    for (uint32_t f = 0; f < FRAMES; f++) {
        draw_frame(f);
        fbflush();          /* memory writes are invisible until flush */
        nanosleep();        /* coarse ~tick sleep keeps the frame rate sane */
    }

    /* Leave a final still image. */
    fbclear(0x00101020);
    fill(0, 0, W, 16, 0x0000ff00);
    fill(0, H - 16, W, 16, 0x0000ff00);
    for (uint32_t i = 0; i < 8; i++) {
        uint32_t x = i * W / 8;
        fill(x, H / 2 - 90, W / 8 - 4, 180,
             rgb(40 + i * 24, 200 - i * 20, 90 + i * 16));
    }
    fbflush();
    const char *done = "fbdemo: done. type 'fb test' for the shell demo\n";
    fbtext(done, strlen(done));
    printf("[fbdemo] done\n");
    return 0;
}
