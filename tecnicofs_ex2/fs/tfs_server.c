#include "operations.h"
#include "fcntl.h"
#include "unistd.h"
#include <sys/stat.h>
#include <string.h>
#include <stdlib.h>

Session sessions[MAX_CLIENTS];
static char free_sessions[MAX_CLIENTS];
pthread_t workers[MAX_CLIENTS];
static pthread_mutex_t global_mutex;
int aux_session_ids[MAX_CLIENTS];
union Message client_messages[MAX_CLIENTS];

int failed = -1;
int success = 0;

int inform_failed_operation(unsigned int session_id) {
    if (write(sessions[session_id].pipe, &failed, sizeof(int)) == -1) {
        return -1;
    }
    return 0;
}

int mount_pipe(struct Mount message) {
    free_sessions[message.session_id] = TAKEN;
    if ((sessions[message.session_id].pipe = open(message.client_pipe_path, O_WRONLY)) == -1) {
        return -1;
    }
    if (write(sessions[message.session_id].pipe, &message.session_id, sizeof(int)) == -1) {
        return -1;
    }
    return 0;
}

int unmount_pipe(struct Unmount message) {
    if (write(sessions[message.session_id].pipe, &success, sizeof(success)) == -1) {
        return -1;
    }
    free_sessions[message.session_id] = FREE;
    return 0;
}

int open_file(struct Open message) {
    char *name = message.name;
    int flags = message.flags;
    int fhandle;
    if ((fhandle = tfs_open(name, flags)) == -1) {
        if (inform_failed_operation(message.session_id) == -1) {
            return -1;
        }
    }
    if (write(sessions[message.session_id].pipe, &fhandle, sizeof(int)) == -1){
        if (inform_failed_operation(message.session_id) == -1) {
            return -1;
        }
    }
    return 0;
}

int find_free_session_id() {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (free_sessions[i] == FREE) {
            return i;
        }
    }
    return -1;
}

int close_file(struct Close message) {
    int fhandle = message.fhandle;
    if (tfs_close(fhandle) == -1) {
        if (inform_failed_operation(message.session_id) == -1) {
            return -1;
        }
    }
    if (write(sessions[message.session_id].pipe, &success, sizeof(int)) == -1) {
        return -1;
    }
    return 0;
}

int write_file(struct Write message, void const* buffer) {
    int fhandle = message.fhandle;
    ssize_t len;
    if ((len = tfs_write(fhandle, buffer, message.len)) == -1) {
        if (inform_failed_operation(message.session_id) == -1) {
            return -1;
        }
    }
    if (write(sessions[message.session_id].pipe, &len, sizeof(ssize_t)) == -1) {
        return -1;
    }
    return 0;
}

int read_file(struct Read message) {
    int fhandle = message.fhandle;
    void* buffer = (void*) malloc(message.len);
    if (tfs_read(fhandle, buffer, message.len) == -1) {
        if (inform_failed_operation(message.session_id) == -1) {
            return -1;
        }
    }
    if (write(sessions[message.session_id].pipe, buffer, message.len) == -1) {
        return -1;
    }
    return 0;
}

int destroy_os(struct Shutdown message) {
    if (tfs_destroy_after_all_closed() == -1) {
        if (inform_failed_operation(message.session_id) == -1) {
            return -1;
        }
    }
    if (write(sessions[message.session_id].pipe, &success, sizeof(int)) == -1) {
        return -1;
    }
    return 0;
}

void init_table(){
    for (size_t i = 0; i < MAX_CLIENTS; i++) {
        free_sessions[i] = FREE;
    }
}

void *create_worker(void* worker) {
    unsigned int worker_id = *((unsigned int*) worker);
    struct Mount m_message;
    struct Unmount u_message;
    struct Open o_message;
    struct Close c_message;
    struct Write w_message;
    struct Read r_message;
    struct Shutdown s_message;
    
    while (1) {
        if (pthread_mutex_lock(&global_mutex) != 0) {
            exit(0);
        }
        while (sessions[worker_id].buffer == NULL) {
            pthread_cond_wait(&sessions[worker_id].sent_all, &global_mutex);
        }
        char op_code = sessions[worker_id].buffer[0];
        
        switch (op_code) {

            case '1': // Mount
                m_message.session_id = worker_id;
                memset(&m_message.client_pipe_path, 0, MAX_FILE_NAME);
                memcpy(&m_message.client_pipe_path, sessions[worker_id].buffer + sizeof(char), MAX_FILE_NAME);
                if (mount_pipe(m_message) == -1) {
                    if (pthread_mutex_unlock(&global_mutex) != 0) {
                        exit(0);
                    }
                    return NULL;
                }
                if (pthread_mutex_unlock(&global_mutex) != 0) {
                    exit(0);
                }
                break;

            case '2': // Unmount
                u_message.session_id = worker_id;
                if (unmount_pipe(u_message) == -1) {
                    if (pthread_mutex_unlock(&global_mutex) != 0) {
                        exit(0);
                    }
                    return NULL;
                }
                if (pthread_mutex_unlock(&global_mutex) != 0) {
                    exit(0);
                }
                break;

            case '3': // Open 
                o_message.session_id = worker_id;
                memcpy(&o_message.name, sessions[worker_id].buffer + sizeof(char), MAX_FILE_NAME);
                memcpy(&o_message.flags, sessions[worker_id].buffer + sizeof(char) + MAX_FILE_NAME, sizeof(int));
                if (open_file(o_message) == -1) {
                    if(inform_failed_operation(o_message.session_id) == -1) {
                        if (pthread_mutex_unlock(&global_mutex) != 0) {
                            exit(0);
                        }
                        return NULL;
                    }
                }
                if (pthread_mutex_unlock(&global_mutex) != 0) {
                    exit(0);
                }
                break;
                
            case '4': // Close
                c_message.session_id = worker_id;
                memcpy(&c_message.fhandle, sessions[worker_id].buffer + sizeof(char), sizeof(int));

                if (close_file(c_message) == -1) {
                    if (pthread_mutex_unlock(&global_mutex) != 0) {
                        exit(0);
                    }
                    return NULL;
                }
                if (pthread_mutex_unlock(&global_mutex) != 0) {
                    exit(0);
                }
                break;

            case '5': // Write
                w_message.session_id = worker_id;
                memcpy(&w_message.fhandle, sessions[worker_id].buffer + sizeof(char), sizeof(int));
                memcpy(&w_message.len, sessions[worker_id].buffer + sizeof(char) + sizeof(int), sizeof(size_t));
                char* temp_buffer = (char*) malloc(w_message.len * sizeof(char));
                memcpy(temp_buffer, sessions[worker_id].buffer + sizeof(char) + sizeof(int) + sizeof(size_t), w_message.len);
                if (write_file(w_message, temp_buffer) == -1) {
                    if (pthread_mutex_unlock(&global_mutex) != 0) {
                        exit(0);
                    }
                    return NULL;
                }
                free(temp_buffer);
                if (pthread_mutex_unlock(&global_mutex) != 0) {
                    exit(0);
                }
                break;

            case '6': // Read
                r_message.session_id = worker_id;
                memcpy(&r_message.fhandle, sessions[worker_id].buffer + sizeof(char), sizeof(int));
                memcpy(&r_message.len, sessions[worker_id].buffer + sizeof(char) + sizeof(int), sizeof(size_t));
                if (read_file(r_message) == -1) {
                    if (pthread_mutex_unlock(&global_mutex) != 0) {
                        exit(0);
                    }
                    return NULL;
                }
                if (pthread_mutex_unlock(&global_mutex) != 0) {
                    exit(0);
                }
                break;

            case '7': // Shutdown
                s_message.session_id = worker_id;
                if (destroy_os(s_message) == -1) { 
                    if (pthread_mutex_unlock(&global_mutex) != 0) {
                        exit(0);
                    }
                    return NULL;
                }
                if (pthread_mutex_unlock(&global_mutex) != 0) {
                    exit(0);
                }
                break;

            default:
                if (pthread_mutex_unlock(&global_mutex) != 0) {
                    exit(0);
                }
                break;
        }
        free(sessions[worker_id].buffer);
        sessions[worker_id].buffer = NULL;
    }
    return NULL;
}

int main(int argc, char **argv) {
    char op_code = ' ';
    struct Mount m_message;
    struct Unmount u_message;
    struct Open o_message;
    struct Close c_message;
    struct Write w_message;
    struct Read r_message;
    struct Shutdown s_message;
    int server_pipe;
    
    init_table();
    if (pthread_mutex_init(&global_mutex, NULL) == -1) {
        return -1;
    }
    if (argc < 2) {
        printf("Please specify the pathname of the server's pipe.\n");
        return 1;
    }
    char *pipename = argv[1];
    unlink(pipename);
    if (mkfifo(pipename, 0777) != 0) {
        return -1;
    }
    for (int i = 0; i < MAX_CLIENTS; i++) {
        pthread_create(&workers[i], NULL, &create_worker, (void*)&aux_session_ids[i]);
    }

    printf("Starting TecnicoFS server with pipe called %s\n", pipename);
    if ((server_pipe = open(pipename, O_RDONLY)) == -1) {
        return -1;
    }
    tfs_init();

    while (1) {
        if (read(server_pipe, &op_code, sizeof(char)) == -1) {
            return -1;
        }
        switch (op_code) {
            case '1': // Mount
                if (pthread_mutex_lock(&global_mutex) == -1) {
                    return -1;
                }
                int session_id_aux;
                memset(&m_message.client_pipe_path, 0, MAX_FILE_NAME);
                if ((read(server_pipe, &m_message.client_pipe_path, MAX_FILE_NAME)) == -1) {
                    if (pthread_mutex_unlock(&global_mutex) == -1) {
                        return -1;
                    }
                    return -1;
                }
                session_id_aux = find_free_session_id();
                if (session_id_aux != -1) {
                    sessions[session_id_aux].buffer = (char*) malloc(sizeof(char) + sizeof(int) + MAX_FILE_NAME);
                    memset(sessions[session_id_aux].buffer, 0, sizeof(char) + sizeof(int) + MAX_FILE_NAME);
                    memcpy(sessions[session_id_aux].buffer, &op_code, sizeof(char));
                    memcpy(sessions[session_id_aux].buffer + sizeof(char), &m_message.client_pipe_path, MAX_FILE_NAME);
                    pthread_cond_signal(&sessions[session_id_aux].sent_all);
                }
                else {
                    int temp_pipe = open(m_message.client_pipe_path, O_WRONLY);
                    if (write(temp_pipe, &failed, sizeof(int)) == -1) {
                        if (pthread_mutex_unlock(&global_mutex) == -1) {
                            return -1;
                        }
                        return -1;
                    }
                    close(temp_pipe);
                }
                if (pthread_mutex_unlock(&global_mutex) == -1) {
                    return -1;
                }
                break;

            case '2': // Unmount
                if (pthread_mutex_lock(&global_mutex) == -1) {
                    return -1;
                }
                if (read(server_pipe, &u_message.session_id, sizeof(int))) {
                    if (pthread_mutex_unlock(&global_mutex) == -1) {
                        return -1;
                    }
                    return -1;
                }
                sessions[u_message.session_id].buffer = (char*) malloc(sizeof(char));
                memcpy(sessions[u_message.session_id].buffer, &op_code, sizeof(char));
                pthread_cond_signal(&sessions[u_message.session_id].sent_all);
                if (pthread_mutex_unlock(&global_mutex) == -1) {
                    return -1;
                }
                break;

            case '3': // Open 
                if (pthread_mutex_lock(&global_mutex) == -1) {
                    return -1;
                }
                if (read(server_pipe, &o_message.session_id, sizeof(int)) == -1) {
                    if (pthread_mutex_unlock(&global_mutex) == -1) {
                        return -1;
                    }
                    return -1;
                }
                sessions[o_message.session_id].buffer = (char*) malloc(sizeof(char) + sizeof(int) + MAX_FILE_NAME);
                memcpy(sessions[o_message.session_id].buffer, &op_code, sizeof(char));
                if (read(server_pipe, sessions[o_message.session_id].buffer + sizeof(char), MAX_FILE_NAME) == -1) {
                    if (pthread_mutex_unlock(&global_mutex) == -1) {
                        return -1;
                    }
                    return -1;
                }

                if (read(server_pipe, &o_message.flags, sizeof(int)) == -1) {
                    if (pthread_mutex_unlock(&global_mutex) == -1) {
                        return -1;
                    }
                    return -1;
                }
                memcpy(sessions[o_message.session_id].buffer + sizeof(char) + MAX_FILE_NAME, &o_message.flags, sizeof(int));
                pthread_cond_signal(&sessions[o_message.session_id].sent_all);
                if (pthread_mutex_unlock(&global_mutex) == -1) {
                    return -1;
                }
                break;
                
            case '4': // Close
                if (pthread_mutex_lock(&global_mutex) == -1) {
                    return -1;
                }
                if (read(server_pipe, &c_message.session_id, sizeof(int)) == -1) {
                    if (pthread_mutex_unlock(&global_mutex) == -1) {
                        return -1;
                    }
                    return -1;
                }
                sessions[c_message.session_id].buffer = (char*) malloc(sizeof(char) + sizeof(int));
                memcpy(sessions[c_message.session_id].buffer, &op_code, sizeof(char));
                if (read(server_pipe, sessions[c_message.session_id].buffer + sizeof(char), sizeof(int)) == -1) {
                    if (pthread_mutex_unlock(&global_mutex) == -1) {
                        return -1;
                    }
                    return -1;
                }
                pthread_cond_signal(&sessions[c_message.session_id].sent_all);
                if (pthread_mutex_unlock(&global_mutex) == -1) {
                    return -1;
                }
                break;

            case '5': // Write
                if (pthread_mutex_lock(&global_mutex) == -1) {
                    return -1;
                }
                if (read(server_pipe, &w_message.session_id, sizeof(int)) == -1) {
                    if (pthread_mutex_unlock(&global_mutex) == -1) {
                        return -1;
                    }
                    return -1;
                }
                if (read(server_pipe, &w_message.fhandle, sizeof(int)) == -1) {
                    if (pthread_mutex_unlock(&global_mutex) == -1) {
                        return -1;
                    }
                    return -1;
                }
                if (read(server_pipe, &w_message.len, sizeof(size_t)) == -1) {
                    if (pthread_mutex_unlock(&global_mutex) == -1) {
                        return -1;
                    }
                    return -1;
                }
                sessions[w_message.session_id].buffer = (char*) malloc(sizeof(char) + sizeof(int) + w_message.len);
                memcpy(sessions[w_message.session_id].buffer, &op_code, sizeof(char));
                memcpy(sessions[w_message.session_id].buffer + sizeof(char), &w_message.fhandle, sizeof(int));
                if (read(server_pipe, sessions[w_message.session_id].buffer + sizeof(char) + sizeof(int), w_message.len) == -1) {
                    if (pthread_mutex_unlock(&global_mutex) == -1) {
                        return -1;
                    }
                    return -1;
                }
                pthread_cond_signal(&sessions[w_message.session_id].sent_all);
                if (pthread_mutex_unlock(&global_mutex) == -1) {
                    return -1;
                }
                break;

            case '6': // Read
                if (pthread_mutex_lock(&global_mutex) == -1) {
                    return -1;
                }
                if(read(server_pipe, &r_message.session_id, sizeof(int)) == -1) {
                    if (pthread_mutex_unlock(&global_mutex) == -1) {
                        return -1;
                    }
                    return -1;
                }
                sessions[r_message.session_id].buffer = (char*) malloc(sizeof(char) + sizeof(int) + sizeof(size_t));
                memcpy(sessions[r_message.session_id].buffer, &op_code, sizeof(char));
                if(read(server_pipe, sessions[r_message.session_id].buffer + sizeof(char), sizeof(int)) == -1){
                    return -1;
                }
                if(read(server_pipe, sessions[r_message.session_id].buffer + sizeof(char) + sizeof(int), sizeof(size_t)) == -1){
                    return -1;
                }
                pthread_cond_signal(&sessions[r_message.session_id].sent_all);
                if (pthread_mutex_unlock(&global_mutex) == -1) {
                    return -1;
                }
                break;

            case '7': // Shutdown
                if (pthread_mutex_lock(&global_mutex) == -1) {
                    return -1;
                }
                if (read(server_pipe, &s_message.session_id, sizeof(int)) == -1) {
                    if (pthread_mutex_unlock(&global_mutex) == -1) {
                        return -1;
                    }
                    return -1;
                }
                sessions[s_message.session_id].buffer = (char*) malloc(sizeof(char));
                memcpy(sessions[s_message.session_id].buffer, &op_code, sizeof(char));
                pthread_cond_signal(&sessions[s_message.session_id].sent_all);
                if (pthread_mutex_unlock(&global_mutex) == -1) {
                    return -1;
                }
                break;

            default:
                break;
        }
    }
    for (int i = 0; i < MAX_CLIENTS; i++) {
        pthread_join(workers[i], NULL);
    }
    if (pthread_mutex_destroy(&global_mutex) == -1) {
        return -1;
    }
    close(server_pipe);
    return 0;
}