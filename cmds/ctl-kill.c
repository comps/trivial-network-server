#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <dirent.h>
#include <arpa/inet.h>
#include "shared.h"

/*
 * forcibly kill a numbered session (child process)
 *
 * this can be very useful in cases where the session parser is busy processing
 * the cmdline and cannot therefore react to ctl-end (to end the session)
 *
 * since there's no easy and safe way to interrupt the parser itself and inject
 * a ctl-end command, it needs to be killed externally, from a session-less
 * connection or another session, using ctl-kill, implemented below
 *
 * ctl-kill can be thought of as a per-session cleanup where the server-wide
 * "cleanup" command would be an overkill
 */

/* args:
 *   ctl-kill,<sessionid>
 */

static void htonl_arr(uint32_t *arr, size_t len)
{
    for (; len; arr++, len--)
        *arr = htonl(*arr);
}

/* like inet_ntop, but uses space-less uppercase hex */
static size_t hex_ntop(int af, const void *src, char *dst, size_t size)
{
    socklen_t addrsize;

    switch (af) {
        case AF_INET: addrsize = sizeof(struct in_addr); break;
        case AF_INET6: addrsize = sizeof(struct in6_addr); break;
        default: return 0;
    }

    /* need at least 2 chars for hex and 1 for '\0' */
    while (size >= 3 && addrsize > 0) {
        snprintf(dst, size, "%02X", *(char*)src & 0xff);
        dst += 2;
        size -= 2;
        src = (char*)src+1;
        addrsize--;
    }
    *dst = '\0';
    return --size;
}

/* given sockaddr, prints the addr:port in hex,
 * uses the /proc/net/{tcp,tcp6,udp,udp6} endianess */
static void hex_addr(struct sockaddr *src, char *dst, size_t size)
{
    union {
        struct sockaddr sa;
        struct sockaddr_in in;
        struct sockaddr_in6 in6;
    } saddr;

    switch (src->sa_family) {
        case AF_INET:
            memcpy(&saddr.in, src, sizeof(struct sockaddr_in));
            htonl_arr((uint32_t *)&saddr.in.sin_addr,
                      sizeof(saddr.in.sin_addr)/sizeof(uint32_t));
            size = hex_ntop(AF_INET, &saddr.in.sin_addr, dst, size);
            snprintf(dst+strlen(dst), size+1, ":%04X",
                     ntohs(saddr.in.sin_port));
            break;
        case AF_INET6:
            memcpy(&saddr.in6, src, sizeof(struct sockaddr_in6));
            htonl_arr((uint32_t *)&saddr.in6.sin6_addr,
                      sizeof(saddr.in6.sin6_addr)/sizeof(uint32_t));
            size = hex_ntop(AF_INET6, &saddr.in6.sin6_addr, dst, size);
            snprintf(dst+strlen(dst), size+1, ":%04X",
                     ntohs(saddr.in6.sin6_port));
            break;
        default:
            return;
    }
}

/* given an addr:port hex string, find a matching socket nr.
 * from /proc/net/{tcp,tcp6,udp,udp6}
 * (as we cannot return error indication, we simply use errno) */
static unsigned long find_socket(int family, int proto, char *addrport)
{
    int rc;
    char fname[15] = "/proc/net/";
    FILE *f;
    int c;
    char readaddr[38]; /* in hex: 32 addr, :, 4 port, \0 */
    unsigned long inode;

    /* construct filename */
    switch (proto) {
        case IPPROTO_TCP: strcat(fname, "tcp"); break;
        case IPPROTO_UDP: strcat(fname, "udp"); break;
        default: errno = EINVAL; return 0;
    }
    switch (family) {
        case AF_INET: break;
        case AF_INET6: strcat(fname, "6"); break;
        default: errno = EINVAL; return 0;
    }

    f = fopen(fname, "r");
    if (!f)
        return 0;

    /* skip table header (column names) */
    while ((c = getc(f)) != EOF && c != '\n');

    while((rc = fscanf(f, "%*u: %s %*s %*X %*s %*s %*s %*s %*s %lu ",
                       readaddr, &inode)) != EOF && rc == 2) {
        if (!strcmp(addrport, readaddr)) {
            fclose(f);
            return inode;
        }
        while ((c = getc(f)) != EOF && c != '\n');
    }

    errno = ENOENT;
    return 0;
}

/* callback, looks if the process (child) listens on a given socket
 * (passed as unsigned long via socketptr) and if it does, kills it */
static void kill_with_socket(pid_t child, void *socketptr)
{
    int rc;
    unsigned long socket = *(unsigned long*)socketptr, read_socket;
    char fddirname[20];  /* "/proc/<I_HERE>/fd" + \0 */
    char sockname[30];   /* "socket:[<UL_here>]" + \0 */
    DIR *fds;
    struct dirent *entry;
    ssize_t link_bytes;

    snprintf(fddirname, sizeof(fddirname), "/proc/%d/fd", child);

    fds = opendir(fddirname);
    if (fds == NULL)
        return;

    while ((entry = readdir(fds)) != NULL) {
        if (entry->d_type != DT_LNK)
            continue;

        link_bytes = readlinkat(dirfd(fds), entry->d_name,
                                sockname, sizeof(sockname));
        if (link_bytes == -1)
            continue;
        sockname[link_bytes] = '\0';

        if ((rc = sscanf(sockname, "socket:[%lu]", &read_socket)) == EOF
                || rc != 1)
            continue;

        if (read_socket == socket) {
            kill(-child, SIGKILL);
            kill(child, SIGKILL);
            /* zombie collection done by the parent */
            break;
        }
    }

    closedir(fds);
}

static int parse(int argc, char **argv, struct session_info *info)
{
    int rc, port;
    char addrport[38]; /* in hex: 32 addr, :, 4 port, \0 */
    unsigned long socketnr;

    union {
        struct sockaddr sa;
        struct sockaddr_in in;
        struct sockaddr_in6 in6;
    } saddr;

    if (argc < 2)
        return 1;

    port = atoi(argv[1]);
    if (port <= 0)
        return 1;

    /* use addr from our control conn, port from the arg */
    socklen_t slen = sizeof(saddr);
    getsockname(info->sock, (struct sockaddr *)&saddr, &slen);
    switch (saddr.sa.sa_family) {
        case AF_INET:   saddr.in.sin_port = htons(port); break;
        case AF_INET6:  saddr.in6.sin6_port = htons(port); break;
        default:        return 1;
    }
    /* construct addr:port hex part from it */
    hex_addr((struct sockaddr *)&saddr, addrport, sizeof(addrport));

    /* find corresponding (global) socket descriptor */
    errno = 0;
    socketnr = find_socket(saddr.sa.sa_family, IPPROTO_TCP, addrport);
    if (errno) {
        perror("find_socket");
        return 1;
    }
    /* without associated inode - probably in TIME_WAIT, without process,
     * nothing to kill here */
    if (socketnr == 0)
        return 0;

    /* look for a process matching the socket, kill it */
    rc = traverse_children(getppid(), kill_with_socket, &socketnr);
    if (!rc)
        return 1;  /* no process found */

    return 0;
}

static __newcmd struct cmd_desc cmd = {
    .name = "ctl-kill",
    .parse = parse,
};
