/* /bin/argtest — exit 0 iff execve delivered the expected argc/argv.
 * Used by /bin/ostest to guard the execve argc preservation fix. */
#include "libc.h"

int main(int argc, char **argv)
{
    if (argc != 3)
        return 100;
    if (strcmp(argv[1], "alpha") != 0)
        return 101;
    if (strcmp(argv[2], "beta") != 0)
        return 102;
    return 0;
}
