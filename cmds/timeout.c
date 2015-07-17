#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include "shared.h"

/* args:
 *   timeout,<secs>   # secs == 0 cancels the timeout
 */

static void handle_exit(int signum)
{
    UNUSED(signum);
    /* kernel should close the client sock */
    _exit(EXIT_SUCCESS);
}

int death_timer(int seconds)
{
    struct sigaction action;

    memset(&action, 0, sizeof(struct sigaction));

    if (seconds < 0)
        seconds = 0;

    if (!seconds) {
        action.sa_handler = SIG_DFL;
    } else {
        action.sa_handler = handle_exit;
    }

    if (sigaction(SIGALRM, &action, NULL) == -1)
        return -1;

    return alarm(seconds);
}

static int parse(int argc, char **argv, struct session_info *info)
{
    UNUSED(info);

    if (argc < 2)
        return 1;

    if (death_timer(atoi(argv[1])) == -1)
        return 1;

    return 0;
}

static __newcmd struct cmd_desc cmd = {
    .name = "timeout",
    .parse = parse,
};
