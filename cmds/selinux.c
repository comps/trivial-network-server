#include <stdio.h>
#include <string.h>
#include <selinux/selinux.h>
#include "shared.h"

/* args:
 *   setsockcreatecon,<fullcontext>
 *   getcon,[raw]
 */

static int
cmd_setsockcreatecon(int argc, char **argv, struct session_info *info)
{
    UNUSED(info);
    if (argc < 2)
        return 1;

    if (setsockcreatecon(argv[1]) == -1)
        return 1;

    return 0;
}

static int cmd_getcon(int argc, char **argv, struct session_info *info)
{
    char *ctx;

    if (argc >= 2 && !strcmp(argv[1], "raw")) {
        if (getcon_raw(&ctx) == -1)
            return 1;
    } else {
        if (getcon(&ctx) == -1)
            return 1;
    }

    dprintf(info->sock, "%s\n", ctx);

    freecon(ctx);

    return 0;
}

static __newcmd struct cmd_desc cmd1 = {
    .name = "setsockcreatecon",
    .parse = cmd_setsockcreatecon,
};
static __newcmd struct cmd_desc cmd2 = {
    .name = "getcon",
    .parse = cmd_getcon,
};
