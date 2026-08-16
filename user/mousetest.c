/* /bin/mousetest — dump virtio-input events from /dev/mouse for ~2s. */
#include "libc.h"

int main(void)
{
    int fd = open("/dev/mouse", 0);
    if (fd < 0) {
        printf("[mousetest] /dev/mouse unavailable\n");
        return 1;
    }
    printf("[mousetest] reading /dev/mouse...\n");

    struct qvos_input_event evs[16];
    for (int i = 0; i < 200; i++) {
        int n = read(fd, evs, sizeof(evs));
        if (n > 0) {
            int cnt = n / (int)sizeof(evs[0]);
            for (int k = 0; k < cnt; k++)
                printf("mouse: type=%u code=%u value=%u\n",
                       evs[k].type, evs[k].code, evs[k].value);
        }
        nanosleep();
    }
    close(fd);
    printf("[mousetest] done\n");
    return 0;
}
