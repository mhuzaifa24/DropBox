#ifndef TASK_QUEUE_H
#define TASK_QUEUE_H

#include "common.h"

typedef enum { 
    UPLOAD, 
    DOWNLOAD, 
    DELETE, 
    LIST 
} operation_t;

typedef struct {
    int client_socket;
    char username[MAX_USERNAME_LEN];
    operation_t operation;
    char filename[MAX_FILENAME_LEN];
    char *file_data;
    size_t data_size;
    int task_id;
    
    // Result fields for worker→client communication
    int result_code;
    char *result_data;
    size_t result_size;
    int completed;
    pthread_mutex_t result_lock;
    pthread_cond_t result_ready;
} task_t;

typedef struct {
    task_t *tasks;
    int capacity;
    int size;
    int front;
    int rear;
    pthread_mutex_t lock;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
} task_queue_t;

// Task queue functions - UPDATED SIGNATURES
task_queue_t* create_task_queue(int capacity);
void destroy_task_queue(task_queue_t *queue);
int enqueue_task(task_queue_t *queue, const task_t *task);  // Changed to pointer
task_t dequeue_task(task_queue_t *queue);

// Task management functions
task_t create_task(int client_socket, const char *username, operation_t op, const char *filename);
void destroy_task(task_t *task);
void set_task_result(task_t *task, int result_code, const char *result_data, size_t result_size);

// Add deep copy function
task_t copy_task(const task_t *src);

#endif
