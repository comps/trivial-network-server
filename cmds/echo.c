#include <stdio.h>
#include <unistd.h>
#include "shared.h"

static int parse(int argc, char **argv, struct client_info *c)
{
    int i;

    if (c->sock == -1)
        return 1;

    for (i = 1; i < argc; i++)
        dprintf(c->sock, "%s\n", argv[i]);

    return 0;
}

static __newcmd struct cmd_info cmd = {
    .name = "echo",
    .parse = parse,
};
