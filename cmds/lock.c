#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/file.h>
#include "shared.h"

#define LOCK_FILE "./ns2.lock"
#define LOCK_GATE_FILE "./ns2-gate.lock"

/* gain/release a lock over the entire server
 *
 * since the default flock/ioctl lock implementation is reader-biased,
 * making writers starve on a busy server, we use an extra mutex called
 * "gate" here - the idea being that both readers and writers have an
 * equal chance of locking the gate, followed by:
 *   - readers: shared-locking the actual lock, unlocking the gate
 *   - writers: exclusive-locking the actual lock, keeping gate locked,
 *              preventing more readers from getting through
 *
 * the result is a write-biased lock, but without an explicit reader
 * or writer starvation (as both have an equal chance at the gate)
 */

/* args:
 *   lock,<sh|ex>,[nonblock]
 *   unlock
 */

static int lockfd = -1;
static int gatefd = -1;

/* note: when doing exclusive->shared, this algorithm could deadlock
 * when acquiring gate on some locking backends, but flock(2) lets us
 * call LOCK_EX multiple times on the same descriptor */
int xflock(int gate, int lock, int op)
{
    int nb = (op & LOCK_NB);

    if (op & LOCK_UN) {
        if (flock(lock, LOCK_UN) == -1)
            return -1;
        if (flock(gate, LOCK_UN) == -1)
            return -1;
    }
    if (op & (LOCK_EX|LOCK_SH)) {
        if (flock(gate, LOCK_EX | nb) == -1)
            return -1;
        if (flock(lock, op) == -1) {
            flock(gate, LOCK_UN);
            return -1;
        }
    }
    if (op & LOCK_SH) {
        if (flock(gate, LOCK_UN) == -1) {
            flock(lock, LOCK_UN);
            return -1;
        }
    }
    return 0;
}

static int cmd_lock(int argc, char **argv, struct session_info *info)
{
    UNUSED(info);
    int type;

    if (argc < 2)
        return -1;  /* dangerous to go on without a lock */

    if (!strcmp(argv[1], "sh")) {
        type = LOCK_SH;
    } else if (!strcmp(argv[1], "ex")) {
        type = LOCK_EX;
    } else {
        return -1;
    }

    if (argc >= 3 && !strcmp(argv[2], "nonblock")) {
        type |= LOCK_NB;
    }

    if (lockfd == -1) {
        lockfd = open(LOCK_FILE, O_RDONLY|O_CREAT|O_NOCTTY|O_CLOEXEC, 0600);
        if (lockfd == -1) {
            perror("lock/open");
            return -1;
        }
    }
    if (gatefd == -1) {
        gatefd = open(LOCK_GATE_FILE, O_RDONLY|O_CREAT|O_NOCTTY|O_CLOEXEC, 0600);
        if (gatefd == -1) {
            perror("lock/open");
            return -1;
        }
    }

    if (xflock(gatefd, lockfd, type) == -1) {
        if (errno == EWOULDBLOCK) {
            return 2;  /* lock is already held */
        } else {
            perror("lock");
            return -1;
        }
    }

    return 0;
}

static int cmd_unlock(int argc, char **argv, struct session_info *info)
{
    UNUSED3(argc, argv, info);

    if (xflock(gatefd, lockfd, LOCK_UN) == -1) {
        perror("unlock");
        return -1;  /* continuing could cause deadlock! */
    }

    return 0;
}

static void cmd_lock_cleanup(void)
{
    unlink(LOCK_FILE);
    unlink(LOCK_GATE_FILE);
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
