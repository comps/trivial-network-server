#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "shared.h"

/*
 * various trivial and showcase / debug commands
 */

static int cmd_echo(int argc, char **argv, struct session_info *info)
{
    int i;

    if (info->sock == -1)
        return 1;

    for (i = 1; i < argc; i++)
        dprintf(info->sock, "%s\n", argv[i]);

    return 0;
}

static int cmd_cat(int argc, char **argv, struct session_info *info)
{
    UNUSED2(argc, argv);
    ssize_t bytes;
    char buf[128];

    if (info->sock == -1)
        return 1;

    dprintf(info->sock, "cat: cating data now!\n"); 

    while ((bytes = read(info->sock, buf, sizeof(buf))) > 0)
        write(info->sock, buf, bytes);

    return 0;
}

static int cmd_cmds(int argc, char **argv, struct session_info *info)
{
    UNUSED2(argc, argv);
    struct cmd_desc *cmd = NULL;
    while ((cmd = cmd_descs_iterate(cmd)) != NULL)
        dprintf(info->sock, "> %s\n", cmd->name);
    return 0;
}

static int cmd_noop(int argc, char **argv, struct session_info *info)
{
    UNUSED(info);
    if (argc >= 2)
        return atoi(argv[1]);
    return 0;
}

static __newcmd struct cmd_desc cmd1 = {
    .name = "echo",
    .parse = cmd_echo,
};
static __newcmd struct cmd_desc cmd2 = {
    .name = "cat",
    .parse = cmd_cat,
};
static __newcmd struct cmd_desc cmd3 = {
    .name = "cmds",
    .parse = cmd_cmds,
};
static __newcmd struct cmd_desc cmd4 = {
    .name = "noop",
    .parse = cmd_noop,
};
