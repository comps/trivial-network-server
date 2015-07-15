#include <stdio.h>
#include "shared.h"

static int parse(int argc, char **argv, struct session_info *info)
{
    UNUSED2(argc, argv);
    char addr[REMOTE_ADDRA_MAX];
    if (remote_addra(info->sock, addr) == -1)
        return 1;
    dprintf(info->sock, "%s\n", addr);
    return 0;
}

static __newcmd struct cmd_desc cmd = {
    .name = "myaddr",
    .parse = parse,
};
