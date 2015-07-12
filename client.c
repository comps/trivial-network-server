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
#include <sys/file.h>

#include "shared.h"

/* maximum size of a control cmdline (with commands for server) */
#define CTL_CMDLINE_MAX 4096
/* maximum number of bytes the in-kernel socket buffer can hold for us,
 * used for MSG_PEEK, can be probably much higher, but don't risk anything */
#define MSG_PEEK_MAX 16
/* maximum size of the control socket return buffer, which holds command
 * names and their return values when we can't send these right away */
#define CTL_OUTBUFF_MAX 8192

/* cmd linked list management - the input control cmdline is parsed and
 * transformed using these functions into a linked list before execution */
static struct cmd *
cmd_list_add(struct cmd *c, int argc, char **argv, struct cmd_desc *desc)
{
    struct cmd *newcmd;

    newcmd = xmalloc(sizeof(struct cmd));
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
    char *arg, *tmp;

    *argv = NULL;
    arg = strtok_r(string, delims, &tmp);
    while (arg) {
        argc++;
        *argv = xrealloc(*argv, sizeof(char*) * argc);
        (*argv)[argc-1] = arg;
        arg = strtok_r(NULL, delims, &tmp);
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
    char *cmd_args, *tmp;
    struct cmd *cmd = NULL;

    int argc;
    char **argv;
    struct cmd_desc *desc;

    cmd_args = strtok_r(cmdline, ";", &tmp);
    while (cmd_args) {
        argc = parse_argv(&argv, cmd_args, ",");
        if (!argc)
            error_down("cmd args empty\n");
        desc = find_cmd_desc(argv[0]);
        if (!desc)
            error_down("cmd not found: %s\n", argv[0]);
        cmd = cmd_list_add(cmd, argc, argv, desc);
        cmd_args = strtok_r(NULL, ";", &tmp);
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

/* write (fully or partially) a string to a socket, remove the written bytes
 * from the string (shifting it to the left) */
static ssize_t write_str(int fd, char *str)
{
    ssize_t rc;
    size_t len = strlen(str);
    rc = write(fd, str, len);
    /* on successful write, remove written bytes */
    if (rc != -1)
        memmove(str, str+rc, len-rc+1);  /* +1 is for '\0' */
    return rc;
}

/* parse the control cmdline supplied by client, call individual cmds */
static void process_ctl_cmdline(int clientfd, char *cmdline)
{
    int rc;
    char ctl_buff[CTL_OUTBUFF_MAX];

    char *args;
    struct cmd *cmd, *tmp = NULL;
    struct session_info info;

    *ctl_buff = '\0';

    /* parse the control cmdline into a list */
    cmd = parse_cmds(cmdline);
    if (!cmd)
        error_down("no cmds on the ctl cmdline\n");

    /* prepare shared session info */
    memset(&info, 0, sizeof(struct session_info));
    info.sock = clientfd;
    info.sock_mode = CTL_MODE_CONTROL;

    /* for each command */
    while (cmd) {
        info.cmd = cmd;
        /* call the parser */
        rc = cmd->desc->parse(cmd->argc, cmd->argv, &info);
        /* append "$rc $name" to the ctl outbuff and send it out */
        args = rebuild_args(cmd->argc, cmd->argv);
        snprintfcat(ctl_buff, sizeof(ctl_buff), "%d %s\n", rc, args);
        free(args);
        if (info.sock_mode == CTL_MODE_CONTROL && info.sock != -1)
            write_str(info.sock, ctl_buff);
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

/* handle basic input/output client interaction */
void process_client(int clientfd)
{
    char buff[CTL_CMDLINE_MAX];
    ssize_t linesz;

    /* make any unexpected connection termination result in (hopefully) TCP RST
     * sent to the client, hoping the client can detect the unexpected error */
    setsockopt(clientfd, SOL_SOCKET, SO_LINGER,
               &(struct linger){1, 0}, sizeof(struct linger));

    /* read the control cmdline, zero the delimiter(s) */
    linesz = sockread_until(clientfd, buff, sizeof(buff), '\n');
    if (linesz == -1)
        error_down("control cmdline too big (%d+ bytes)\n", sizeof(buff));
    /* FIXME: should be buff[linesz-1], but gcc 4.8 doesn't like it,
     * despite linesz being always >0 here */
    *(buff+linesz-1) = '\0';
    /* for telnet & others - if next-to-last chr is \r, zero it too */
    if (linesz > 1 && buff[linesz-2] == '\r')
        buff[linesz-2] = '\0';

    /* parse the null-terminated control cmdline we just read */
    process_ctl_cmdline(clientfd, buff);

    shutdown(clientfd, SHUT_RDWR);
    close(clientfd);
}
