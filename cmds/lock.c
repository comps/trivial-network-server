#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/file.h>
#include "shared.h"

/* gain/release exclusive lock over the entire server */

/* args:
 *   lock,[nonblock]
 *   unlock
 */

static int cmd_lock(int argc, char **argv, struct client_info *c)
{
    UNUSED(c);

    /* upgrade existing shared lock to exclusive lock,
     * let other clients finish */

    if (argc > 1 && !strcmp(argv[1], "nonblock")) {
        /* don't block / wait */
        if (srvlock(LOCK_EX | LOCK_NB) == -1) {
            if (errno == EWOULDBLOCK)
                return 2;  /* lock is already held */
            else
                return 1;
        }
    } else {
        /* wait */
        if (srvlock(LOCK_EX) == -1)
            return 1;
    }

    return 0;
}

static int cmd_unlock(int argc, char **argv, struct client_info *c)
{
    UNUSED3(argc, argv, c);

    /* downgrade an exclusive lock back to shared,
     * note that this is not atomic - we might block if some other client
     * manages to get exclusive lock after we release ours, but before we
     * get a shared lock - in which case we *have to* wait (no nonblock) */

    if (srvlock(LOCK_SH) == -1) {
        /* fatal: we can't afford to run more commands if we don't have
         * any lock */
        perror("srvlock");
        return -1;
    }

    return 0;
}

static __newcmd struct cmd_info cmd1 = {
    .name = "lock",
    .parse = cmd_lock,
};
static __newcmd struct cmd_info cmd2 = {
    .name = "unlock",
    .parse = cmd_unlock,
};
