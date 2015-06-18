/*
 * server-wide locking, with a simple global variable abstraction, so it can be
 * called from anywhere in the call stack without passing lock fd to every func
 * of every other func
 *
 * no need to reinvent the wheel, all the shared/exclusive/unlock/blocking/..
 * functionality is provided by the flock() interface, so include sys/file.h
 * and use it from elsewhere as if srvlock() was flock() without fd
 */

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/file.h>

#include "shared.h"

#define GLOBAL_LOCK_FILE "/var/lock/ns2.lock"

static int lockfd = -1;

static void srvlock_init(void)
{
    int fd;
    fd = open(GLOBAL_LOCK_FILE, O_RDONLY|O_CREAT|O_NOCTTY|O_CLOEXEC, 0600);
    if (fd == -1)
        perror_down("srvlock_init");
    lockfd = fd;
}

int srvlock(int op)
{
    if (lockfd == -1)
        srvlock_init();
    return flock(lockfd, op);
}

void srvlock_cleanup(void)
{
    unlink(GLOBAL_LOCK_FILE);
}
