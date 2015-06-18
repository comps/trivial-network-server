#include <stdio.h>
#include <unistd.h>
#include "shared.h"

static int parse(int argc, char **argv, struct client_info *c)
{
    UNUSED2(argc, argv);
    ssize_t bytes;
    char buf[128];

    if (c->sock == -1)
        return 1;

    dprintf(c->sock, "cat: cating data now!\n"); 

    while ((bytes = read(c->sock, buf, sizeof(buf))) > 0)
        write(c->sock, buf, bytes);

    return 0;
}

static __newcmd struct cmd_info cmd = {
    .name = "cat",
    .parse = parse,
};
