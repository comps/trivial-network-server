#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "shared.h"

#define MAX_DGRAM_SIZE 65536

/* this command file provides cmds to send/recv data via a "side channel",
 * eg. newly created additional socket (bypassing client control socket)
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

static int recvsend(enum dir dir, struct session_info *info,
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
               && (bytes = read(s, buff, MAX_DGRAM_SIZE)) > 0);
        break;
    case DIR_SEND:
        strcpy(buff, "lorem ipsum dolor sit amet\n");
        write(s, buff, strlen(buff));
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

static int cmd_recv(int argc, char **argv, struct session_info *info)
{
    if (argc < 4)
        return 1;

    return recvsend(DIR_RECV, info, argv[1], argv[2], argv[3],
                    (argc >= 5) ? atoi(argv[4]) : 1);
}

static int cmd_send(int argc, char **argv, struct session_info *info)
{
    if (argc < 4)
        return 1;

    return recvsend(DIR_SEND, info, argv[1], argv[2], argv[3], 1);
}

static int cmd_sendback(int argc, char **argv, struct session_info *info)
{
    char remote[REMOTE_ADDRA_MAX];

    if (argc < 3)
        return 1;

    if (remote_addra(info->sock, remote) == -1)
        return 1;

    return recvsend(DIR_SEND, info, remote, argv[1], argv[2], 1);
}

static __newcmd struct cmd_desc cmd1 = {
    .name = "recv",
    .parse = cmd_recv,
};
static __newcmd struct cmd_desc cmd2 = {
    .name = "send",
    .parse = cmd_send,
};
static __newcmd struct cmd_desc cmd3 = {
    .name = "sendback",
    .parse = cmd_sendback,
};
