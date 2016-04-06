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
    /* is the session active? if not, it will be closed (finished)
     * after the current cmd list ends */
    int active;
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
void _verbose(char *, int, char *, ...);
#define verbose(...) _verbose(__FILE__, __LINE__, __VA_ARGS__)
void _verbose_s(char *, int, int, char *, ...);
#define verbose_s(...) _verbose_s(__FILE__, __LINE__, __VA_ARGS__)
void _error(char *, int, char *, ...);
#define error(...) _error(__FILE__, __LINE__, __VA_ARGS__)
void _error_down(char *, int, char *, ...);
#define error_down(...) _error_down(__FILE__, __LINE__, __VA_ARGS__)
void _perror_down(char *, int, char *);
#define perror_down(...) _perror_down(__FILE__, __LINE__, __VA_ARGS__)
void *xmalloc(size_t);
void *xrealloc(void *, size_t);
ssize_t xrecv(int, void *, size_t, int);
void (*xsignal(int, void (*)(int)))(int);
int linger(int, int);

/* generic helpers from shared.c */
#include <arpa/inet.h>
#define ASCII_ADDR_MAX INET6_ADDRSTRLEN /* should hold about anything */
int ascii_addr(int, char *, int (*)(int, struct sockaddr *, socklen_t *));
int get_port(int, int (*)(int, struct sockaddr *, socklen_t *));
struct cmd_desc *cmd_descs_iterate(struct cmd_desc *);

/* helper functions from main.c */
int create_socket(char *, char *, int, int);

/* client processing functions from client.c */
int sock_ctol(int, int, int, int, int *);
void process_client(int);

/* cleanup signal handler from cleanup.c */
#include <sys/types.h>
int traverse_children(pid_t, void (*)(pid_t, void *), void *);
void cleanup_sig_handler(int);
void cleanup_exit_handler(int);

/* defined by various commands, useful elsewhere */
int death_timer(int);

#endif /* SHARED_H */
