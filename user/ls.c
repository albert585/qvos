/* /bin/ls — list a FAT32 or devfs directory via SYS_getdents. */
#include "libc.h"

int main(int argc, char **argv)
{
    const char *path = (argc > 1) ? argv[1] : "/";
    int fd = open(path, 0);   /* O_RDONLY */
    if (fd < 0) {
        printf("ls: %s: no such file or directory\n", path);
        return 1;
    }

    struct qvos_dirent ents[16];
    for (;;) {
        int n = getdents(fd, ents, 16);
        if (n < 0) {
            printf("ls: %s: not a directory\n", path);
            close(fd);
            return 1;
        }
        if (n == 0)
            break;
        for (int i = 0; i < n; i++) {
            printf("%s", ents[i].d_name);
            if (ents[i].d_type == 1)
                printf("/");
            printf("\n");
        }
    }
    close(fd);
    return 0;
}
