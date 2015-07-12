#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/file.h>
#include "shared.h"

#define GLOBAL_LOCK_FILE "/var/lock/ns2.lock"

/* gain/release or up/downgrade a lock over the entire server */

/* args:
 *   lock,[sh|ex],[nonblock]
 *   unlock
 */

static int lockfd = -1;
static int lock_type = 0;

static int cmd_lock(int argc, char **argv, struct session_info *info)
{
    UNUSED(info);
    int type = 0;

    if (argc > 1 && !strcmp(argv[1], "sh")) {
        type = LOCK_SH;
        argv++;
        argc--;
    } else if (argc > 1 && !strcmp(argv[1], "ex")) {
        type = LOCK_EX;
        argv++;
        argc--;
    }

    if (argc > 1 && !strcmp(argv[1], "nonblock")) {
        type |= LOCK_NB;
    }

    if (lockfd == -1) {
        lockfd = open(GLOBAL_LOCK_FILE,
                      O_RDONLY|O_CREAT|O_NOCTTY|O_CLOEXEC, 0600);
        if (lockfd == -1) {
            perror("lock/open");
            return -1;  /* dangerous to go on without a lock */
        }
    }

    /* default argument-less action:
     * - if no lock is held, get a shared one
     * - if a shared lock is held, upgrade to exclusive
     */
    if (!(type & (LOCK_SH | LOCK_EX))) {
        if (!lock_type)
            type |= LOCK_SH;
        else if (lock_type & LOCK_SH)
            type |= LOCK_EX;
    }

    if (flock(lockfd, type) == -1) {
        if (errno == EWOULDBLOCK)
            return 2;  /* lock is already held */
        else
            return -1;  /* dangerous to go on without a lock */
    }
    lock_type = type & (LOCK_SH | LOCK_EX);

    return 0;
}

static int cmd_unlock(int argc, char **argv, struct session_info *info)
{
    UNUSED3(argc, argv, info);
    int op;

    /* just downgrade the lock:
     * - if an exclusive lock is held, downgrade it to shared
     * - if a shared lock is held, unlock
     */
    if (lock_type & LOCK_EX)
        op = LOCK_SH;
    else
        op = LOCK_UN;

    if (flock(lockfd, op) == -1) {
        return -1;  /* continuing could cause deadlock! */
    }

    return 0;
}

static void cmd_lock_cleanup(void)
{
    unlink(GLOBAL_LOCK_FILE);
}

static __newcmd struct cmd_desc cmd1 = {
    .name = "lock",
    .parse = cmd_lock,
    .cleanup = cmd_lock_cleanup,
};
static __newcmd struct cmd_desc cmd2 = {
    .name = "unlock",
    .parse = cmd_unlock,
};
