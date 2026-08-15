/* /bin/sh — small fallback shell for qvos. */
#include "libc.h"

#define MAX_ARGS 16
#define MAX_LINE 256

static void prompt(void)
{
    write(1, "qvos$ ", 6);
}

static int readline(char *buf, size_t max)
{
    size_t n = 0;
    for (;;) {
        char c;
        ssize_t r = read(0, &c, 1);
        if (r != 1)
            continue;
        if (c == '\r' || c == '\n') {
            buf[n] = 0;
            write(1, "\r\n", 2);
            return (int)n;
        }
        if ((c == 0x7f || c == 0x08) && n > 0) {
            n--;
            write(1, "\b \b", 3);
            continue;
        }
        if (n + 1 < max) {
            buf[n++] = c;
            write(1, &c, 1);
        }
    }
}

static int cmd_echo(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        write(1, argv[i], strlen(argv[i]));
        if (i + 1 < argc)
            write(1, " ", 1);
    }
    write(1, "\n", 1);
    return 0;
}

static int cmd_cat(int argc, char **argv)
{
    if (argc < 2) {
        printf("usage: cat <file>\n");
        return 1;
    }
    int fd = open(argv[1], 0);
    if (fd < 0) {
        printf("cat: %s: no such file\n", argv[1]);
        return 1;
    }
    char buf[256];
    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf))) > 0)
        write(1, buf, (size_t)n);
    close(fd);
    return 0;
}

static int cmd_ostest(void)
{
    int pid = fork();
    if (pid == 0) {
        char *args[] = { "/bin/ostest", 0 };
        execve("/bin/ostest", args, 0);
        exit(127);
    }
    int st = 0;
    waitpid(pid, &st, 0);
    return st;
}

static int run_program(int argc, char **argv)
{
    char path[128];
    if (argv[0][0] == '/')
        strcpy(path, argv[0]);
    else {
        strcpy(path, "/bin/");
        strcat(path, argv[0]);
    }

    int pid = fork();
    if (pid < 0) {
        printf("fork failed\n");
        return 1;
    }
    if (pid == 0) {
        execve(path, argv, 0);
        printf("%s: exec failed\n", path);
        exit(127);
    }
    int st = 0;
    waitpid(pid, &st, 0);
    return st;
}

int main(void)
{
    char line[MAX_LINE];
    char *argv[MAX_ARGS];

    printf("\n[qvos] small shell. type 'help'\n");
    for (;;) {
        prompt();
        int n = readline(line, sizeof(line));
        if (n == 0)
            continue;

        int argc = 0;
        char *p = line;
        while (*p && argc < MAX_ARGS - 1) {
            while (*p == ' ' || *p == '\t')
                p++;
            if (!*p)
                break;
            argv[argc++] = p;
            while (*p && *p != ' ' && *p != '\t')
                p++;
            if (*p)
                *p++ = 0;
        }
        argv[argc] = 0;
        if (argc == 0)
            continue;

        if (strcmp(argv[0], "exit") == 0)
            break;
        if (strcmp(argv[0], "help") == 0) {
            printf("builtins: help echo cat ostest panic poweroff exit\n");
            printf("external: any executable in /bin\n");
        } else if (strcmp(argv[0], "echo") == 0) {
            cmd_echo(argc, argv);
        } else if (strcmp(argv[0], "cat") == 0) {
            cmd_cat(argc, argv);
        } else if (strcmp(argv[0], "ostest") == 0) {
            cmd_ostest();
        } else if (strcmp(argv[0], "panic") == 0) {
            printf("triggering kernel panic...\n");
            reboot(0xdeadbeef);
        } else if (strcmp(argv[0], "poweroff") == 0) {
            printf("powering off via SBI SRST...\n");
            poweroff();
        } else {
            run_program(argc, argv);
        }
    }
    return 0;
}
