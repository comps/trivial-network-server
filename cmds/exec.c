#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <sys/wait.h>
#include "shared.h"

/* args:
 *   exec,<cmdname>,[cmdarg1],...
 */

/* directory prefix for executing files - we don't allow any arbitrary
 * path to be executed, only files in this dir
 * (by default, use cwd of the server) */
#ifndef EXEC_DIR
#define EXEC_DIR "."
#endif

/* maximum file path length (incl. EXEC_DIR above, excl. argv) */
#define MAX_PATH_LEN 4096

/*
 * this command returns the exit code of the command it executes - to avoid
 * interfering with this return value, we return 256 (outside of the normal
 * 8 bits of return code) on *our* error (setup around execve()), ie. when
 * the specified file does not exist, is not executable, etc.
 * - we can do that, because we return a full int, not just 8 bits
 *   (yes, this assumes sizeof(int) > 1)
 */
#define ERR_RET 256

/* add a trailing zero element to an existing argv array, copying it
 * in the process (since realloc could move it elsewhere)
 * - needed when passing the argv to execve, which doesn't take argc */
char **append_zero_elem(char **arr, int elems)
{
    char **ret;
    ret = xmalloc((elems+1) * sizeof(char*));
    memcpy(ret, arr, elems * sizeof(char*));
    ret[elems] = NULL;
    return ret;
}

static int parse(int argc, char **argv, struct session_info *info)
{
    int status;
    pid_t childpid;
    char **newargv;
    char execpath[MAX_PATH_LEN];
    char raddr[13+ASCII_ADDR_MAX] = "REMOTE_ADDR=";
    char laddr[12+ASCII_ADDR_MAX] = "LOCAL_ADDR=";

    if (argc < 2)
        return ERR_RET;

    /* the filename can't contain slashes */
    if (strchr(argv[1], '/'))
        return ERR_RET;

    *execpath = '\0';
    strncat(execpath, EXEC_DIR "/", MAX_PATH_LEN-1);
    strncat(execpath, argv[1], MAX_PATH_LEN-1);

    if (access(execpath, R_OK|X_OK) != 0)
        return ERR_RET;

    newargv = append_zero_elem(argv, argc);

    if (info->sock != -1) {
        if (ascii_addr(info->sock, raddr+strlen(raddr), getpeername) == -1)
            return ERR_RET;
        if (ascii_addr(info->sock, laddr+strlen(laddr), getsockname) == -1)
            return ERR_RET;
    }

    childpid = fork();
    switch (childpid) {
        case -1:
            return ERR_RET;

        case 0:
            /* reopen stdio on the client socket, leave stderr on the server
             * for debug purposes - this also leaves any other possibly open
             * fds (by other commands) for the exec'd command to use */
            if (info->sock != -1) {
                if (close(STDIN_FILENO) == -1 || close(STDOUT_FILENO) == -1)
                    exit(-1);
                if (dup2(info->sock, STDIN_FILENO) == -1)
                    exit(-1);
                if (dup2(info->sock, STDOUT_FILENO) == -1)
                    exit(-1);
                if (putenv(raddr))
                    exit(-1);
                if (putenv(laddr))
                    exit(-1);
            }

            /* load new executable image */
            execv(execpath, newargv+1);

            /* exec failed */
            exit(-1);

        default:
            break;
    }

    if (waitpid(childpid, &status, 0) == -1)
        return ERR_RET;

    free(newargv);

    return WEXITSTATUS(status);
}

static __newcmd struct cmd_desc cmd = {
    .name = "exec",
    .parse = parse,
    /* cleanup taken care of generically - parent kills children recursively */
};
