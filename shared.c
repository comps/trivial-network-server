/*
 * small helpers shared amongst the entire codebase of this project
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <signal.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "shared.h"

/* format a verbose message */
void _verbose(char *file, int line, char *fmt, ...)
{
    va_list ap;
    if (fmt) {
        va_start(ap, fmt);
        fprintf(stdout, "%s:%d: ", file, line);
        vfprintf(stdout, fmt, ap);
        va_end(ap);
    }
}
/* format an error msg */
void _error(char *file, int line, char *fmt, ...)
{
    va_list ap;
    if (fmt) {
        va_start(ap, fmt);
        fprintf(stderr, "%s:%d: ", file, line);
        vfprintf(stderr, fmt, ap);
        va_end(ap);
    }
}
/* exit with msg, never return */
void _error_down(char *file, int line, char *fmt, ...)
{
    va_list ap;
    if (fmt) {
        va_start(ap, fmt);
        fprintf(stderr, "%s:%d: ", file, line);
        vfprintf(stderr, fmt, ap);
        va_end(ap);
    }
    exit(EXIT_FAILURE);
}
/* use perror to print error, exit, never return */
void _perror_down(char *file, int line, char *msg)
{
    if (msg) {
        fprintf(stderr, "%s:%d: ", file, line);
        perror(msg);
    }
    exit(EXIT_FAILURE);
}

/* "safe" malloc */
void *xmalloc(size_t size)
{
    void *ptr = malloc(size);
    if (ptr == NULL)
        error_down("malloc failed\n");
    return ptr;
}
void *xrealloc(void *ptr, size_t size)
{
    /* no need to save old ptr as we just exit on error */
    ptr = realloc(ptr, size);
    if (ptr == NULL)
        error_down("realloc failed\n");
    return ptr;
}
/* "safe" recv */
ssize_t xrecv(int sockfd, void *buf, size_t len, int flags)
{
    ssize_t ret = recv(sockfd, buf, len, flags);
    if (ret == -1)
        perror_down("recv");
    else if (ret == 0)  /* only for blocking stream sockets! */
        error_down("recv: unexpected stream end-of-file\n");
    return ret;
}
/* safe signal hooker, return func pointer to old handler */
void (*xsignal(int signum, void (*newhandler)(int)))(int)
{
    struct sigaction oldact, act = { .sa_handler = newhandler };
    if (sigaction(signum, &act, &oldact) == -1)
        perror_down("sigaction");
    return oldact.sa_handler;
}
/* disable/enable background socket linger,
 * when disabled, close() without shutdown() sends back RST (if TCP),
 * when enabled, close() lingers in the background (linux default) */
int linger(int sock, int op)
{
    return setsockopt(sock, SOL_SOCKET, SO_LINGER,
               &(struct linger){!op, 0}, sizeof(struct linger));
}

/* store the ascii equivalent of a local/remote addr into a char[],
 * which *must* be at least ASCII_ADDR_MAX bytes big.
 * the "func" arg should be either getsockname() or getpeername() */
int ascii_addr(int sock, char *dest,
               int (*func)(int, struct sockaddr *, socklen_t *))
{
    union {
        struct sockaddr sa;
        struct sockaddr_in in;
        struct sockaddr_in6 in6;
    } saddr;
    socklen_t slen;

    slen = sizeof(saddr);
    if (func(sock, (struct sockaddr *)&saddr, &slen) == -1)
        return -1;

    switch (saddr.sa.sa_family) {
        case AF_INET:
            if (inet_ntop(AF_INET, &saddr.in.sin_addr, dest,
                                   ASCII_ADDR_MAX) == NULL)
                return -1;
            break;
        case AF_INET6:
            if (inet_ntop(AF_INET6, &saddr.in6.sin6_addr, dest,
                                    ASCII_ADDR_MAX) == NULL)
                return -1;
            break;
        default:
            return -1;
    }

    return 0;
}

/* iterate over cmd descs in the cmds ELF section */
struct cmd_desc *cmd_descs_iterate(struct cmd_desc *itr)
{
    if (itr == NULL) {
        if (&__start_cmd_descs >= &__stop_cmd_descs)
            return NULL;
        else
            return &__start_cmd_descs;
    }
    if (++itr < &__stop_cmd_descs)
        return itr;
    return NULL;
}
