#include <stdio.h>
#include <unistd.h>
#include "shared.h"

static int parse(int argc, char **argv, struct session_info *info)
{
    int i;

    if (info->sock == -1)
        return 1;

    for (i = 1; i < argc; i++)
        dprintf(info->sock, "%s\n", argv[i]);

    return 0;
}

static __newcmd struct cmd_desc cmd = {
    .name = "echo",
    .parse = parse,
};
