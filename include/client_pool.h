#ifndef CLIENT_POOL_H
#define CLIENT_POOL_H

#include "common.h"
#include "client_queue.h"
#include "task_queue.h"
#include "user_auth.h"

typedef struct {
    pthread_t *threads;
    int thread_count;
    client_queue_t *client_queue;
    task_queue_t *task_queue;
    volatile int shutdown;
} client_pool_t;

// Client pool management
client_pool_t* create_client_pool(int thread_count, client_queue_t *c_queue, task_queue_t *t_queue);
void destroy_client_pool(client_pool_t *pool);
void shutdown_client_pool(client_pool_t *pool);

// Client handler functions
void* client_thread_handler(void *arg);
int handle_client_authentication(int client_socket, char *username);
int parse_client_command(int client_socket, const char *command, char *username, task_t *task);

#endif
