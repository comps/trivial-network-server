/*
 * main server code, takes care of the listening and accepting new connections,
 * forking new processes for each accepted connection and calling
 * process_client() in the fork
 *
 * also sets up signal handler for cleanup
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <netdb.h>
#include <sys/wait.h>

#include "shared.h"

/* how many clients can wait on a particular address (TCP backlog) */
#define LISTEN_BACKLOG 5

/* create a listening/connected socket and return its fd,
 * server arg as N:
 *   if > 0, create listening socket with N conn backlog
 *   if == 0, create a connected (client) socket
 *   if < 0, don't do bind, listen or connect */
int create_socket(char *addr, char *port, int socktype, int server)
{
    int rc, fd;
    struct addrinfo *ainfo, hints;

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = socktype;

    rc = getaddrinfo(addr, port, &hints, &ainfo);
    if (rc != 0) {
        fprintf(stderr, "%s\n", (char*)gai_strerror(rc));
        return -1;
    }

    fd = socket(ainfo->ai_family, ainfo->ai_socktype, ainfo->ai_protocol);
    if (fd < 0) {
        perror("socket");
        goto err;
    }

    rc = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &rc, sizeof(rc)) == -1) {
        perror("setsockopt");
        goto err;
    }

    if (server > 0) {
        if (bind(fd, ainfo->ai_addr, ainfo->ai_addrlen) == -1) {
            perror("bind");
            goto err;
        }
        if (socktype == SOCK_STREAM || socktype == SOCK_SEQPACKET) {
            if (listen(fd, server) == -1) {
                perror("listen");
                goto err;
            }
        }
    } else if (!server) {
        if (connect(fd, ainfo->ai_addr, ainfo->ai_addrlen) == -1) {
            perror("connect");
            goto err;
        }
    }

    freeaddrinfo(ainfo);

    return fd;
err:
    freeaddrinfo(ainfo);
    return -1;
}

/*
 * main
 */
int main(int argc, char **argv)
{
    int i;
    char *port;

    int fd;
    struct pollfd *fds;
    int nfds;

    if (argc < 3) {
        error_down(
            "usage: %s <addr1> [addr2] ... <port>\n\n"
            "Listen on all specified addresses on a given TCP port.\n",
            argv[0]);
    }

    port = argv[argc-1];

    nfds = argc-2;  /* no argv[0] or port */
    fds = xmalloc(nfds * sizeof(struct pollfd));

    /* go through the addresses one by one */
    argv++;
    for (i = 0; i < nfds; i++) {
        fd = create_socket(argv[i], port, SOCK_STREAM, LISTEN_BACKLOG);
        if (fd == -1)
            exit(EXIT_FAILURE);

        fds[i].fd = fd;
        fds[i].events = POLLIN | POLLPRI;
    }
    argv--;

    /* clean up on HUP/INT/TERM */
    xsignal(SIGHUP, cleanup_sig_handler);
    xsignal(SIGINT, cleanup_exit_handler);
    xsignal(SIGTERM, cleanup_exit_handler);

    /* wait for clients on any listening socket */
    while (1) {
        if (poll(fds, nfds, 100) == -1)
            if (errno != EINTR)
                perror_down("poll");

        for (i = 0; i < nfds; i++) {
            if (fds[i].revents & (POLLIN | POLLPRI)) {
                /* accept the client, start processing input data */
                fd = accept(fds[i].fd, NULL, NULL);
                if (fd == -1)
                    continue;

                switch (fork()) {
                    case -1:
                        perror_down("fork");
                        break;
                    case 0:
                        /* start a new process group (for easy kill/cleanup) */
                        if (setsid() == (pid_t)-1)
                            exit(EXIT_FAILURE);
                        /* listen fds no longer needed in the child */
                        for (i = 0; i < nfds; i++)
                            close(fds[i].fd);
                        free(fds);
                        process_client(fd);
                        exit(EXIT_SUCCESS);
                        break;
                    default:
                        /* client fd no longer needed in the parent */
                        close(fd);
                }

            } else if (fds[i].revents) {
                /* any other event unexpected */
                error_down("got invalid poll revent(s) on fd %d: %hd\n",
                           i, fds[i].revents);
            }
        }

        /* housekeeping: collect all zombies */
        while (waitpid(-1, NULL, WNOHANG) > 0);
    }

    free(fds);

    return 0;
}
