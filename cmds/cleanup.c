#include <unistd.h>
#include <signal.h>
#include "shared.h"

/* sends SIGHUP to the parent and gets SIGKILLed itself,
 * should never return */

/* DO NOT USE THIS COMMAND UNLESS YOU REALLY, REALLY KNOW THE IMPLICATIONS,
 * see README, ## CLEANUP ## section, at the bottom */

static int parse(int argc, char **argv, struct session_info *info)
{
    UNUSED3(argc, argv, info);
    linger(info->sock, 1);
    kill(getppid(), SIGHUP);
    
    /* there's nothing more we can reliably do here, we might have enough
     * CPU time to return and reply to the client, we might have not, so play
     * it safe and just wait for SIGKILL */
    pause();

    return 1;
}

static __newcmd struct cmd_desc cmd = {
    .name = "cleanup",
    .parse = parse,
};
