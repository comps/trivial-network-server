#include <stdio.h>
#include "shared.h"

/*
 * list all available commands by going through the command ELF data section
 */

static int parse(int argc, char **argv, struct session_info *info)
{
    UNUSED2(argc, argv);
    struct cmd_desc *cmd = NULL;
    while ((cmd = cmd_descs_iterate(cmd)) != NULL)
        dprintf(info->sock, "> %s\n", cmd->name);
    return 0;
}

static __newcmd struct cmd_desc cmd = {
    .name = "cmds",
    .parse = parse,
};
