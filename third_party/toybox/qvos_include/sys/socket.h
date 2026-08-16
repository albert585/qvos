/* Minimal <sys/socket.h> for toybox/picolibc (definitions only). */
#ifndef QVOS_SYS_SOCKET_H
#define QVOS_SYS_SOCKET_H

#include <sys/types.h>

typedef __uint32_t socklen_t;

#define AF_UNIX     1
#define AF_INET     2
#define AF_INET6    10

#define SOCK_STREAM 1
#define SOCK_DGRAM  2
#define SOCK_RAW    3

#define SOL_SOCKET  1
#define SO_REUSEADDR 2
#define SO_BROADCAST 6

struct sockaddr {
    unsigned short sa_family;
    char           sa_data[14];
};

int socket(int, int, int);
int bind(int, const struct sockaddr *, socklen_t);
int connect(int, const struct sockaddr *, socklen_t);
int listen(int, int);
int accept(int, struct sockaddr *, socklen_t *);
int setsockopt(int, int, int, const void *, socklen_t);
int getsockopt(int, int, int, void *, socklen_t *);
ssize_t send(int, const void *, size_t, int);
ssize_t recv(int, void *, size_t, int);
int shutdown(int, int);

#endif
