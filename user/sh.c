/* /bin/sh — small fallback shell for qvos. */
#include "libc.h"

#define MAX_ARGS 16
#define MAX_LINE 256

static void prompt(void)
{
    write(1, "qvos$ ", 6);
}

static int readline(char *buf, size_t max)
{
    size_t n = 0;
    for (;;) {
        char c;
        ssize_t r = read(0, &c, 1);
        if (r != 1)
            continue;
        if (c == '\r' || c == '\n') {
            buf[n] = 0;
            write(1, "\r\n", 2);
            return (int)n;
        }
        if ((c == 0x7f || c == 0x08) && n > 0) {
            n--;
            write(1, "\b \b", 3);
            continue;
        }
        if (n + 1 < max) {
            buf[n++] = c;
            write(1, &c, 1);
        }
    }
}

static int cmd_echo(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        write(1, argv[i], strlen(argv[i]));
        if (i + 1 < argc)
            write(1, " ", 1);
    }
    write(1, "\n", 1);
    return 0;
}

static int cmd_cat(int argc, char **argv)
{
    if (argc < 2) {
        printf("usage: cat <file>\n");
        return 1;
    }
    int fd = open(argv[1], 0);
    if (fd < 0) {
        printf("cat: %s: no such file\n", argv[1]);
        return 1;
    }
    char buf[256];
    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf))) > 0)
        write(1, buf, (size_t)n);
    close(fd);
    return 0;
}

static int cmd_ostest(void)
{
    int pid = fork();
    if (pid == 0) {
        char *args[] = { "/bin/ostest", 0 };
        execve("/bin/ostest", args, 0);
        exit(127);
    }
    int st = 0;
    waitpid(pid, &st, 0);
    return st;
}

static uint32_t parse_hex_color(const char *s)
{
    uint32_t v = 0;
    for (int i = 0; s[i]; i++) {
        char c = s[i];
        uint32_t d;
        if (c >= '0' && c <= '9') d = (uint32_t)(c - '0');
        else if (c >= 'a' && c <= 'f') d = (uint32_t)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') d = (uint32_t)(c - 'A' + 10);
        else return 0;
        v = (v << 4) | d;
        if (v > 0x00ffffffUL)
            return 0x00ffffffUL;
    }
    return v & 0x00ffffffUL;
}

static uint32_t parse_uint(const char *s)
{
    uint32_t v = 0;
    int base = 10;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        base = 16;
        s += 2;
    } else {
        for (int i = 0; s[i]; i++)
            if ((s[i] >= 'a' && s[i] <= 'f') ||
                (s[i] >= 'A' && s[i] <= 'F'))
                base = 16;          /* bare hex like deadbeef */
    }
    for (int i = 0; s[i]; i++) {
        uint32_t d;
        char c = s[i];
        if (c >= '0' && c <= '9') d = (uint32_t)(c - '0');
        else if (base == 16 && c >= 'a' && c <= 'f') d = (uint32_t)(c - 'a' + 10);
        else if (base == 16 && c >= 'A' && c <= 'F') d = (uint32_t)(c - 'A' + 10);
        else return 0;
        v = v * (uint32_t)base + d;
    }
    return v;
}

static void print_hex_bytes_nl(const unsigned char *b, size_t n, int nl)
{
    static const char d[] = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) {
        char s[3];
        s[0] = d[b[i] >> 4];
        s[1] = d[b[i] & 15];
        s[2] = ' ';
        write(1, s, 3);
    }
    if (nl)
        write(1, "\n", 1);
}

static void print_hex_bytes(const unsigned char *b, size_t n)
{
    print_hex_bytes_nl(b, n, 1);
}

static void print_hex_u32(uint32_t v)
{
    static const char d[] = "0123456789abcdef";
    char b[9];
    b[8] = 0;
    for (int i = 7; i >= 0; i--) {
        b[i] = d[v & 15];
        v >>= 4;
    }
    write(1, b, 8);
}

static int cmd_hd(int argc, char **argv)
{
    if (argc < 2) {
        printf("usage: hd <file> [offset] [length]\n");
        return 1;
    }
    int fd = open(argv[1], 0);
    if (fd < 0) {
        printf("hd: %s: no such file\n", argv[1]);
        return 1;
    }
    uint32_t off = (argc > 2) ? parse_uint(argv[2]) : 0;
    uint32_t len = (argc > 3) ? parse_uint(argv[3]) : 128;
    if (len == 0)
        len = 128;
    if (lseek(fd, off, 0) < 0) {
        printf("hd: lseek failed\n");
        close(fd);
        return 1;
    }

    unsigned char buf[16];
    uint32_t pos = off;
    while ((uint32_t)(pos - off) < len) {
        int n = read(fd, buf, sizeof(buf));
        if (n <= 0)
            break;
        print_hex_u32(pos);
        write(1, "  ", 2);
        print_hex_bytes_nl(buf, (size_t)n, 0);
        write(1, " |", 2);
        for (int i = 0; i < n; i++) {
            char c = (char)buf[i];
            char ch = (c >= 0x20 && c <= 0x7e) ? c : '.';
            write(1, &ch, 1);
        }
        write(1, "|\n", 2);
        pos += (uint32_t)n;
    }
    close(fd);
    return 0;
}

static int cmd_fb(int argc, char **argv)
{
    struct qvos_fb_info info;

    if (fbinfo(&info) != 0) {
        printf("fb: no framebuffer (virtio-gpu missing?)\n");
        return 1;
    }

    const char *sub = (argc > 1) ? argv[1] : "info";

    if (strcmp(sub, "info") == 0) {
        printf("fb: %ux%u %ubpp pitch=%u size=%u phys=%x\n",
               info.width, info.height, info.bpp, info.pitch,
               info.size_bytes, (unsigned long)info.phys);
        return 0;
    }

    if (strcmp(sub, "clear") == 0) {
        uint32_t bg = (argc > 2) ? parse_hex_color(argv[2]) : 0x00000000;
        if (fbclear(bg) != 0)
            printf("fb: clear failed\n");
        return 0;
    }

    if (strcmp(sub, "text") == 0) {
        if (argc < 3) {
            printf("usage: fb text <string>\n");
            return 1;
        }
        for (int i = 2; i < argc; i++) {
            fbtext(argv[i], strlen(argv[i]));
            if (i + 1 < argc)
                fbtext(" ", 1);
        }
        fbtext("\n", 1);
        return 0;
    }

    if (strcmp(sub, "dev") == 0) {
        int fd = open("/dev/fb0", 2);   /* O_RDWR */
        if (fd < 0) {
            printf("fb: open /dev/fb0 failed\n");
            return 1;
        }
        struct qvos_stat st;
        if (fstat(fd, &st) != 0) {
            printf("fb: fstat /dev/fb0 failed\n");
            close(fd);
            return 1;
        }
        printf("fb: /dev/fb0 size=%u mode=%u\n", (unsigned)st.st_size,
               (unsigned)st.st_mode);

        /* Raw write through the device file: one 4 KiB row of pixels. */
        static uint32_t row[1024];
        for (int i = 0; i < 1024; i++)
            row[i] = 0x000000ff | ((uint32_t)(i * 255 / 1023) << 16) |
                     ((uint32_t)i * 4);
        uint32_t off = (info.height / 2) * info.pitch + (info.width / 4) * 4;
        if (lseek(fd, off, 0) < 0) {
            printf("fb: lseek /dev/fb0 failed\n");
            close(fd);
            return 1;
        }
        ssize_t w = write(fd, row, sizeof(row));
        printf("fb: wrote %d bytes to /dev/fb0 @ %u (flushed)\n", (int)w, off);

        /* Read the same bytes back through the device file. */
        static uint32_t back[1024];
        lseek(fd, off, 0);
        ssize_t r = read(fd, back, sizeof(back));
        int ok = (r == (ssize_t)sizeof(back)) && back[0] == row[0] &&
                 back[1023] == row[1023];
        printf("fb: /dev/fb0 readback %s (%d bytes)\n", ok ? "OK" : "FAIL",
               (int)r);
        close(fd);
        return ok ? 0 : 1;
    }

    if (strcmp(sub, "test") != 0) {
        printf("fb: unknown command '%s' (info|clear|text|dev|test)\n", sub);
        return 1;
    }

    volatile uint32_t *fb = (volatile uint32_t *)fbmap();
    if (!fb) {
        printf("fb: fbmap failed\n");
        return 1;
    }

    fbclear(0x00081018);

    /* Gradient + moving rainbow bars + checkerboard, drawn in U-mode. */
    uint32_t W = info.width, H = info.height;
    for (uint32_t y = 0; y < H; y++) {
        uint32_t c = ((y * 0x30) / H) << 16 | ((y * 0x50) / H) << 8 |
                     (0x20 + (y * 0x40) / H);
        for (uint32_t x = 0; x < W; x++)
            fb[(uint64_t)y * W + x] = c;
    }

    static const uint32_t bars[] = {
        0x00ff0000, 0x00ff7f00, 0x00ffff00, 0x0000ff00,
        0x0000ffff, 0x000000ff, 0x00ff00ff, 0x00ffffff,
    };
    for (uint32_t i = 0; i < 8; i++)
        for (uint32_t y = 0; y < H / 10; y++)
            for (uint32_t x = 0; x < W / 8; x++)
                fb[(uint64_t)y * W + i * (W / 8) + x] = bars[i];

    for (uint32_t y = H / 4; y < H / 4 + H / 2; y += 32)
        for (uint32_t x = 0; x < W; x += 32)
            for (uint32_t yy = 0; yy < 32 && y + yy < H; yy++)
                for (uint32_t xx = 0; xx < 32 && x + xx < W; xx++)
                    fb[(uint64_t)(y + yy) * W + (x + xx)] =
                        (((x / 32) + (y / 32)) & 1) ? 0x00303038
                                                    : 0x00181820;

    fbflush();
    const char *tag = "qvOS shell fb test: 8 colour bars + checkerboard\n";
    fbtext(tag, strlen(tag));
    return 0;
}

static int cmd_cd(int argc, char **argv)
{
    const char *path = (argc > 1) ? argv[1] : "/";
    if (chdir(path) != 0) {
        printf("cd: %s: no such directory\n", path);
        return 1;
    }
    return 0;
}

static int cmd_pwd(int argc, char **argv)
{
    char buf[128];

    (void)argc; (void)argv;
    if (!getcwd(buf, sizeof(buf))) {
        printf("pwd: failed\n");
        return 1;
    }
    printf("%s\n", buf);
    return 0;
}

static int cmd_mount(int argc, char **argv)
{
    if (argc < 3) {
        printf("usage: mount <source> <target>\n");
        return 1;
    }
    int r = mount(argv[1], argv[2]);
    printf("mount %s -> %s: %s\n", argv[1], argv[2],
           r == 0 ? "OK" : "FAIL");
    return r;
}

static int cmd_vda(int argc, char **argv)
{
    int fd = open("/dev/vda", 2);   /* O_RDWR */
    if (fd < 0) {
        printf("vda: open /dev/vda failed (virtio-blk missing?)\n");
        return 1;
    }
    struct qvos_stat st;
    if (fstat(fd, &st) != 0) {
        printf("vda: fstat /dev/vda failed\n");
        close(fd);
        return 1;
    }

    const char *sub = (argc > 1) ? argv[1] : "info";
    int rc = 0;

    if (strcmp(sub, "info") == 0) {
        printf("vda: size=%u mode=%u\n", (unsigned)st.st_size,
               (unsigned)st.st_mode);
    } else if (strcmp(sub, "read") == 0) {
        uint32_t off = (argc > 2) ? parse_uint(argv[2]) : 0;
        unsigned char buf[16];
        if (lseek(fd, off, 0) < 0 || read(fd, buf, sizeof(buf)) !=
            (ssize_t)sizeof(buf)) {
            printf("vda: read @ %u failed\n", off);
            rc = 1;
        } else {
            printf("vda[%u..]: ", off);
            print_hex_bytes(buf, sizeof(buf));
        }
    } else if (strcmp(sub, "write") == 0) {
        if (argc < 4) {
            printf("usage: vda write <offset> <value>\n");
            rc = 1;
        } else {
            uint32_t off = parse_uint(argv[2]);
            uint32_t val = parse_uint(argv[3]);
            if (lseek(fd, off, 0) < 0 || write(fd, &val, 4) != 4) {
                printf("vda: write @ %u failed\n", off);
                rc = 1;
            } else {
                printf("vda: wrote %x to offset %u\n", (unsigned long)val, off);
            }
        }
    } else if (strcmp(sub, "test") == 0) {
        /* Raw read/write round-trip in the last 512-byte sector. */
        uint32_t sec = 512;
        uint32_t off = ((uint32_t)st.st_size > sec)
                       ? ((uint32_t)st.st_size / sec - 1) * sec : 0;
        static uint32_t pat[128];
        static uint32_t back[128];
        for (int i = 0; i < 128; i++)
            pat[i] = 0x5aa50000U ^ ((uint32_t)i * 0x01010101U);
        if (lseek(fd, off, 0) < 0 ||
            write(fd, pat, sizeof(pat)) != (ssize_t)sizeof(pat) ||
            lseek(fd, off, 0) < 0 ||
            read(fd, back, sizeof(back)) != (ssize_t)sizeof(back)) {
            printf("vda: test @ %u failed\n", off);
            rc = 1;
        } else {
            int ok = 1;
            for (int i = 0; i < 128; i++)
                if (pat[i] != back[i]) ok = 0;
            printf("vda: 512-byte readback @ %u: %s\n", off,
                   ok ? "OK" : "FAIL");
            rc = ok ? 0 : 1;
        }
    } else {
        printf("vda: unknown command '%s' (info|read|write|test)\n", sub);
        rc = 1;
    }

    close(fd);
    return rc;
}

static int run_program(int argc, char **argv)
{
    char path[128];
    if (argv[0][0] == '/')
        strcpy(path, argv[0]);
    else {
        strcpy(path, "/bin/");
        strcat(path, argv[0]);
    }

    int pid = fork();
    if (pid < 0) {
        printf("fork failed\n");
        return 1;
    }
    if (pid == 0) {
        execve(path, argv, 0);
        /* Not a real file: fall back to the toybox multiplexer. */
        execve("/bin/toybox", argv, 0);
        printf("%s: exec failed\n", path);
        exit(127);
    }
    int st = 0;
    waitpid(pid, &st, 0);
    return st;
}

int main(void)
{
    char line[MAX_LINE];
    char *argv[MAX_ARGS];

    printf("\n[qvos] small shell. type 'help'\n");
    for (;;) {
        prompt();
        int n = readline(line, sizeof(line));
        if (n == 0)
            continue;

        int argc = 0;
        char *p = line;
        while (*p && argc < MAX_ARGS - 1) {
            while (*p == ' ' || *p == '\t')
                p++;
            if (!*p)
                break;
            argv[argc++] = p;
            while (*p && *p != ' ' && *p != '\t')
                p++;
            if (*p)
                *p++ = 0;
        }
        argv[argc] = 0;
        if (argc == 0)
            continue;

        if (strcmp(argv[0], "exit") == 0)
            break;
        if (strcmp(argv[0], "help") == 0) {
            printf("builtins: help echo cat hd ostest test fb vda mount cd pwd panic poweroff exit\n");
            printf("external: any executable in /bin (try ls or fbdemo)\n");
        } else if (strcmp(argv[0], "echo") == 0) {
            cmd_echo(argc, argv);
        } else if (strcmp(argv[0], "cat") == 0) {
            cmd_cat(argc, argv);
        } else if (strcmp(argv[0], "hd") == 0) {
            cmd_hd(argc, argv);
        } else if (strcmp(argv[0], "ostest") == 0) {
            cmd_ostest();
        } else if (strcmp(argv[0], "test") == 0) {
            cmd_ostest();   /* alias: run the POSIX-subset test suite */
        } else if (strcmp(argv[0], "fb") == 0) {
            cmd_fb(argc, argv);
        } else if (strcmp(argv[0], "vda") == 0) {
            cmd_vda(argc, argv);
        } else if (strcmp(argv[0], "mount") == 0) {
            cmd_mount(argc, argv);
        } else if (strcmp(argv[0], "cd") == 0) {
            cmd_cd(argc, argv);
        } else if (strcmp(argv[0], "pwd") == 0) {
            cmd_pwd(argc, argv);
        } else if (strcmp(argv[0], "panic") == 0) {
            printf("triggering kernel panic...\n");
            reboot(0xdeadbeef);
        } else if (strcmp(argv[0], "poweroff") == 0) {
            printf("powering off via SBI SRST...\n");
            poweroff();
        } else {
            run_program(argc, argv);
        }
    }
    return 0;
}
