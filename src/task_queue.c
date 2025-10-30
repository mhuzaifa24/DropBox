#include "../include/task_queue.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

task_queue_t* create_task_queue(int capacity) {
    if (capacity <= 0) {
        return NULL;
    }
    
    task_queue_t *queue = malloc(sizeof(task_queue_t));
    if (!queue) {
        return NULL;
    }
    
    queue->tasks = malloc(sizeof(task_t) * capacity);
    if (!queue->tasks) {
        free(queue);
        return NULL;
    }
    
    queue->capacity = capacity;
    queue->size = 0;
    queue->front = 0;
    queue->rear = -1;
    
    // Initialize synchronization primitives
    if (pthread_mutex_init(&queue->lock, NULL) != 0) {
        free(queue->tasks);
        free(queue);
        return NULL;
    }
    
    if (pthread_cond_init(&queue->not_empty, NULL) != 0 ||
        pthread_cond_init(&queue->not_full, NULL) != 0) {
        pthread_mutex_destroy(&queue->lock);
        free(queue->tasks);
        free(queue);
        return NULL;
    }
    
    return queue;
}

void destroy_task_queue(task_queue_t *queue) {
    if (!queue) return;
    
    // Clean up any remaining tasks
    for (int i = 0; i < queue->size; i++) {
        int index = (queue->front + i) % queue->capacity;
        destroy_task(&queue->tasks[index]);
    }
    
    pthread_mutex_destroy(&queue->lock);
    pthread_cond_destroy(&queue->not_empty);
    pthread_cond_destroy(&queue->not_full);
    free(queue->tasks);
    free(queue);
}

// Deep copy function for tasks
task_t copy_task(const task_t *src) {
    task_t dest = {0};
    
    if (!src) return dest;
    
    // Copy basic fields
    dest.client_socket = src->client_socket;
    dest.operation = src->operation;
    dest.task_id = src->task_id;
    dest.data_size = src->data_size;
    dest.result_code = src->result_code;
    dest.result_size = src->result_size;
    dest.completed = src->completed;
    
    // Copy strings
    if (src->username[0] != '\0') {
        strncpy(dest.username, src->username, MAX_USERNAME_LEN - 1);
    }
    if (src->filename[0] != '\0') {
        strncpy(dest.filename, src->filename, MAX_FILENAME_LEN - 1);
    }
    
    // Copy file data (deep copy)
    if (src->file_data && src->data_size > 0) {
        dest.file_data = malloc(src->data_size);
        if (dest.file_data) {
            memcpy(dest.file_data, src->file_data, src->data_size);
        } else {
            // If allocation fails, zero out the size
            dest.data_size = 0;
        }
    }
    
    // Copy result data (deep copy)  
    if (src->result_data && src->result_size > 0) {
        dest.result_data = malloc(src->result_size);
        if (dest.result_data) {
            memcpy(dest.result_data, src->result_data, src->result_size);
        } else {
            dest.result_size = 0;
        }
    }
    
    // Initialize synchronization primitives for the copy
    pthread_mutex_init(&dest.result_lock, NULL);
    pthread_cond_init(&dest.result_ready, NULL);
    
    return dest;
}

int enqueue_task(task_queue_t *queue, const task_t *task) {
    if (!queue || !task) return ERROR;
    
    pthread_mutex_lock(&queue->lock);
    
    // Wait while queue is full
    while (queue->size >= queue->capacity) {
        pthread_cond_wait(&queue->not_full, &queue->lock);
    }
    
    // Use deep copy instead of shallow copy
    queue->tasks[queue->rear] = copy_task(task);
    queue->rear = (queue->rear + 1) % queue->capacity;
    queue->size++;
    
    // Signal that queue is not empty
    pthread_cond_signal(&queue->not_empty);
    pthread_mutex_unlock(&queue->lock);
    
    return SUCCESS;
}

task_t dequeue_task(task_queue_t *queue) {
    task_t empty_task = {0};
    
    if (!queue) return empty_task;
    
    pthread_mutex_lock(&queue->lock);
    
    // Wait while queue is empty
    while (queue->size <= 0) {
        pthread_cond_wait(&queue->not_empty, &queue->lock);
    }
    
    // Remove task from queue
    task_t task = queue->tasks[queue->front];
    queue->front = (queue->front + 1) % queue->capacity;
    queue->size--;
    
    // Signal that queue is not full
    pthread_cond_signal(&queue->not_full);
    pthread_mutex_unlock(&queue->lock);
    
    return task;
}

task_t create_task(int client_socket, const char *username, operation_t op, const char *filename) {
    task_t task = {0};
    task.client_socket = client_socket;
    task.operation = op;
    task.task_id = rand();
    
    if (username) {
        strncpy(task.username, username, MAX_USERNAME_LEN - 1);
    }
    
    if (filename) {
        strncpy(task.filename, filename, MAX_FILENAME_LEN - 1);
    }
    
    // Initialize result synchronization
    task.result_code = 0;
    task.result_data = NULL;
    task.result_size = 0;
    task.completed = 0;
    
    pthread_mutex_init(&task.result_lock, NULL);
    pthread_cond_init(&task.result_ready, NULL);
    
    return task;
}

void destroy_task(task_t *task) {
    if (!task) return;
    
    // Free file data if it exists
    if (task->file_data) {
        free(task->file_data);
        task->file_data = NULL;
    }
    
    // Free result data if it exists
    if (task->result_data) {
        free(task->result_data);
        task->result_data = NULL;
    }
    
    pthread_mutex_destroy(&task->result_lock);
    pthread_cond_destroy(&task->result_ready);
}

void set_task_result(task_t *task, int result_code, const char *result_data, size_t result_size) {
    if (!task) return;
    
    pthread_mutex_lock(&task->result_lock);
    
    task->result_code = result_code;
    
    // Free previous result data if it exists
    if (task->result_data) {
        free(task->result_data);
    }
    
    // Copy new result data if provided
    if (result_data && result_size > 0) {
        task->result_data = malloc(result_size);
        if (task->result_data) {
            memcpy(task->result_data, result_data, result_size);
            task->result_size = result_size;
        }
    } else {
        task->result_data = NULL;
        task->result_size = 0;
    }
    
    task->completed = 1;
    
    // Signal that result is ready
    pthread_cond_signal(&task->result_ready);
    pthread_mutex_unlock(&task->result_lock);
}
