#ifndef SHARED_H
#define SHARED_H

/** constants **/


/** generic structures **/

/* command to be executed, with arguments parsed from control cmdline */
struct cmd {
    int argc;
    char **argv;
    struct cmd_desc *desc;
    struct cmd *prev;
    struct cmd *next;
    /* custom data reference,
     * may be overwritten by commands (passing any data) */
    void *custom_data;
};

/* overall info about the current session, ie. the client, cmds being processed,
 * data passed between commands, etc. */
struct session_info {
    /* client connection socket, ie. for getsockname(),
     * may be overwritten by commands (ie. by 'detach' to -1) */
    int sock;
    /* 'control socket output buffer' (cmd return values) */
    char *ctl_outbuff;
    /* currently executed command (along with prev/next in a list) */
    struct cmd *cmd;
};

/* describes properties of a command parser */
struct cmd_desc {
    /* matched against client-provided commands */
    char *name;
    /* parsing function for the command, called on match */
    int (*parse)(int argc, char **argv, struct session_info *);
    /* *global* cleanup function, should close/remove/cleanup *any*
     * command-created or owned sockets/files/processes/...
     * think of it like a SIGINT/SIGTERM handler for any program */
    void (*cleanup)(void);
} __attribute__((__packed__));


/** ELF sections **/

/* we store all commands in separate ELF sections, for modularity */
/* the explicit aligned(1) is required if the struct is not naturally 16-byte
 * aligned (for x86_64 ABI at least) */

/* cmd descriptions */
#define __newcmd \
    __attribute__((__section__("cmd_descs"))) \
    __attribute__((__used__)) \
    __attribute__((aligned(1)))
extern struct cmd_desc __start_cmd_descs;
extern struct cmd_desc __stop_cmd_descs;


/** useful macros **/

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
struct cmd_desc *cmd_descs_iterate(struct cmd_desc *);

/* helper functions from main.c */
int create_socket(char *, char *, int, int);

/* client processing functions from client.c */
void process_client(int);

/* cleanup signal handler from cleanup.c */
void cleanup_sig_handler(int);
void cleanup_exit_handler(int);

#endif /* SHARED_H */
