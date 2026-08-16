/* /bin/term — framebuffer terminal with a real shell behind it.
 *
 * A user-space pseudo-terminal:
 *   fork()  -> child: dup stdin/stdout/stderr onto two pipes, exec /bin/sh
 *   parent: reads the shell pipe and renders it as 8x8 text on the fb,
 *           reads /dev/kbd and feeds keystrokes into the shell pipe.
 *
 * So commands typed in the framebuffer window are executed by the real qvos
 * shell.  Everything is userspace: no kernel console changes were needed.
 */
#include "libc.h"
#include "font8x8.h"

#define KEY_BACKSPACE  14
#define KEY_TAB        15
#define KEY_ENTER      28
#define KEY_LEFTSHIFT  42
#define KEY_RIGHTSHIFT 54
#define KEY_SPACE      57
#define KEY_MINUS      12
#define KEY_EQUAL      13
#define KEY_LEFTBRACE  26
#define KEY_RIGHTBRACE 27
#define KEY_SEMICOLON  39
#define KEY_APOSTROPHE 40
#define KEY_GRAVE      41
#define KEY_BACKSLASH  43
#define KEY_COMMA      51
#define KEY_DOT        52
#define KEY_SLASH      53

#define MAX_COLS 160
#define MAX_ROWS 100

static volatile uint32_t *fb;
static uint32_t W, H;
static int COLS, ROWS;
static char cells[MAX_ROWS * MAX_COLS];
static int cx, cy;              /* cursor column/row */
static int shift_down;

static uint32_t rgb(uint32_t r, uint32_t g, uint32_t b)
{
    return (r << 16) | (g << 8) | b;
}

static char *cell_at(int r, int c)
{
    return &cells[r * MAX_COLS + c];
}

static void term_clear(void)
{
    for (int i = 0; i < MAX_ROWS * MAX_COLS; i++)
        cells[i] = ' ';
    cx = cy = 0;
}

static void term_scroll(void)
{
    for (int r = 1; r < ROWS; r++)
        for (int c = 0; c < COLS; c++)
            *cell_at(r - 1, c) = *cell_at(r, c);
    for (int c = 0; c < COLS; c++)
        *cell_at(ROWS - 1, c) = ' ';
}

/* Terminal output processing (also mirrors printable chars to the UART). */
static void term_putc(char ch)
{
    if (ch == '\r')
        return;
    if (ch == '\n') {
        cx = 0;
        cy++;
        if (cy >= ROWS) {
            term_scroll();
            cy = ROWS - 1;
        }
        write(1, "\r\n", 2);    /* mirror newlines on the UART */
        return;
    }
    if (ch == '\b') {
        if (cx > 0)
            cx--;
        else if (cy > 0) {
            cy--;
            cx = COLS - 1;
        }
        *cell_at(cy, cx) = ' ';
        return;
    }
    if (ch == '\t') {
        do {
            *cell_at(cy, cx) = ' ';
            cx++;
        } while (cx < COLS && (cx & 7) != 0);
        return;
    }
    if (ch < 0x20 || ch > 0x7e)
        ch = '?';
    *cell_at(cy, cx) = ch;
    write(1, &ch, 1);           /* mirror on the serial console */
    cx++;
    if (cx >= COLS) {
        cx = 0;
        cy++;
        if (cy >= ROWS) {
            term_scroll();
            cy = ROWS - 1;
        }
    }
}

static void term_puts(const char *s)
{
    while (*s)
        term_putc(*s++);
}

static void draw_glyph(int r, int c, char ch, int cursor)
{
    int glyph_index = (ch < 0x20 || ch > 0x7e) ? 0 : ch - 0x20;
    const unsigned char *glyph = font8x8[glyph_index];
    uint32_t bg = cursor ? rgb(0x00, 0xff, 0xd0) : rgb(0x10, 0x18, 0x20);
    uint32_t fg = cursor ? rgb(0x00, 0x00, 0x00) : rgb(0xd0, 0xd8, 0xe0);

    for (int yy = 0; yy < 8; yy++) {
        for (int xx = 0; xx < 8; xx++) {
            int on = (glyph[yy] & (1 << xx)) != 0;
            uint32_t px = on ? fg : bg;
            uint32_t x = (uint32_t)(c * 8 + xx);
            uint32_t y = (uint32_t)(r * 8 + yy);
            fb[(uint64_t)y * W + x] = px;
        }
    }
}

static void term_draw(void)
{
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            fb[(uint64_t)y * W + x] = rgb(0x10, 0x18, 0x20);

    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            char ch = *cell_at(r, c);
            if (ch == ' ' && !(r == cy && c == cx))
                continue;
            draw_glyph(r, c, ch, (r == cy && c == cx));
        }
    }
    fbflush();
}

static char map_key(uint16_t code)
{
    static const char letters[128] = {
        [30]='a', [48]='b', [46]='c', [32]='d', [18]='e', [33]='f',
        [34]='g', [35]='h', [23]='i', [36]='j', [37]='k', [38]='l',
        [50]='m', [49]='n', [24]='o', [25]='p', [16]='q', [19]='r',
        [31]='s', [20]='t', [22]='u', [47]='v', [17]='w', [45]='x',
        [21]='y', [44]='z',
    };

    if (code < 128 && letters[code])
        return (char)(letters[code] + (shift_down ? 'A' - 'a' : 0));
    if (code >= 2 && code <= 11) {
        static const char unshifted[] = "1234567890";
        static const char shifted[] = "!@#$%^&*()";
        return shift_down ? shifted[code - 2] : unshifted[code - 2];
    }
    switch (code) {
    case KEY_SPACE:     return ' ';
    case KEY_ENTER:     return '\n';
    case KEY_TAB:       return '\t';
    case KEY_BACKSPACE: return '\b';
    case KEY_MINUS:     return shift_down ? '_' : '-';
    case KEY_EQUAL:     return shift_down ? '+' : '=';
    case KEY_LEFTBRACE: return shift_down ? '{' : '[';
    case KEY_RIGHTBRACE:return shift_down ? '}' : ']';
    case KEY_SEMICOLON: return shift_down ? ':' : ';';
    case KEY_APOSTROPHE:return shift_down ? '"' : '\'';
    case KEY_GRAVE:     return shift_down ? '~' : '`';
    case KEY_BACKSLASH: return shift_down ? '|' : '\\';
    case KEY_COMMA:     return shift_down ? '<' : ',';
    case KEY_DOT:       return shift_down ? '>' : '.';
    case KEY_SLASH:     return shift_down ? '?' : '/';
    default:            return 0;
    }
}

/* fork + pipe + dup + exec /bin/sh; returns child pid or -1. */
static int spawn_shell(int *sh_in, int *sh_out)
{
    int to_sh[2], from_sh[2];
    if (pipe(to_sh) != 0 || pipe(from_sh) != 0)
        return -1;

    int pid = fork();
    if (pid < 0)
        return -1;

    if (pid == 0) {
        /* child: stdin <- keyboard pipe, stdout/stderr -> display pipe */
        close(to_sh[1]);
        close(from_sh[0]);
        close(0);
        close(1);
        close(2);
        int in = dup(to_sh[0]);
        int out = dup(from_sh[1]);
        int err = dup(from_sh[1]);
        close(to_sh[0]);
        close(from_sh[1]);
        if (in != 0 || out != 1 || err != 2)
            exit(127);
        char *args[] = { "/bin/sh", 0 };
        execve("/bin/sh", args, 0);
        exit(127);
    }

    /* parent keeps write end of keyboard pipe + read end of display pipe */
    close(to_sh[0]);
    close(from_sh[1]);
    *sh_in = to_sh[1];
    *sh_out = from_sh[0];
    return pid;
}

int main(void)
{
    struct qvos_fb_info info;
    if (fbinfo(&info) != 0 || info.width < 8 || info.height < 8) {
        printf("[term] no framebuffer\n");
        return 1;
    }
    fb = (volatile uint32_t *)fbmap();
    if (!fb) {
        printf("[term] fbmap failed\n");
        return 1;
    }
    W = info.width;
    H = info.height;
    COLS = (int)(W / 8);
    ROWS = (int)(H / 8);
    if (COLS > MAX_COLS) COLS = MAX_COLS;
    if (ROWS > MAX_ROWS) ROWS = MAX_ROWS;

    int kbd = open("/dev/kbd", 0);
    if (kbd < 0) {
        printf("[term] /dev/kbd unavailable (virtio-keyboard missing?)\n");
        return 1;
    }

    term_clear();
    term_draw();

    int sh_in = -1, sh_out = -1;
    int shell_pid = spawn_shell(&sh_in, &sh_out);
    if (shell_pid < 0) {
        printf("[term] shell spawn failed, standalone echo mode\n");
        term_puts("qvos framebuffer terminal (no shell)\n");
    } else {
        printf("[term] shell pid=%d attached; type commands in the window\n",
               shell_pid);
    }

    struct qvos_input_event evs[16];
    char outbuf[512];
    char pending = 0;

    for (;;) {
        int dirty = 0;

        /* Shell stdout/stderr -> terminal grid. */
        if (sh_out >= 0) {
            int n = read(sh_out, outbuf, sizeof(outbuf));
            if (n > 0) {
                for (int i = 0; i < n; i++)
                    term_putc(outbuf[i]);
                dirty = 1;
            }
        }

        /* Keyboard -> shell stdin (or local echo if no shell). */
        int n = read(kbd, evs, sizeof(evs));
        if (n > 0) {
            int cnt = n / (int)sizeof(evs[0]);
            for (int i = 0; i < cnt; i++) {
                if (evs[i].type != 1)      /* EV_KEY only */
                    continue;
                uint16_t code = evs[i].code;
                uint32_t val = evs[i].value;
                if (code == KEY_LEFTSHIFT || code == KEY_RIGHTSHIFT) {
                    shift_down = val != 0;
                    continue;
                }
                if (val == 1) {
                    char ch = map_key(code);
                    if (!ch)
                        continue;
                    if (sh_in >= 0)
                        pending = ch;       /* one pending byte max */
                    else {
                        term_putc(ch);
                        dirty = 1;
                    }
                }
            }
        }

        if (sh_in >= 0 && pending) {
            int w = write(sh_in, &pending, 1);
            if (w == 1) {
                pending = 0;
            }
        }

        if (dirty)
            term_draw();
        nanosleep();
    }
}
