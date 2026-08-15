/* /bin/init: run the POSIX-subset test suite, then hand over to the
 * small fallback shell. */
#include "libc.h"

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    printf("[init] qvos init: argc=%d\n", argc);
    printf("[init] starting /bin/ostest\n");

    int pid = fork();
    if (pid < 0) {
        printf("[init] fork failed\n");
        exit(1);
    }
    if (pid == 0) {
        char *args[] = { "/bin/ostest", 0 };
        int r = execve("/bin/ostest", args, 0);
        printf("[init] execve /bin/ostest failed: %d\n", r);
        exit(127);
    }

    int st = -1;
    int w = waitpid(pid, &st, 0);
    printf("[init] ostest pid=%d status=%d\n", w, st);

    printf("[init] starting /bin/sh\n");
    char *shargs[] = { "/bin/sh", 0 };
    execve("/bin/sh", shargs, 0);
    printf("[init] execve /bin/sh failed\n");
    exit(2);
}
