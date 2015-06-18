#include <stdio.h>
#include "shared.h"

/*
 * list all available commands by going through the command ELF data section
 */

static int parse(int argc, char **argv, struct client_info *c)
{
    UNUSED2(argc, argv);
    struct cmd_info *cmd = NULL;
    while ((cmd = cmds_iterate(cmd)) != NULL)
        dprintf(c->sock, "> %s\n", cmd->name);
    return 0;
}

static __newcmd struct cmd_info cmd = {
    .name = "cmds",
    .parse = parse,
};
