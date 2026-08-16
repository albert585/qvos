/* /bin/ostest — qvos POSIX-subset acceptance test. */
#include "libc.h"

static int failures;

static void check(int cond, const char *name)
{
    printf("[ostest] %s %s\n", name, cond ? "PASS" : "FAIL");
    if (!cond)
        failures++;
}

int main(void)
{
    printf("[ostest] qvos POSIX subset test starting (pid=%d)\n", getpid());

    /* process identity */
    check(getpid() > 0, "getpid");
    check(getppid() > 0, "getppid");

    /* console write */
    const char *msg = "ostest-console-write\n";
    check(write(1, msg, strlen(msg)) == (ssize_t)strlen(msg), "write(1)");

    /* FAT32: open/fstat/lseek/read/close */
    int fd = open("/bin/ostest", 0);
    check(fd >= 0, "open /bin/ostest");
    if (fd >= 0) {
        struct qvos_stat st;
        check(fstat(fd, &st) == 0, "fstat");
        check(st.st_size > 64, "fstat size");
        unsigned magic = 0;
        check(read(fd, &magic, 4) == 4, "read ELF magic");
        check(magic == 0x464c457fU, "ELF magic value");
        check(lseek(fd, 0, 0) == 0, "lseek SEEK_SET");
        unsigned m2 = 0;
        read(fd, &m2, 4);
        check(m2 == magic, "read after lseek");
        check(close(fd) == 0, "close");
    }

    /* fork/wait/exit */
    int child = fork();
    check(child >= 0, "fork");
    if (child == 0) {
        exit(42);
    } else if (child > 0) {
        int st = -1;
        int w = waitpid(child, &st, 0);
        check(w == child, "waitpid pid");
        check(st == 42, "child exit status");
    }

    /* execve must preserve the new image's argc/argv (a0 was clobbered) */
    int apid = fork();
    check(apid >= 0, "fork for exec argv");
    if (apid == 0) {
        char *args[] = { "/bin/argtest", "alpha", "beta", 0 };
        execve("/bin/argtest", args, 0);
        exit(127);
    } else if (apid > 0) {
        int ast = -1;
        int aw = waitpid(apid, &ast, 0);
        check(aw == apid && ast == 0, "exec argc/argv preserved");
    }

    /* userspace framebuffer: map, direct write, flush, fork sharing */
    struct qvos_fb_info fi;
    if (fbinfo(&fi) == 0) {
        check(fi.width > 0 && fi.height > 0 && fi.bpp == 32,
              "fb info sane");
        volatile uint32_t *fbp = (volatile uint32_t *)fbmap();
        check(fbp != 0, "fbmap");
        if (fbp) {
            fbp[3] = 0x00123456;
            check(fbp[3] == 0x00123456, "fb direct write");
            check(fbflush() == 0, "fbflush");

            int fpid = fork();
            check(fpid >= 0, "fork with fb mapped");
            if (fpid == 0) {
                fbp[7] = 0x00abcdef;      /* must hit the shared GPU pages */
                exit(43);
            } else if (fpid > 0) {
                int fst = -1;
                int fw = waitpid(fpid, &fst, 0);
                check(fw == fpid && fst == 43, "fb fork child");
                check(fbp[7] == 0x00abcdef, "fb shared across fork");
            }
            fbclear(0x00000000);
        }

        /* /dev/fb0 raw byte API (open/lseek/write/read/fstat). */
        int devfd = open("/dev/fb0", 2);   /* O_RDWR */
        check(devfd >= 0, "open /dev/fb0");
        if (devfd >= 0) {
            struct qvos_stat dst;
            check(fstat(devfd, &dst) == 0, "/dev/fb0 fstat");
            check(dst.st_size == fi.size_bytes, "/dev/fb0 fstat size");
            uint32_t probe = 0x0000aa55;
            int off = 32;
            check(lseek(devfd, off, 0) == off, "/dev/fb0 lseek");
            check(write(devfd, &probe, 4) == 4, "/dev/fb0 write");
            uint32_t probe_back = 0;
            lseek(devfd, off, 0);
            check(read(devfd, &probe_back, 4) == 4, "/dev/fb0 read");
            check(probe_back == probe, "/dev/fb0 readback");
            check(lseek(devfd, 0, 2) == (int)fi.size_bytes,
                  "/dev/fb0 seek end");
            close(devfd);
        }
    } else {
        printf("[ostest] fb tests skipped: no virtio-gpu\n");
    }

    /* /dev/null and /dev/zero */
    int nfd = open("/dev/null", 1);        /* O_WRONLY */
    check(nfd >= 0, "open /dev/null");
    if (nfd >= 0) {
        const char discard[] = "discard me";
        check(write(nfd, discard, sizeof(discard) - 1) ==
              (ssize_t)(sizeof(discard) - 1), "/dev/null write");
        close(nfd);
    }
    int zfd = open("/dev/zero", 0);        /* O_RDONLY */
    check(zfd >= 0, "open /dev/zero");
    if (zfd >= 0) {
        unsigned char zeros[16];
        int zok = 1;
        ssize_t zr = read(zfd, zeros, sizeof(zeros));
        for (int i = 0; i < (int)sizeof(zeros); i++)
            if (zeros[i] != 0) zok = 0;
        check(zr == (ssize_t)sizeof(zeros) && zok, "/dev/zero read");
        close(zfd);
    }

    /* /dev/vda: raw virtio-blk disk (open/lseek/write/read/fstat). */
    int vfd = open("/dev/vda", 2);         /* O_RDWR */
    check(vfd >= 0, "open /dev/vda");
    if (vfd >= 0) {
        struct qvos_stat vst;
        check(fstat(vfd, &vst) == 0, "/dev/vda fstat");
        check(vst.st_size >= 512 && (vst.st_size % 512) == 0,
              "/dev/vda fstat size");
        uint32_t voff = (vst.st_size > 0x100000UL) ? 0x100000 : 0;
        uint32_t vprobe = 0x5aa5c33c;
        uint32_t vback = 0;
        check(lseek(vfd, voff, 0) == (int)voff, "/dev/vda lseek");
        check(write(vfd, &vprobe, 4) == 4, "/dev/vda write");
        lseek(vfd, voff, 0);
        check(read(vfd, &vback, 4) == 4, "/dev/vda read");
        check(vback == vprobe, "/dev/vda readback");
        check(lseek(vfd, 0, 2) == (int)vst.st_size, "/dev/vda seek end");
        close(vfd);
    } else {
        printf("[ostest] vda tests skipped: no virtio-blk\n");
    }

    /* directory enumeration: FAT root, FAT /bin and devfs /dev */
    int rootfd = open("/", 0);
    check(rootfd >= 0, "open / directory");
    if (rootfd >= 0) {
        struct qvos_dirent ents[16];
        int n = getdents(rootfd, ents, 16);
        int found_bin = 0, found_dev = 0;
        for (int i = 0; i < n; i++) {
            if (strcmp(ents[i].d_name, "BIN") == 0 &&
                ents[i].d_type == 1)
                found_bin = 1;
            if (strcmp(ents[i].d_name, "dev") == 0 &&
                ents[i].d_type == 1)
                found_dev = 1;
        }
        check(n >= 2 && found_bin && found_dev, "getdents / (BIN,dev)");
        close(rootfd);
    }

    int binfd = open("/bin", 0);
    check(binfd >= 0, "open /bin directory");
    if (binfd >= 0) {
        struct qvos_dirent ents[16];
        int n = getdents(binfd, ents, 16);
        check(n >= 4, "getdents /bin count");
        close(binfd);
    }

    int devdir = open("/dev", 0);
    check(devdir >= 0, "open /dev directory");
    if (devdir >= 0) {
        struct qvos_dirent ents[16];
        int n = getdents(devdir, ents, 16);
        int fb0 = 0, vda = 0, mouse = 0, kbd = 0;
        for (int i = 0; i < n; i++) {
            if (strcmp(ents[i].d_name, "fb0") == 0) fb0 = 1;
            if (strcmp(ents[i].d_name, "vda") == 0) vda = 1;
            if (strcmp(ents[i].d_name, "mouse") == 0) mouse = 1;
            if (strcmp(ents[i].d_name, "kbd") == 0) kbd = 1;
        }
        check(n == 6 && fb0 && vda && mouse && kbd,
              "getdents /dev (fb0,vda,mouse,kbd)");
        close(devdir);
    }

    /* pipe */
    int pfd[2];
    check(pipe(pfd) == 0, "pipe");
    if (pfd[0] >= 0) {
        const char *pm = "pipe-ok";
        ssize_t n = write(pfd[1], pm, 7);
        char rb[8] = {0};
        ssize_t rn = read(pfd[0], rb, sizeof(rb));
        check(n == 7 && rn == 7, "pipe write/read");
        int ok = 1;
        for (int i = 0; i < 7; i++)
            if (rb[i] != pm[i]) ok = 0;
        check(ok, "pipe data");
        close(pfd[0]);
        close(pfd[1]);
    }

    /* sbrk/malloc/free */
    void *a = malloc(37);
    void *b = malloc(100);
    check(a != 0 && b != 0, "malloc");
    if (a && b) {
        memset(a, 0x5a, 37);
        int ok = 1;
        for (int i = 0; i < 37; i++)
            if (((unsigned char *)a)[i] != 0x5a) ok = 0;
        check(ok, "malloc data");
        free(a);
        free(b);
        check(malloc(16) != 0, "malloc after free");
    }

    /* FP context across ecall */
    volatile float fa = 1.5f, fb = 2.25f, fc = 0.0f;
    fc = fa + fb;
    unsigned bits = *(unsigned *)&fc;
    check(bits == 0x40700000U, "FP save/restore");
    printf("[ostest] 1.5+2.25 = %x\n", bits);

    /* RVV context across ecall */
    volatile unsigned va[4] = { 1, 2, 3, 4 };
    volatile unsigned vb[4] = { 10, 20, 30, 40 };
    volatile unsigned vc[4] = { 0, 0, 0, 0 };
    __asm__ volatile(
        "vsetivli zero, 4, e32, m1, ta, ma\n\t"
        "vle32.v v0, (%0)\n\t"
        "vle32.v v1, (%1)\n\t"
        "vadd.vv v2, v0, v1\n\t"
        "vse32.v v2, (%2)\n\t"
        :
        : "r"(va), "r"(vb), "r"(vc)
        : "memory", "v0", "v1", "v2");
    int vok = 1;
    for (int i = 0; i < 4; i++)
        if (vc[i] != 11 + 11 * i) vok = 0;
    check(vok, "RVV save/restore");
    printf("[ostest] RVV = [%d,%d,%d,%d]\n", vc[0], vc[1], vc[2], vc[3]);

    /* scheduler: yield several quanta */
    for (int i = 0; i < 5; i++)
        yield();
    check(1, "yield");

    printf("[ostest] %s (%d failures)\n",
           failures ? "RESULT: FAIL" : "RESULT: PASS", failures);
    return failures ? 1 : 0;
}
