#include <stdlib.h>
#include <unistd.h>
#include "shared.h"

static int parse(int argc, char **argv, struct client_info *c)
{
    UNUSED(c);
    if (argc < 2)
        return 1;
    sleep(atoi(argv[1]));
    return 0;
}

static __newcmd struct cmd_info cmd = {
    .name = "sleep",
    .parse = parse,
};
