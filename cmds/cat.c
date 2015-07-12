#include <stdio.h>
#include <unistd.h>
#include "shared.h"

static int parse(int argc, char **argv, struct session_info *info)
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

static __newcmd struct cmd_desc cmd = {
    .name = "cat",
    .parse = parse,
};
