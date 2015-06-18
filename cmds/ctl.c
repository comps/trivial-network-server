#include <stdio.h>
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

/*
 * various helpers for managing the control cmdline (user interaction)
 *
 * detach:
 * - close the client socket so that it "detaches"
 *
 * mode:
 * - sets the control mode:
 *   - in 'control', commands and their return codes are sent to client
 *   - in 'binary', the server doesn't interact with the socket at all,
 *     this is useful for ie. transferring binary files over the socket
 *
 * recv/send:
 * - uses unix domain sockets with SCM_RIGHTS to recv/send a new clientfd
 *   from/to another instance of this parser, useful ie. after detach
 */

/* args:
 *   ctl-detach
 *   ctl-mode,<mode>
 *   ctl-recv,<string>
 *   ctl-send,<string>   # string should be unique, same for recv and send
 */

static int cmd_detach(int argc, char **argv, struct client_info *c)
{
    if (c->sock == -1)
        return 1;

    if (argc > 1 && !strcmp(argv[1], "force")) {
        /* send RST or otherwise "forcibly" close the connection,
         * (results in ECONNRESET on client, making nmap-ncat *really* exit) */
        setsockopt(c->sock, SOL_SOCKET, SO_LINGER,
                   &(struct linger){1, 0}, sizeof(struct linger));
    } else {
        /* clean, with default background linger */
        shutdown(c->sock, SHUT_RDWR);
    }
    close(c->sock);
    c->sock = -1;
    return 0;
}

static int cmd_mode(int argc, char **argv, struct client_info *c)
{
    if (argc < 2)
       return 1;

    if (!strcmp(argv[1], "control"))
        c->sock_mode = CTL_MODE_CONTROL;
    else if (!strcmp(argv[1], "binary"))
        c->sock_mode = CTL_MODE_BINARY;
    else
        return 1;

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

    *fd = *((int *) CMSG_DATA(cmsg));
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

static int cmd_recv(int argc, char **argv, struct client_info *c)
{
    int unixfd, fd;
    char namebuf[128];

    if (argc < 2)
        return 1;

    /* okay to fail if the dir already exists */
    mkdir(VAR_RUN_DIR, 0750);

    *namebuf = '\0';
    snprintf(namebuf, sizeof(namebuf), "%s/%s%s",
             VAR_RUN_DIR, FD_TRANSFER_PREFIX, argv[1]);

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
    c->sock = fd;
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

static int cmd_send(int argc, char **argv, struct client_info *c)
{
    int unixfd;
    char namebuf[128];

    if (c->sock == -1)
        return 1;

    if (argc < 2)
        return 1;

    *namebuf = '\0';
    snprintf(namebuf, sizeof(namebuf), "%s/%s%s",
             VAR_RUN_DIR, FD_TRANSFER_PREFIX, argv[1]);

    unixfd = connect_unix(namebuf);
    if (unixfd == -1) {
        perror("connect_unix");
        return -1;  /* cannot afford to continue */
    }

    if (send_fd(unixfd, c->sock) == -1) {
        perror("send_fd");
        return -1;  /* cannot afford to continue */
    }

    /* sucessfully sent, close for us */
    close(c->sock);
    c->sock = -1;

    return 0;
}


static __newcmd struct cmd_info cmd1 = {
    .name = "ctl-detach",
    .parse = cmd_detach,
};
static __newcmd struct cmd_info cmd2 = {
    .name = "ctl-mode",
    .parse = cmd_mode,
};
static __newcmd struct cmd_info cmd3 = {
    .name = "ctl-recv",
    .parse = cmd_recv,
    .cleanup = cmd_recv_cleanup,
};
static __newcmd struct cmd_info cmd4 = {
    .name = "ctl-send",
    .parse = cmd_send,
};
