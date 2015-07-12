#include <stdlib.h>
#include <unistd.h>
#include "shared.h"

static int parse(int argc, char **argv, struct session_info *info)
{
    UNUSED(info);
    if (argc < 2)
        return 1;
    sleep(atoi(argv[1]));
    return 0;
}

static __newcmd struct cmd_desc cmd = {
    .name = "sleep",
    .parse = parse,
};
