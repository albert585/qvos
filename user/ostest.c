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
