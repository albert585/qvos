/* /bin/wtest — minimal FAT32 write regression:
 * create /WTEST.DAT, write, seek back, read-verify, close, reopen and
 * verify the size + contents survived in the directory entry.
 */
#include "libc.h"

#define PATH "/WTEST.DAT"
#define O_RDWR   2
#define O_CREAT  0x40
#define O_TRUNC  0x200

int main(void)
{
    const char *msg = "hello fat32 write path";
    char buf[64];
    struct qvos_stat st;
    int fd = open(PATH, O_RDWR | O_CREAT | O_TRUNC);

    if (fd < 0) {
        printf("[wtest] open/create failed fd=%d\n", fd);
        return 1;
    }

    int n = 0;
    while (msg[n])
        n++;
    if (write(fd, msg, n) != n) {
        printf("[wtest] write failed\n");
        return 1;
    }
    printf("[wtest] wrote %d bytes\n", n);

    if (lseek(fd, 0, 0) != 0) {
        printf("[wtest] lseek 0 failed\n");
        return 1;
    }
    if (read(fd, buf, n) != n) {
        printf("[wtest] readback failed\n");
        return 1;
    }
    for (int i = 0; i < n; i++)
        if (buf[i] != msg[i]) {
            printf("[wtest] MISMATCH at %d: %02x != %02x\n",
                   i, buf[i], msg[i]);
            return 1;
        }
    printf("[wtest] readback matches: %s\n", buf);

    if (fstat(fd, &st) != 0 || st.st_size != (uint64_t)n) {
        printf("[wtest] fstat size bad (%llu)\n",
               (unsigned long long)st.st_size);
        return 1;
    }
    close(fd);

    fd = open(PATH, 0);
    if (fd < 0) {
        printf("[wtest] reopen failed\n");
        return 1;
    }
    if (fstat(fd, &st) != 0 || st.st_size != (uint64_t)n) {
        printf("[wtest] reopen size lost\n");
        return 1;
    }
    printf("[wtest] reopen size %llu PASS\n",
           (unsigned long long)st.st_size);
    close(fd);
    printf("[wtest] RESULT: PASS\n");
    return 0;
}
