/* Stage 5 freestanding U-mode test.  Loaded by the kernel as a raw binary
 * at 0x40000000 (Stage 6 replaces this with a real ELF loader).  Exercises:
 *   - ecall syscalls (write/getpid/fork/waitpid/yield/exit)
 *   - single-precision FP arithmetic (forces F-state save/restore)
 *   - RVV 1.0 vle32/vadd/vse32 (forces vector-state save/restore)
 */
typedef unsigned long u64;
typedef unsigned int  u32;

static inline long sc6(long n, long a0, long a1, long a2,
                       long a3, long a4, long a5)
{
    register long ra0 __asm__("a0") = a0;
    register long ra1 __asm__("a1") = a1;
    register long ra2 __asm__("a2") = a2;
    register long ra3 __asm__("a3") = a3;
    register long ra4 __asm__("a4") = a4;
    register long ra5 __asm__("a5") = a5;
    register long ra7 __asm__("a7") = n;
    __asm__ volatile("ecall" : "+r"(ra0)
                     : "r"(ra1), "r"(ra2), "r"(ra3), "r"(ra4),
                       "r"(ra5), "r"(ra7)
                     : "memory");
    return ra0;
}

static long write_s(int fd, const char *s, unsigned long n)
{
    return sc6(4, fd, (long)s, (long)n, 0, 0, 0);
}

static void puts_s(const char *s)
{
    unsigned long n = 0;
    while (s[n])
        n++;
    write_s(1, s, n);
}

static void print_hex(u64 v)
{
    static const char d[] = "0123456789abcdef";
    char buf[19];
    buf[0] = '0';
    buf[1] = 'x';
    buf[18] = 0;
    for (int i = 17; i >= 2; i--) {
        buf[i] = d[v & 15];
        v >>= 4;
    }
    puts_s(buf);
}

static void print_dec(long v)
{
    char buf[24];
    int i = 23;
    buf[i--] = 0;
    int neg = 0;
    if (v < 0) {
        neg = 1;
        v = -v;
    }
    do {
        buf[i--] = (char)('0' + (v % 10));
        v /= 10;
    } while (v);
    if (neg)
        buf[i--] = '-';
    puts_s(&buf[i + 1]);
}

__attribute__((noreturn)) void _start(void)
{
    puts_s("[user] stage5 U-mode process started\n");

    long pid = sc6(10, 0, 0, 0, 0, 0, 0);      /* getpid */
    puts_s("[user] getpid() = ");
    print_dec(pid);
    puts_s("\n");

    /* ---- FP: force sstatus.FS Dirty across the next ecall ---- */
    volatile float fa = 1.5f, fb = 2.25f, fc = 0.0f;
    fc = fa + fb;
    u32 fbits = *(volatile u32 *)&fc;
    puts_s("[user] FP 1.5+2.25 = ");
    print_hex(fbits);
    puts_s(fbits == 0x40700000 ? " (ok)\n" : " (FAIL)\n");

    /* ---- RVV: force sstatus.VS Dirty across the next ecall ---- */
    volatile u32 va[4] = { 1, 2, 3, 4 };
    volatile u32 vb[4] = { 10, 20, 30, 40 };
    volatile u32 vc[4] = { 0, 0, 0, 0 };
    __asm__ volatile(
        "vsetivli zero, 4, e32, m1, ta, ma\n\t"
        "vle32.v v0, (%0)\n\t"
        "vle32.v v1, (%1)\n\t"
        "vadd.vv v2, v0, v1\n\t"
        "vse32.v v2, (%2)\n\t"
        :
        : "r"(va), "r"(vb), "r"(vc)
        : "memory", "v0", "v1", "v2");
    puts_s("[user] RVV [1,2,3,4]+[10,20,30,40] = [");
    int vok = 1;
    for (int i = 0; i < 4; i++) {
        if (vc[i] != 11 + 11 * (u32)i)
            vok = 0;
        if (i)
            puts_s(",");
        print_dec(vc[i]);
    }
    puts_s(vok ? "] (ok)\n" : "] (FAIL)\n");

    /* ---- fork/wait/yield: scheduler + process syscalls ---- */
    long child = sc6(2, 0, 0, 0, 0, 0, 0);      /* fork */
    if (child == 0) {
        for (int i = 0; i < 3; i++) {
            puts_s("[user] child yields\n");
            sc6(13, 0, 0, 0, 0, 0, 0);          /* yield */
        }
        sc6(1, 0, 0, 0, 0, 0, 0);               /* exit(0) */
    }
    if (child < 0) {
        puts_s("[user] fork FAILED\n");
        sc6(1, 1, 0, 0, 0, 0, 0);
    }

    int status = -1;
    long waited = sc6(7, child, (long)&status, 0, 0, 0, 0);  /* waitpid */
    puts_s("[user] parent waited pid ");
    print_dec(waited);
    puts_s(" status ");
    print_dec(status);
    puts_s("\n");

    puts_s("[user] STAGE5_USER_OK\n");
    sc6(1, 0, 0, 0, 0, 0, 0);                   /* exit(0) */
    for (;;) {}
}
