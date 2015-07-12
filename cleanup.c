/*
 * "emergency" cleanup, invoked by a signal handler from the main listening
 * server, executes per-cmd cleanup functions as well as killing all children
 * and other generic cleanup-like actions
 */

#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <ctype.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>

#include "shared.h"

/* go over children of PID 'ppid', call callback on each child PID,
 * return the number of children found */
/* this function *can not* depend on any variable provided by the parent
 * (such as fork() return value) during runtime as it's called from a signal
 * handler on at least one occasion */
static int
traverse_children(pid_t ppid, void (*callback)(pid_t pid, void *data),
                  void *callback_data)
{
    DIR *proc;
    struct dirent *entry;
    pid_t readpid, readppid;
    char statpath[32];
    FILE *statfile;
    int childnr;

    proc = opendir("/proc");
    if (proc == NULL)
        return -1;

    /* no need to re-scan this multiple times as we can only find children
     * of ppid, which is suspended for the time being and therefore can't
     * create new children */
    childnr = 0;
    statfile = NULL;
    while ((entry = readdir(proc)) != NULL) {
        readpid = atoi(entry->d_name);
        if (readpid == 0 || !isdigit(entry->d_name[0]))
            continue;

        *statpath = '\0';
        snprintf(statpath, sizeof(statpath), "/proc/%d/stat", readpid);

        statfile = fopen(statpath, "r");
        if (statfile == NULL)
            continue;
        if (fscanf(statfile, "%*d %*s %*c %d", &readppid) == EOF) {
            fclose(statfile);
            continue;
        } else {
            fclose(statfile);
        }

        if (readppid == ppid && callback != NULL) {
            childnr++;
            if (callback != NULL)
                callback(readpid, callback_data);
        }
    }

    closedir(proc);
    return childnr;
}

/* traverse_children uses /proc walk, so it doesn't depend on anything local */
static void kill_child(pid_t child, void *sig)
{
    int *signum = sig;
    /* try to kill pgroup if the child managed to do setsid() */
    kill(-child, *signum);
    /* fall back if not */
    kill(child, *signum);
    waitpid(child, NULL, 0);
}

/* the actual "cleanup" body, should completely "reset" the server, like it was
 * just (re)started externally (+ clean up created objects) */
static void do_cleanup(void)
{
    int killwith = SIGKILL;
    struct cmd_info *ptr = NULL;

    /* call cleanup functions for each command */
    while ((ptr = cmds_iterate(ptr)) != NULL) {
        /* we can do this in a signal handler because the list of func
         * pointers was created at compile time, not during runtime */
        if (ptr->cleanup != NULL)
            ptr->cleanup();
    }

    /* kill all running children */
    traverse_children(getpid(), kill_child, &killwith);
    /* collect all zombies */
    while (waitpid(-1, NULL, WNOHANG) > 0);
}

/* signal handlers */
void cleanup_sig_handler(int signum)
{
    UNUSED(signum);
    do_cleanup();
}
void cleanup_exit_handler(int signum)
{
    UNUSED(signum);
    do_cleanup();
    _exit(EXIT_SUCCESS);
}
