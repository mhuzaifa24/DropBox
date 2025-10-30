#ifndef WORKER_POOL_H
#define WORKER_POOL_H

#include "common.h"
#include "task_queue.h"
#include "user_auth.h"
#include "file_ops.h"

typedef struct {
    pthread_t *threads;
    int thread_count;
    task_queue_t *task_queue;
    volatile int shutdown;
} worker_pool_t;

// Worker pool management
worker_pool_t* create_worker_pool(int thread_count, task_queue_t *queue);
void destroy_worker_pool(worker_pool_t *pool);
void shutdown_worker_pool(worker_pool_t *pool);

// Worker functions
void* worker_thread_handler(void *arg);
int execute_task(task_t *task);
void send_task_result_to_client(task_t *task);

#endif
