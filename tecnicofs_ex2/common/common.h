#ifndef COMMON_H
#include <stdio.h>
#include <pthread.h>
/* tfs_open flags */

enum {
    TFS_O_CREAT = 0b001,
    TFS_O_TRUNC = 0b010,
    TFS_O_APPEND = 0b100,
};

typedef struct Mount {
    unsigned int session_id;
    char client_pipe_path[40];
} Mount;

typedef struct Unmount {
    unsigned int session_id;
}  Unmount;

typedef struct Open {
    unsigned int session_id;
    char name[40];
    int flags;
} Open;

typedef struct Close {
    unsigned int session_id;
    int fhandle;
} Close;

typedef struct Write {
    unsigned int session_id;
    int fhandle;
    size_t len;
} Write;

typedef struct Read {
    unsigned int session_id;
    int fhandle;
    size_t len;
} Read;

typedef struct Shutdown {
    unsigned int session_id;
} Shutdown;

union Message {
    struct Mount m_message;
    struct Unmount u_message;
    struct Open o_message;
    struct Close c_message;
    struct Write w_message;
    struct Read r_message;
    struct Shutdown s_message;
};

/*
 * Session
 */
typedef struct {
    int tid;
    int pipe;
    pthread_cond_t sent_all;
    char* buffer;
} Session;

/* operation codes (for client-server requests) */
enum {
    TFS_OP_CODE_MOUNT = 1,
    TFS_OP_CODE_UNMOUNT = 2,
    TFS_OP_CODE_OPEN = 3,
    TFS_OP_CODE_CLOSE = 4,
    TFS_OP_CODE_WRITE = 5,
    TFS_OP_CODE_READ = 6,
    TFS_OP_CODE_SHUTDOWN_AFTER_ALL_CLOSED = 7
};

#endif /* COMMON_H */