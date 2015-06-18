/*
 * per-client code, gets invoked after accepting client connection and takes
 * care of control cmdline parsing, command execution, etc.,
 * basically per-client stuff
 *
 * overview, for readability:
 *   process_client() - our "main", gets called from outside
 *   `-> parse_ctl_cmdline() - chops cmdline into commands with args (`;')
 *       `-> parse_cmd() - chops args (`,') and calls the cmd->parse() func
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

/* create argv array based on delim-separated string,
 * remember to free() it ! */
int parse_args(char ***argv, char *string, char delim)
{
    int argc, i;
    char *s;

    /* count args, start from 1 (counting words, not commas) */
    s = string;
    for (argc = 1; *s != '\0'; argc += !!(*s == delim), s++);

    *argv = xmalloc(sizeof(char*) * argc);
    *argv[0] = string;

    /* parse/modify the string itself, manually
     * (strtok can't handle empty arguments) */
    s = string;
    for (i = 1; *s != '\0'; s++) {
        if (*s == delim) {
            *s = '\0';
            (*argv)[i++] = s+1;
        }
    }

    return argc;
}

/* find cmd_info struct based on name */
static struct cmd_info *find_cmd(char *name)
{
    struct cmd_info *ptr = NULL;
    while ((ptr = cmds_iterate(ptr)) != NULL)
        if (!strcmp(ptr->name, name))
            break;
    return ptr;
}

/* based on a `;'-separated portion of the control cmdline, call/wrap command
 * parse functions, preparing argv, looking for cmd name, etc. */
static int parse_cmd(char *cmdline_part, struct client_info *cl)
{
    struct cmd_info *cmdinfo;
    int cmd_argc;
    char **cmd_argv, *cmdline_part_copy;
    int rc;

    /* don't modify paren't copy of cmdline_part */
    cmdline_part_copy = xmalloc(strlen(cmdline_part)+1);
    strcpy(cmdline_part_copy, cmdline_part);

    /* parse cmdline part into cmd name + args */
    /* (there's always going to be at least argv[0] because our parent func
     * skips empty `;'-separated strings) */
    cmd_argc = parse_args(&cmd_argv, cmdline_part_copy, ',');

    /* find a command (handler), fail if none found */
    cmdinfo = find_cmd(cmd_argv[0]);
    if (!cmdinfo) {
        error("cmd not found: %s\n", cmd_argv[0]);
        rc = -1;
        goto ret;
    }

    /* call the handler */
    rc = cmdinfo->parse(cmd_argc, cmd_argv, cl);

ret:
    free(cmd_argv);
    free(cmdline_part_copy);
    return rc;
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
ssize_t write_str(int fd, char *str)
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
static void parse_ctl_cmdline(int clientfd, char *cmdline)
{
    int rc;
    char ctl_buff[CTL_OUTBUFF_MAX];

    int i;
    int line_argc;
    char **line_argv;
    struct client_info clinfo;

    *ctl_buff = '\0';
    memset(&clinfo, 0, sizeof(struct client_info));
    clinfo.sock = clientfd;
    clinfo.sock_mode = CTL_MODE_CONTROL;

    /* parse control cmdline into commands */
    line_argc = parse_args(&line_argv, cmdline, ';');

    /* for each command */
    for (i = 0; i < line_argc; i++) {
        /* skip if empty, ie. ';' at the end */
        if (*line_argv[i] == '\0')
            continue;

        /* call the cmd, if any matches */
        rc = parse_cmd(line_argv[i], &clinfo);
        if (rc < 0)
            break;

        /* append "$rc $name" to the ctl outbuff and send it out */
        snprintfcat(ctl_buff, sizeof(ctl_buff), "%d %s\n", rc, line_argv[i]);
        if (clinfo.sock_mode == CTL_MODE_CONTROL && clinfo.sock != -1)
            write_str(clinfo.sock, ctl_buff);
    }

    free(line_argv);
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

/* handle basic input/output client interaction of control cmdlines */
void process_client(int clientfd)
{
    char buff[CTL_CMDLINE_MAX];
    ssize_t linesz;

    /* take a shared server lock, wait for exclusive clients to finish */
    if (srvlock(LOCK_SH) == -1)
        perror_down("srvlock");

    /* read the control cmdline, zero the delimiter(s) */
    linesz = sockread_until(clientfd, buff, sizeof(buff), '\n');
    if (linesz == -1)
        error_down("control cmdline too big (%d+ bytes)\n", sizeof(buff));
    buff[linesz-1] = '\0';
    /* for telnet & others - if next-to-last chr is \r, zero it too */
    if (linesz > 1 && buff[linesz-2] == '\r')
        buff[linesz-2] = '\0';

    /* parse the null-terminated control cmdline we just read */
    parse_ctl_cmdline(clientfd, buff);

    srvlock(LOCK_UN);
    shutdown(clientfd, SHUT_RDWR);
    close(clientfd);
}
