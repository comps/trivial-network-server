/*
 * per-client code, gets invoked after accepting client connection and takes
 * care of control cmdline parsing, command execution, etc.,
 * basically per-client stuff
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/file.h>

#include "shared.h"

/* maximum size of a control cmdline (with commands for server) */
#define CTL_CMDLINE_MAX 4096
/* maximum number of bytes the in-kernel socket buffer can hold for us,
 * used for MSG_PEEK, can be probably much higher, but don't risk anything */
#define MSG_PEEK_MAX 16
/* maximum size of the control socket output buffer, which holds command
 * names and their return values for delayed retrieval */
#define CTL_OUTBUFF_MAX 8192

/* cmd linked list management - the input control cmdline is parsed and
 * transformed using these functions into a linked list before execution */
static struct cmd *
cmd_list_add(struct cmd *c, int argc, char **argv, struct cmd_desc *desc)
{
    struct cmd *newcmd;

    newcmd = xmalloc(sizeof(struct cmd));
    memset(newcmd, 0, sizeof(struct cmd));
    newcmd->argc = argc;
    newcmd->argv = argv;
    newcmd->desc = desc;

    if (!c) {
        newcmd->prev = newcmd->next = NULL;
    } else {
        newcmd->prev = c;
        c->next = newcmd;
    }

    return newcmd;
}
static struct cmd *cmd_list_rewind(struct cmd *end)
{
    while (end && end->prev)
        end = end->prev;
    return end;
}

/* create argv array based on delims-separated string,
 * remember to free() it ! */
static int parse_argv(char ***argv, char *string, char *delims)
{
    int argc = 0;
    char *arg;

    *argv = NULL;
    while ((arg = strsep(&string, delims))) {
        argc++;
        *argv = xrealloc(*argv, sizeof(char*) * argc);
        (*argv)[argc-1] = arg;
    }
    return argc;
}

/* find cmd_desc struct based on name */
static struct cmd_desc *find_cmd_desc(char *name)
{
    struct cmd_desc *ptr = NULL;
    while ((ptr = cmd_descs_iterate(ptr)) != NULL)
        if (!strcmp(ptr->name, name))
            break;
    return ptr;
}

/* parse cmd structures (with argc/argv) out of a control cmdline into a list */
static struct cmd *parse_cmds(char *cmdline)
{
    char *cmd_args;
    struct cmd *cmd = NULL;

    int argc;
    char **argv;
    struct cmd_desc *desc;

    while ((cmd_args = strsep(&cmdline, ";"))) {
        if (!*cmd_args)
            continue;  /* silently skip empty cmds */
        argc = parse_argv(&argv, cmd_args, ",");
        if (!argc)
            error_down("cmd args empty\n");
        desc = find_cmd_desc(argv[0]);
        if (!desc)
            error_down("cmd not found: %s\n", argv[0]);
        cmd = cmd_list_add(cmd, argc, argv, desc);
    }

    /* first cmd of the list */
    cmd = cmd_list_rewind(cmd);
    return cmd;
}

/* based on an argc+argv, rebuild the original `,'-separated command */
static char *rebuild_args(int argc, char **argv)
{
    int i, len = 0;
    char *args;
    for (i = 0; i < argc; i++)
        len += strlen(argv[i]) + 1; /* for `,' */
    args = xmalloc(len);
    *args = '\0';
    for (i = 0; i < argc; i++) {
        strcat(args, argv[i]);
        if (i < argc-1)
            strcat(args, ",");
    }
    return args;
}

/* append printf-formatted string to the buffer, but only if the entire
 * printed expression fits the size, return value is identical to snprintf */
static int snprintfcat(char *str, size_t size, const char *format, ...)
{
    va_list ap;
    int rc;
    size_t currlen;

    /* skip to string end */
    currlen = strlen(str);
    str += currlen;
    size -= currlen;

    /* "size" for snprintf is incl. \0, IOW with "abcd",3 it writes "ab\0" */
    va_start(ap, format);
    rc = vsnprintf(str, size, format, ap);
    va_end(ap);
    if (rc < 0)
        return rc;

    /* "return value of size or more means that the output was truncated" */
    /* (rc was checked above to be >=0) */
    if ((size_t)rc >= size)
        /* avoid partial printouts */
        *str = '\0';

    return rc;
}

/* parse the control cmdline supplied by client, call individual cmds */
static void process_cmds(struct session_info *sinfo, struct cmd *cmd)
{
    int rc;
    char *args;
    struct cmd *tmp = NULL;

    /* for each command */
    while (cmd) {
        sinfo->cmd = cmd;
        /* call the parser */
        rc = cmd->desc->parse(cmd->argc, cmd->argv, sinfo);
        if (rc < 0)
            error_down("cmd %s raised a fatal error\n", cmd->argv[0]);
        /* append "$rc $name" to the ctl outbuff */
        args = rebuild_args(cmd->argc, cmd->argv);
        snprintfcat(sinfo->ctl_outbuff, CTL_OUTBUFF_MAX, "%d %s\n", rc, args);
        free(args);
        /* next command */
        tmp = cmd;
        cmd = cmd->next;
    }

    /* free allocated structures */
    cmd = cmd_list_rewind(tmp);
    while (cmd) {
        free(cmd->argv);
        tmp = cmd->next;
        free(cmd);
        cmd = tmp;
    }
}

/* read from a socket until delim is found/read
 *
 * we carefuly read up to buffsiz bytes from the socket, MSG_PEEK_MAX bytes
 * at a time, peeking each time for a delimiter, so that we don't read past it
 * - we could theoretically keep peeking always from the start, but that would
 *   put too much pressure on the kernel socket buffer, potentially never
 *   returning buffsiz (maximum number of) bytes */
static ssize_t
sockread_until(int fd, char *rcvbuff, size_t buffsiz, char delimchr)
{
    char *buffhead, *delim;
    ssize_t read;

    buffhead = rcvbuff;
    while (buffsiz > 0) {
        /* peek, look for delim */
        read = xrecv(fd, rcvbuff, buffsiz, MSG_PEEK);
        delim = memchr(rcvbuff, delimchr, read);
        if (!delim) {
            /* not found - the end is not near,
             * fully read what we just peeked */
            read = xrecv(fd, rcvbuff, buffsiz, MSG_WAITALL);
            rcvbuff += read;
            buffsiz -= read;
        } else {
            /* found - finish reading,
             * read everything up to and incl. the delimiter */
            read = xrecv(fd, rcvbuff, delim-rcvbuff+1, MSG_WAITALL);
            rcvbuff += read;
            buffsiz -= read;
            return rcvbuff-buffhead;  /* total read */
        }
    }

    /* out of buffer space */
    return -1;
}

/* read exactly one line from socket, take care of client-specifics */
static ssize_t sockread_line(int fd, char *rcvbuff, size_t buffsiz)
{
    ssize_t linesz;

    linesz = sockread_until(fd, rcvbuff, buffsiz, '\n');
    if (linesz == -1)
        error_down("read line too big (%d+ bytes)\n", buffsiz);
    else if (linesz < 1)
        error_down("read line is empty\n");
    rcvbuff[linesz-1] = '\0';

    /* for telnet & others - if next-to-last chr is \r, zero it too */
    if (linesz > 1 && rcvbuff[linesz-2] == '\r') {
        linesz--;
        rcvbuff[linesz-1] = '\0';
    }

    return linesz;
}

/* copy a (possibly connected) socket into a new, listening one, copying the
 * addr structure and selecting a random port to listen on */
static int sock_ctol(int sock, int backlog, int *port)
{
    union {
        struct sockaddr sa;
        struct sockaddr_in in;
        struct sockaddr_in6 in6;
    } saddr;
    socklen_t slen;
    int lsock, domain, type, proto;

    slen = sizeof(int);
    if (getsockopt(sock, SOL_SOCKET, SO_DOMAIN, &domain, &slen) == -1)
        return -1;
    slen = sizeof(int);
    if (getsockopt(sock, SOL_SOCKET, SO_TYPE, &type, &slen) == -1)
        return -1;
    slen = sizeof(int);
    if (getsockopt(sock, SOL_SOCKET, SO_PROTOCOL, &proto, &slen) == -1)
        return -1;
    slen = sizeof(saddr);
    if (getsockname(sock, (struct sockaddr *)&saddr, &slen) == -1)
        return -1;

    switch (saddr.sa.sa_family) {
        case AF_INET:   saddr.in.sin_port = 0; break;
        case AF_INET6:  saddr.in6.sin6_port = 0; break;
        default:        return -1;
    }

    lsock = socket(domain, type, proto);
    if (lsock < 0)
        return -1;

    if (bind(lsock, &saddr.sa, slen) == -1)
        goto err;

    if (type == SOCK_STREAM || type == SOCK_SEQPACKET) {
        if (listen(lsock, backlog) == -1)
            goto err;
    }

    if (port) {
        slen = sizeof(saddr);
        if (getsockname(lsock, (struct sockaddr *)&saddr, &slen) == -1)
            goto err;
        switch (saddr.sa.sa_family) {
            case AF_INET:   *port = ntohs(saddr.in.sin_port); break;
            case AF_INET6:  *port = ntohs(saddr.in6.sin6_port); break;
            default:        goto err;
        }
    }

    return lsock;

err:
    close(lsock);
    return -1;
}

/* handle basic input/output client interaction */
void process_client(int clientfd)
{
    char buff[CTL_CMDLINE_MAX];
    struct session_info sinfo;
    struct cmd *cmds;
    int lsock, lport;

    /* let client detect unexpected errors (as conn reset) */
    linger(clientfd, 0);

    sockread_line(clientfd, buff, sizeof(buff));

    /* prepare shared session info */
    memset(&sinfo, 0, sizeof(struct session_info));
    sinfo.sock = clientfd;
    sinfo.ctl_outbuff = xmalloc(CTL_OUTBUFF_MAX);
    *sinfo.ctl_outbuff = '\0';

    /* new session requested - multi-cmdline client */
    if (!strcmp(buff, "SESSION")) {
        /* start listening, send port nr. to client */
        lsock = sock_ctol(clientfd, 10, &lport);
        if (lsock == -1)
            perror_down("session listener");
        dprintf(clientfd, "%d\n", lport);
        linger(clientfd, 1);
        close(clientfd);

        sinfo.active = 1;
        while (sinfo.active) {
            sinfo.sock = accept(lsock, NULL, NULL);
            if (sinfo.sock == -1)
                perror("session accept");
            linger(sinfo.sock, 0);
            /* read control cmdline */
            sockread_line(sinfo.sock, buff, sizeof(buff));
            /* parse it into cmds */
            cmds = parse_cmds(buff);
            if (!cmds)
                error_down("no cmds on the ctl cmdline\n");
            /* execute them */
            process_cmds(&sinfo, cmds);
            linger(sinfo.sock, 1);
            close(sinfo.sock);
        }

    /* simple single-cmdline client */
    } else {
        /* parse previously read control cmdline into cmds */
        cmds = parse_cmds(buff);
        if (!cmds)
            error_down("no cmds on the ctl cmdline\n");
        /* execute them */
        process_cmds(&sinfo, cmds);
        linger(sinfo.sock, 1);
        close(sinfo.sock);
    }

    free(sinfo.ctl_outbuff);
}
