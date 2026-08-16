/* Side-by-side picolibc test: same "hello" shape as the hand-rolled
 * libc programs, but linked against picolibc + qvos syscall glue. */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

int
main(int argc, char **argv)
{
    printf("picolibc hello: argc=%d argv[0]=%s\n", argc, argv[0] ? argv[0] : "(null)");
    for (int i = 1; i < argc; i++)
        printf("  argv[%d] = %s\n", i, argv[i]);

    char *p = malloc(256);
    if (!p) {
        puts("malloc failed");
        return 1;
    }
    strcpy(p, "malloc + stdio from picolibc");
    puts(p);
    free(p);

    FILE *f = fopen("/bin/init", "rb");
    if (f) {
        unsigned char b[16];
        size_t n = fread(b, 1, sizeof(b), f);
        struct stat st;
        int sr = fstat(fileno(f), &st);
        printf("fopen/fread /bin/init: n=%d first=%02x %02x %02x %02x size=%ld fstat=%d\n",
               (int)n, b[0], b[1], b[2], b[3], (long)st.st_size, sr);
        fclose(f);
    } else {
        perror("fopen /bin/init");
    }

    printf("errno check: errno=%d\n", errno);
    return 0;
}
