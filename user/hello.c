/* /bin/hello — minimal custom program template. */
#include "libc.h"

int main(int argc, char **argv)
{
    printf("[hello] qvos userspace, argc=%d\n", argc);
    for (int i = 0; i < argc; i++)
        printf("  argv[%d] = %s\n", i, argv[i]);

    struct qvos_fb_info info;
    if (fbinfo(&info) == 0)
        printf("[hello] framebuffer: %ux%u %ubpp\n",
               info.width, info.height, info.bpp);

    return 0;
}
