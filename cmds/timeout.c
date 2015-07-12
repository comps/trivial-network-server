#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include "shared.h"

/* args:
 *   timeout,<secs|stop>   # "stop" cancels the timeout
 */

static void handle_exit(int signum)
{
    UNUSED(signum);
    /* kernel should close the client sock */
    _exit(EXIT_SUCCESS);
}

static int parse(int argc, char **argv, struct session_info *info)
{
    UNUSED(info);
    int ret, secs;
    struct sigaction action;

    if (argc < 2)
        return 1;

    memset(&action, 0, sizeof(struct sigaction));

    if (!strcmp(argv[1], "stop")) {
        action.sa_handler = SIG_DFL;
        secs = 0;  /* disable any existing alarm */
    } else {
        action.sa_handler = handle_exit;
        secs = atoi(argv[1]);
        if (secs <= 0)
            return 1;
    }

    alarm(secs);

    ret = sigaction(SIGALRM, &action, NULL);
    if (ret == -1)
        return 1;

    return 0;
}

static __newcmd struct cmd_desc cmd = {
    .name = "timeout",
    .parse = parse,
};
