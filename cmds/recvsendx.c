#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include "shared.h"

#define MAX_DGRAM_SIZE 65536

/* these are 'extended' versions of recv/send, giving up generic concepts
 * for ease of use in specific testing scenarios
 *
 * recvx - based on the control connection (IP version, dst addr), create
 *         a listening socket, send its port nr. to client (newline-delim.),
 *         detach and start listening
 *
 * sendx - based on the control connection (IP version, src addr), detach
 *         and start connection/send attempts on the peer address
 */

/* args:
 *   recvx,<l4proto>
 *   sendx,<l4proto>,[string]
 */

static int cmd_recvx(int argc, char **argv, struct session_info *info)
{
    int s = -1, cs = -1;
    int type, proto;
    int port;
    char *buff;

    if (argc < 2)
        return 1;

    if (!strcmp(argv[1], "tcp")) {
        type = SOCK_STREAM;
        proto = IPPROTO_TCP;
    } else if (!strcmp(argv[1], "udp")) {
        type = SOCK_DGRAM;
        proto = IPPROTO_UDP;
    } else {
        return 1;
    }

    /* create listening socket */
    s = sock_ctol(info->sock, type, proto, 1, &port);
    if (s == -1) {
        perror("sock_ctol");
        return 1;
    }

    buff = xmalloc(MAX_DGRAM_SIZE);

    /* send port nr. to client */
    dprintf(info->sock, "%d\n", port);

    /* detach */
    linger(info->sock, 1);
    close(info->sock);

    /* read data */
    if (type == SOCK_STREAM) {
        cs = accept(s, NULL, 0);
        if (cs == -1) {
            perror("accept");
            goto err;
        }
        close(s);
        s = cs;
    }
    if (read(s, buff, MAX_DGRAM_SIZE) == -1)
        goto err;

    free(buff);
    close(s);
    return 0;
err:
    free(buff);
    close(s);
    return 1;
}

static int msleep(int msec)
{
    struct timespec tv;
    tv.tv_sec = msec / 1000;
    tv.tv_nsec = (msec % 1000) * 1000000;
    return nanosleep(&tv, NULL);
}

static int cmd_sendx(int argc, char **argv, struct session_info *info)
{
    int s, rc;
    int type, proto;
    int port;
    char *string = "lorem ipsum dolor sit amet\n";

    union {
        struct sockaddr sa;
        struct sockaddr_in in;
        struct sockaddr_in6 in6;
    } saddr;
    socklen_t slen;

    if (argc < 3)
        return 1;

    if (!strcmp(argv[1], "tcp")) {
        type = SOCK_STREAM;
        proto = IPPROTO_TCP;
    } else if (!strcmp(argv[1], "udp")) {
        type = SOCK_DGRAM;
        proto = IPPROTO_UDP;
    } else {
        return 1;
    }

    port = atoi(argv[2]);
    if (port <= 0)
        return 1;

    if (argc >= 4)
        string = argv[3];

    /* use addr / family of the control connection */
    slen = sizeof(saddr);
    if (getpeername(info->sock, (struct sockaddr *)&saddr, &slen) == -1)
        return 1;

    switch (saddr.sa.sa_family) {
        case AF_INET:   saddr.in.sin_port = htons(port); break;
        case AF_INET6:  saddr.in6.sin6_port = htons(port); break;
        default:        return 1;
    }

    s = socket(saddr.sa.sa_family, type, proto);
    if (s < 0)
        return -1;

    /* detach */
    linger(info->sock, 1);
    close(info->sock);

    switch (type) {
        case SOCK_STREAM:
            /* try to do quick connection attempts, don't wait long periods
             * for failures to timeout */
            rc = 2;
            if (setsockopt(s, IPPROTO_TCP, TCP_SYNCNT, &rc, sizeof(int)) == -1)
                goto err;
            while (connect(s, &saddr.sa, slen) == -1) {
                switch (errno) {
                    case EAGAIN:
                    case ECONNREFUSED:
                    case ENETUNREACH:
                        msleep(100);
                        break;
                    case ETIMEDOUT:
                        break;
                    default:
                        goto err;
                }
            }
            if (send(s, string, strlen(string), 0) == -1)
                goto err;
            break;
        case SOCK_DGRAM:
            /* since we can't detect success, simply send packets forever */
            while (1) {
                if (sendto(s, string, strlen(string), 0, &saddr.sa, slen) == -1)
                    goto err;
                msleep(100);
            }
            break;
        default:
            goto err;
    }

    close(s);
    return 0;
err:
    close(s);
    return 1;
}

static __newcmd struct cmd_desc cmd1 = {
    .name = "recvx",
    .parse = cmd_recvx,
};
static __newcmd struct cmd_desc cmd2 = {
    .name = "sendx",
    .parse = cmd_sendx,
};
