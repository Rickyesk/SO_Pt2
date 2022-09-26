#include "tecnicofs_client_api.h"
#include "fcntl.h"
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_FILE_NAME (40)

char const* server_pipe_name;
char client_pipe_name[40];
unsigned int client_session;
int server_pipe;
int client_pipe;
void* message_buffer;
int success;

int tfs_mount(char const *client_pipe_path, char const *server_pipe_path) {
    char op_code = '1';
    struct Mount message;
    message_buffer = (void*) malloc(sizeof(char) * (MAX_FILE_NAME + 1));

    memset(client_pipe_name, '\0', MAX_FILE_NAME);
    strcpy(client_pipe_name, client_pipe_path);
    server_pipe_name = server_pipe_path;

    unlink(client_pipe_path);
    if (mkfifo(client_pipe_path, 0777) == -1) {
        return -1;
    }
    strcpy(message.client_pipe_path, client_pipe_name);
    memcpy(message_buffer, &op_code, sizeof(char));
    memcpy(message_buffer + sizeof(char), &message.client_pipe_path, MAX_FILE_NAME);

    if ((server_pipe = open(server_pipe_name, O_WRONLY)) == -1) {
        return -1;
    }
    if (write(server_pipe, message_buffer, MAX_FILE_NAME + 1) == -1) {
        return -1;
    }
    free(message_buffer);
    if ((client_pipe = open(client_pipe_path, O_RDONLY)) == -1) {
        return -1;
    }
    if (read(client_pipe, &client_session, sizeof(int)) == -1) {
        return -1;
    }
    if (client_session == -1) { 
        return -1;
    }
    return 0;
}

int tfs_unmount() { 
    char op_code = '2';
    struct Unmount message;
    message_buffer = (void*)malloc(sizeof(char) + sizeof(int));

    memcpy(message_buffer, &op_code, sizeof(op_code));
    message.session_id = client_session;
    memcpy(message_buffer + sizeof(char), &message.session_id, sizeof(int));

    if (write(server_pipe, message_buffer, 1 + sizeof(int)) == -1 || close(server_pipe) == -1) {
        return -1;
    }
    if (read(client_pipe, &success, sizeof(int)) == -1) {
        return -1;
    }
    if (success == -1 || close(client_pipe) == -1 || unlink(client_pipe_name) == -1) {
        return -1;
    }
    free(message_buffer);
    return success;
}

int tfs_open(char const *name, int flags) {
    char op_code = '3';
    struct Open message;
    message_buffer = (void*)malloc(sizeof(char) + 2 * sizeof(int) + MAX_FILE_NAME);

    memcpy(message_buffer, &op_code, sizeof(char));
    message.session_id = client_session;
    memset(message.name, '\0', MAX_FILE_NAME);
    strcpy(message.name, name);
    message.flags = flags;
    memcpy(message_buffer + sizeof(char), &message.session_id, sizeof(int));
    memcpy(message_buffer + sizeof(char) + sizeof(int), &message.name, MAX_FILE_NAME);
    memcpy(message_buffer + sizeof(char) + sizeof(int) + MAX_FILE_NAME, &message.flags, sizeof(int));

    if (write(server_pipe, message_buffer, 1 + 2 * sizeof(int) + MAX_FILE_NAME) == -1) {
        return -1;
    }
    //here success equals the file handle returned by the tfs_open in operations.c
    if (read(client_pipe, &success, sizeof(int)) == -1) {
        return -1;
    }
    free(message_buffer);
    return success;
}

int tfs_close(int fhandle) {
    char op_code = '4';
    struct Close message;
    message_buffer = (void*)malloc(sizeof(char) + 2 * sizeof(int));

    memcpy(message_buffer, &op_code, sizeof(op_code));
    message.session_id = client_session;
    message.fhandle = fhandle;
    memcpy(message_buffer + sizeof(char), &message.session_id, sizeof(int));
    memcpy(message_buffer + sizeof(char) + sizeof(int), &message.fhandle, sizeof(int));

    if (write(server_pipe, message_buffer, sizeof(char) + 2 * sizeof(int)) == -1) {
        return -1;
    }
    //success checks that this operation succeeded
    if (read(client_pipe, &success, sizeof(int)) == -1 || success == -1) {
        return -1;
    }
    free(message_buffer);
    return 0;
}

ssize_t tfs_write(int fhandle, void const *buffer, size_t len) {
    char op_code = '5';
    struct Write message;
    ssize_t count;
    message_buffer = (void*)malloc(sizeof(char) + 2 * sizeof(int) + len);

    char* temp_buffer = (char*)malloc(len);
    memcpy(message_buffer, &op_code, sizeof(op_code));
    message.session_id = client_session;
    message.fhandle = fhandle;
    message.len = len;

    memcpy(message_buffer + sizeof(char), &message.session_id, sizeof(int));
    memcpy(message_buffer + sizeof(char) + sizeof(int), &message.fhandle, sizeof(int));
    memcpy(message_buffer + sizeof(char) + 2 * sizeof(int), &message.len, sizeof(size_t));
    memcpy(temp_buffer, buffer, len);
    memcpy(message_buffer + sizeof(char) + 2 * sizeof(int) + sizeof(size_t), temp_buffer, len);

    if (write(server_pipe, message_buffer, len + 1 + 2 * sizeof(int) + sizeof(size_t)) == -1) {
        return -1;
    }
    if (read(client_pipe, &count, sizeof(ssize_t)) == -1) {
        return -1;
    }
    free(message_buffer);
    free(temp_buffer);
    return count;
}

ssize_t tfs_read(int fhandle, void *buffer, size_t len) {
    char op_code = '6';
    struct Read message;
    message_buffer = (void*)malloc(sizeof(char) + 2 * sizeof(int) + sizeof(size_t));

    memcpy(message_buffer, &op_code, sizeof(op_code));
    message.session_id = client_session;
    message.fhandle = fhandle;
    message.len = len;
    memcpy(message_buffer + sizeof(char), &message.session_id, sizeof(int));
    memcpy(message_buffer + sizeof(char) + sizeof(int), &message.fhandle, sizeof(int));
    memcpy(message_buffer + sizeof(char) + 2 * sizeof(int), &message.len, sizeof(size_t));

    if (write(server_pipe, message_buffer, sizeof(char) + 2 * sizeof(int) + sizeof(size_t)) == -1) {
        return -1;
    }
    if (read(client_pipe, buffer, len) == -1) {
        return -1;
    }
    free(message_buffer);
    return (ssize_t)strlen(buffer);
}

int tfs_shutdown_after_all_closed() {
    char op_code = '7';
    struct Shutdown message;
    message_buffer = (void*)malloc(sizeof(char) + sizeof(int));

    memcpy(message_buffer, &op_code, sizeof(op_code));
    message.session_id = client_session;
    memcpy(message_buffer + sizeof(char), &message.session_id, sizeof(int));

    if (write(server_pipe, message_buffer, sizeof(char) + sizeof(int)) == -1) {
        return -1;
    }
    if (read(client_pipe, &success, sizeof(int)) == -1 || success == -1) {
        return -1;
    }
    free(message_buffer);
    return 0;
}
