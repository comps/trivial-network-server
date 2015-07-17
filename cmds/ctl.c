#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "shared.h"

/*
 * various helpers for managing the control cmdline (user interaction)
 *
 * status:
 * - print out the control socket output buffer (cmd return values)
 *
 * loop:
 * - loops last [lastcmds] commands [loops] times
 *
 * detach:
 * - close the client socket so that it "detaches"
 *
 * end:
 * - mark the session as inactive, causing it to end after the current
 *   ctl cmdline finishes
 */

/* args:
 *   ctl-status
 *   ctl-loop,[loops],[lastcmds]  # "loops" defaults to -1 (infinite loop),
 *                                # "lastcmds" defaults to all previous
 *   ctl-detach
 *   ctl-end
 */

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

static int cmd_status(int argc, char **argv, struct session_info *info)
{
    UNUSED2(argc, argv);
    if (info->sock == -1)
        return 1;

    write_str(info->sock, info->ctl_outbuff);

    return 0;
}

struct cmd_loop_data { int loopcnt; void *orignext; };
static int cmd_loop(int argc, char **argv, struct session_info *info)
{
    int jmpcmds;
    struct cmd_loop_data *data;
    struct cmd *start;

    /* looping */
    data = info->cmd->custom_data;
    if (data) {
        if (data->loopcnt < 0) {
            return 0;  /* infinite */
        } else if (data->loopcnt <= 1) {
            info->cmd->custom_data = NULL;
            info->cmd->next = data->orignext;
            free(data);
            return 0;
        } else {
            data->loopcnt--;
            return 0;
        }
    }

    /* setup */
    data = xmalloc(sizeof(struct cmd_loop_data));

    if (argc >= 2) {
        data->loopcnt = atoi(argv[1]);
        if (!data->loopcnt) {
            free(data);
            return 0;  /* 0 loops */
        }
    } else {
        data->loopcnt = -1;
    }

    data->orignext = info->cmd->next;
    if (argc >= 3) {
        jmpcmds = atoi(argv[2]);
        for (start = info->cmd; jmpcmds-- && start->prev; start = start->prev);
    } else {
        for (start = info->cmd; start->prev; start = start->prev);
    }
    info->cmd->next = start;

    info->cmd->custom_data = data;
    return 0;
}

static int cmd_detach(int argc, char **argv, struct session_info *info)
{
    UNUSED2(argc, argv);
    if (info->sock == -1)
        return 1;

    linger(info->sock, 1);
    close(info->sock);
    info->sock = -1;
    return 0;
}

static int cmd_end(int argc, char **argv, struct session_info *info)
{
    UNUSED2(argc, argv);
    info->active = 0;
    return 0;
}

static __newcmd struct cmd_desc cmd1 = {
    .name = "ctl-status",
    .parse = cmd_status,
};
static __newcmd struct cmd_desc cmd2 = {
    .name = "ctl-loop",
    .parse = cmd_loop,
};
static __newcmd struct cmd_desc cmd3 = {
    .name = "ctl-detach",
    .parse = cmd_detach,
};
static __newcmd struct cmd_desc cmd4 = {
    .name = "ctl-end",
    .parse = cmd_end,
};
