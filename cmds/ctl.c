#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <fnmatch.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <linux/un.h>  /* instead of <sys/un.h> for UNIX_PATH_MAX */
#include "shared.h"

#define VAR_RUN_DIR "/var/run/ns2"
/* used for fd transfer unix sockets */
#define FD_TRANSFER_PREFIX "fd-"
/* maximum total length of socket path+filename */
#ifndef PATH_MAX
#define PATH_MAX 1024
#endif

/*
 * various helpers for managing the control cmdline (user interaction)
 *
 * detach:
 * - close the client socket so that it "detaches"
 *
 * status:
 * - print out the control socket output buffer (cmd return values)
 *
 * recv/send:
 * - uses unix domain sockets with SCM_RIGHTS to recv/send a new clientfd
 *   from/to another instance of this parser, useful ie. after detach
 */

/* args:
 *   ctl-status
 *   ctl-loop,[loops],[lastcmds]  # "loops" defaults to -1 (infinite loop),
 *                                # "lastcmds" defaults to all previous
 *   ctl-detach
 *   ctl-recv,[string]   # string should be unique, same for recv and send,
 *   ctl-send,[string]   # remote address is used if left unspecified
 */

/* write (fully or partially) a string to a socket, remove the written bytes
 * from the string (shifting it to the left) */
static ssize_t write_str(int fd, char *str)
{
    ssize_t rc;
    size_t len = strlen(str);
    rc = write(fd, str, len);
    /* on successful write, remove written bytes */
    if (rc != -1)
        memmove(str, str+rc, len-rc+1);  /* +1 is for '\0' */
    return rc;
}

static int cmd_status(int argc, char **argv, struct session_info *info)
{
    UNUSED2(argc, argv);
    if (info->sock == -1)
        return 1;

    write_str(info->sock, info->ctl_outbuff);

    return 0;
}

struct cmd_loop_data { int loopcnt; void *orignext; };
static int cmd_loop(int argc, char **argv, struct session_info *info)
{
    int jmpcmds;
    struct cmd_loop_data *data;
    struct cmd *start;

    /* looping */
    data = info->cmd->custom_data;
    if (data) {
        if (data->loopcnt < 0) {
            return 0;  /* infinite */
        } else if (data->loopcnt <= 1) {
            info->cmd->custom_data = NULL;
            info->cmd->next = data->orignext;
            free(data);
            return 0;
        } else {
            data->loopcnt--;
            return 0;
        }
    }

    /* setup */
    data = xmalloc(sizeof(struct cmd_loop_data));

    if (argc >= 2) {
        data->loopcnt = atoi(argv[1]);
        if (!data->loopcnt) {
            free(data);
            return 0;  /* 0 loops */
        }
    } else {
        data->loopcnt = -1;
    }

    data->orignext = info->cmd->next;
    if (argc >= 3) {
        jmpcmds = atoi(argv[2]);
        for (start = info->cmd; jmpcmds-- && start->prev; start = start->prev);
    } else {
        for (start = info->cmd; start->prev; start = start->prev);
    }
    info->cmd->next = start;

    info->cmd->custom_data = data;
    return 0;
}

static int cmd_detach(int argc, char **argv, struct session_info *info)
{
    UNUSED2(argc, argv);
    if (info->sock == -1)
        return 1;

    linger(info->sock, 1);
    close(info->sock);
    info->sock = -1;
    return 0;
}

static int send_fd(int via, int fd)
{
    int *fdptr;
    struct msghdr msgh;
    struct iovec iov;
    char dummy;
    char cmsg_buf[CMSG_SPACE(sizeof(int))];
    struct cmsghdr *cmsg;

    /* need to send *some* payload on linux */
    dummy = 0;
    iov.iov_base = &dummy;
    iov.iov_len = 1;

    memset(&msgh, 0, sizeof(struct msghdr));
    msgh.msg_iov = &iov;
    msgh.msg_iovlen = 1;
    msgh.msg_control = cmsg_buf;
    msgh.msg_controllen = sizeof(cmsg_buf);

    memset(&cmsg_buf, 0, sizeof(cmsg_buf));
    cmsg = CMSG_FIRSTHDR(&msgh);
    cmsg->cmsg_len = CMSG_LEN(sizeof(int));
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;

    fdptr = (int *) CMSG_DATA(cmsg);
    *fdptr = fd;

    return sendmsg(via, &msgh, 0);
}

static int recv_fd(int via, int *fd)
{
    struct msghdr msgh;
    struct iovec iov;
    char dummy;
    char cmsg_buf[CMSG_SPACE(sizeof(int))];
    struct cmsghdr *cmsg;

    iov.iov_base = &dummy;
    iov.iov_len = 1;

    memset(&msgh, 0, sizeof(struct msghdr));
    msgh.msg_iov = &iov;
    msgh.msg_iovlen = 1;
    msgh.msg_control = cmsg_buf;
    msgh.msg_controllen = sizeof(cmsg_buf);

    if (recvmsg(via, &msgh, 0) == -1)
        return -1;

    cmsg = CMSG_FIRSTHDR(&msgh);
    if (cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_type != SCM_RIGHTS)
        return -1;

    memcpy(fd, CMSG_DATA(cmsg), sizeof(int));
    return 0;
}

static int listen_unix(char *path)
{
    int fd, clfd;
    struct sockaddr_un addr;

    fd = socket(PF_UNIX, SOCK_STREAM, 0);
    if (fd == -1)
        return -1;

    memset(&addr, 0, sizeof(struct sockaddr_un));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, UNIX_PATH_MAX);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(struct sockaddr_un)) == -1)
        return -1;

    if (listen(fd, 1) == -1)
        return -1;

    clfd = accept(fd, NULL, NULL);
    if (clfd == -1)
        return -1;

    close(fd);
    return clfd;
}

static int connect_unix(char *path)
{
    int fd;
    struct sockaddr_un addr;

    fd = socket(PF_UNIX, SOCK_STREAM, 0);
    if (fd == -1)
        return -1;

    memset(&addr, 0, sizeof(struct sockaddr_un));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, UNIX_PATH_MAX);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(struct sockaddr_un)) == -1)
        return -1;

    return fd;
}

static int cmd_recv(int argc, char **argv, struct session_info *info)
{
    int unixfd, fd;
    char *uniquestr;
    char namebuf[PATH_MAX];
    char remote_ascii[REMOTE_ADDRA_MAX];

    if (argc >= 2) {
        uniquestr = argv[1];
    } else {
        if (remote_addra(info->sock, remote_ascii) == -1)
            return 1;
        uniquestr = remote_ascii;
    }

    /* if the old socket is still open, "detach" it */
    if (info->sock != -1) {
        linger(info->sock, 1);
        close(info->sock);
    }

    /* okay to fail if the dir already exists */
    mkdir(VAR_RUN_DIR, 0750);

    *namebuf = '\0';
    snprintf(namebuf, sizeof(namebuf), "%s/%s%s",
             VAR_RUN_DIR, FD_TRANSFER_PREFIX, uniquestr);

    unixfd = listen_unix(namebuf);
    if (unixfd == -1) {
        perror("listen_unix");
        return -1;  /* cannot afford to continue */
    }

    if (recv_fd(unixfd, &fd) == -1) {
        perror("recv_fd");
        return -1;  /* cannot afford to continue */
    }
    unlink(namebuf);

    info->sock = fd;
    return 0;
}
static void cmd_recv_cleanup(void)
{
    DIR *dp;
    struct dirent *ent;
    int dfd;

    dp = opendir(VAR_RUN_DIR);
    if (dp == NULL)
        return;
    dfd = dirfd(dp);
    if (dfd == -1)
        return;

    /* since we can't pass anything, remove all possible fd transfers */
    while ((ent = readdir(dp)) != NULL) {
        if (!fnmatch(FD_TRANSFER_PREFIX "*", ent->d_name, 0))
            unlinkat(dfd, ent->d_name, 0);
    }
    closedir(dp);
    rmdir(VAR_RUN_DIR);
}

static int cmd_send(int argc, char **argv, struct session_info *info)
{
    int unixfd;
    char *uniquestr;
    char namebuf[PATH_MAX];
    char remote_ascii[REMOTE_ADDRA_MAX];

    if (info->sock == -1)
        return 1;

    if (argc >= 2) {
        uniquestr = argv[1];
    } else {
        if (remote_addra(info->sock, remote_ascii) == -1)
            return 1;
        uniquestr = remote_ascii;
    }

    *namebuf = '\0';
    snprintf(namebuf, sizeof(namebuf), "%s/%s%s",
             VAR_RUN_DIR, FD_TRANSFER_PREFIX, uniquestr);

    unixfd = connect_unix(namebuf);
    if (unixfd == -1) {
        perror("connect_unix");
        return -1;  /* cannot afford to continue */
    }

    if (send_fd(unixfd, info->sock) == -1) {
        perror("send_fd");
        return -1;  /* cannot afford to continue */
    }

    /* sucessfully sent, close for us */
    close(info->sock);
    info->sock = -1;

    return 0;
}


static __newcmd struct cmd_desc cmd1 = {
    .name = "ctl-status",
    .parse = cmd_status,
};
static __newcmd struct cmd_desc cmd2 = {
    .name = "ctl-loop",
    .parse = cmd_loop,
};
static __newcmd struct cmd_desc cmd3 = {
    .name = "ctl-detach",
    .parse = cmd_detach,
};
static __newcmd struct cmd_desc cmd4 = {
    .name = "ctl-recv",
    .parse = cmd_recv,
    .cleanup = cmd_recv_cleanup,
};
static __newcmd struct cmd_desc cmd5 = {
    .name = "ctl-send",
    .parse = cmd_send,
};
