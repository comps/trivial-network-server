#ifndef SHARED_H
#define SHARED_H

/** per-command structures **/

/* client sock 'control mode' */
enum sock_ctl_mode {
    CTL_MODE_CONTROL,
    CTL_MODE_BINARY,
};

/* passed to a parsing function of a command */
struct client_info {
    /* client connection socket, ie. for getsockname(),
     * may be overwritten by commands (ie. by 'detach' to -1) */
    int sock;
    /* 'control mode' of the socket, how the parser itself uses it */
    enum sock_ctl_mode sock_mode;
    /* custom data reference,
     * may be overwritten by commands (passing any data) */
    void *custom_data;
};

/* describes properties of a command */
struct cmd_info {
    /* matched against client-provided commands */
    char *name;
    /* parsing function for the command, called on match */
    int (*parse)(int argc, char **argv, struct client_info *);
    /* *global* cleanup function, should close/remove/cleanup *any*
     * command-created or owned sockets/files/processes/...
     * think of it like a SIGINT/SIGTERM handler for any program */
    void (*cleanup)(void);
} __attribute__((__packed__));


/** ELF sections **/

/* we store all commands in separate ELF sections, for modularity */
/* the explicit aligned(1) is required if the struct is not naturally 16-byte
 * aligned (for x86_64 ABI at least) */

/* cmds */
#define __newcmd \
    __attribute__((__section__("cmds"))) \
    __attribute__((__used__)) \
    __attribute__((aligned(1)))
extern struct cmd_info __start_cmds;
extern struct cmd_info __stop_cmds;


/** useful defines **/

/* unused arguments in a function */
#define UNUSED(a) (void)a
#define UNUSED2(a,b) UNUSED(a),UNUSED(b)
#define UNUSED3(a,b,c) UNUSED(a),UNUSED2(b,c)
#define UNUSED4(a,b,c,d) UNUSED(a),UNUSED3(b,c,d)


/** helper functions **/

/* small helper/wrapper functions from shared.c */
void error(char *, ...);
void error_down(char *, ...);
void perror_down(char *);
void *xmalloc(size_t);
void *xrealloc(void *, size_t);
ssize_t xrecv(int, void *, size_t, int);
void (*xsignal(int, void (*)(int)))(int);
/* ELF section parsers / iterators from shared.c */
struct cmd_info *cmds_iterate(struct cmd_info *);

/* helper functions from main.c */
int create_socket(char *, char *, int, int);

/* client processing functions from client.c */
int parse_args(char ***, char *, char);
void process_client(int);

/* cleanup signal handler from cleanup.c */
void cleanup_sig_handler(int);
void cleanup_exit_handler(int);

#endif /* SHARED_H */
