#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "shared.h"

#define MAX_DGRAM_SIZE 65536

/* this command file provides cmds to send/recv data via a "side channel",
 * eg. newly created additional socket (bypassing client socket)
 *
 * recv - listen on the given ip version, l4 proto and port
 * send - send data to given addr, l4 proto and port
 * sendback - same as send, but get addr from the client socket
 */

/* args:
 *   recv,<addr>,<l4proto>,<port>[,dgrams]   # dgrams only for udp
 *   send,<addr>,<l4proto>,<port>
 *   sendback,<l4proto>,<port>
 */

/* direction */
enum dir {
    DIR_SEND = 0,
    DIR_RECV = 1,  /* same as 'server' for create_socket() */
};

static int recvsend(enum dir dir, struct client_info *c,
                    char *addr, char *proto, char *port, int dgrams)
{
    int s = -1, cs;
    int stype;

    char *buff = NULL;
    ssize_t bytes;

    buff = xmalloc(MAX_DGRAM_SIZE);

    if (!strcmp(proto, "tcp"))
        stype = SOCK_STREAM;
    else if (!strcmp(proto, "udp"))
        stype = SOCK_DGRAM;
    else
        goto err;

    s = create_socket(addr, port, stype, dir);
    if (s == -1)
        goto err;

    switch (dir) {
    case DIR_RECV:
        if (stype == SOCK_STREAM) {
            cs = accept(s, NULL, 0);
            close(s);
            s = cs;
        }
        /* works surprisingly for both TCP and UDP, since in case of UDP,
         * we don't need to send anything back over the UDP socket */
        /* also, if the client sock is in binary mode, echo recv'd data */
        while (((stype == SOCK_DGRAM && dgrams-- > 0) || stype != SOCK_DGRAM)
               && (bytes = read(s, buff, MAX_DGRAM_SIZE)) > 0)
            if (c->sock != -1 && c->sock_mode == CTL_MODE_BINARY)
                write(c->sock, buff, bytes);
        break;
    case DIR_SEND:
        /* if the client sock is in binary mode, read data from it until read()
         * returns 0 (client ended / sent empty packet on ^D),
         * otherwise just send some random data */
        if (c->sock_mode == CTL_MODE_BINARY) {
            while ((bytes = read(c->sock, buff, MAX_DGRAM_SIZE)) > 0)
                write(s, buff, bytes);
        } else {
            strcpy(buff, "lorem ipsum dolor sit amet\n");
            write(s, buff, strlen(buff));
        }
        break;
    default:
        goto err;
    }

    free(buff);
    close(s);
    return 0;
err:
    free(buff);
    close(s);
    return 1;
}

static int cmd_recv(int argc, char **argv, struct client_info *c)
{
    if (argc < 4)
        return 1;

    return recvsend(DIR_RECV, c, argv[1], argv[2], argv[3],
                    (argc >= 5) ? atoi(argv[4]) : 1);
}

static int cmd_send(int argc, char **argv, struct client_info *c)
{
    if (argc < 4)
        return 1;

    return recvsend(DIR_SEND, c, argv[1], argv[2], argv[3], 1);
}

static int cmd_sendback(int argc, char **argv, struct client_info *c)
{
    union {
        struct sockaddr sa;
        struct sockaddr_in in;
        struct sockaddr_in6 in6;
    } saddr;
    socklen_t slen;
    char asaddr[INET6_ADDRSTRLEN];  /* covers INET4 as well */

    if (argc < 3)
        return 1;

    slen = sizeof(saddr);
    if (getpeername(c->sock, (struct sockaddr *)&saddr, &slen) == -1)
        return 1;

    switch (saddr.sa.sa_family) {
        case AF_INET:
            if (inet_ntop(AF_INET, &saddr.in.sin_addr, asaddr, slen) == NULL)
                return 1;
            break;

        case AF_INET6:
            if (inet_ntop(AF_INET6, &saddr.in6.sin6_addr, asaddr, slen) == NULL)
                return 1;
            break;

        default:
            return 1;
    }

    return recvsend(DIR_SEND, c, asaddr, argv[1], argv[2], 1);
}

static __newcmd struct cmd_info cmd1 = {
    .name = "recv",
    .parse = cmd_recv,
};
static __newcmd struct cmd_info cmd2 = {
    .name = "send",
    .parse = cmd_send,
};
static __newcmd struct cmd_info cmd3 = {
    .name = "sendback",
    .parse = cmd_sendback,
};
