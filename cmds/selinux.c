#include <stdio.h>
#include <string.h>
#include <selinux/selinux.h>
#include "shared.h"

/* args:
 *   setsockcreatecon,<fullcontext>
 *   getcon,[raw]
 */

static int cmd_setsockcreatecon(int argc, char **argv, struct client_info *c)
{
    UNUSED(c);
    if (argc < 2)
        return 1;

    if (setsockcreatecon(argv[1]) == -1)
        return 1;

    return 0;
}

static int cmd_getcon(int argc, char **argv, struct client_info *c)
{
    char *ctx;

    if (argc >= 2 && !strcmp(argv[1], "raw")) {
        if (getcon_raw(&ctx) == -1)
            return 1;
    } else {
        if (getcon(&ctx) == -1)
            return 1;
    }

    dprintf(c->sock, "%s\n", ctx);

    freecon(ctx);

    return 0;
}

static __newcmd struct cmd_info cmd1 = {
    .name = "setsockcreatecon",
    .parse = cmd_setsockcreatecon,
};
static __newcmd struct cmd_info cmd2 = {
    .name = "getcon",
    .parse = cmd_getcon,
};
