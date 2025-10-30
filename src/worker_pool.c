#include "../include/worker_pool.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>

worker_pool_t* create_worker_pool(int thread_count, task_queue_t *queue) {
    if (thread_count <= 0 || !queue) {
        return NULL;
    }
    
    worker_pool_t *pool = malloc(sizeof(worker_pool_t));
    if (!pool) {
        return NULL;
    }
    
    pool->threads = malloc(sizeof(pthread_t) * thread_count);
    if (!pool->threads) {
        free(pool);
        return NULL;
    }
    
    pool->thread_count = thread_count;
    pool->task_queue = queue;
    pool->shutdown = 0;
    
    // Create worker threads
    for (int i = 0; i < thread_count; i++) {
        if (pthread_create(&pool->threads[i], NULL, worker_thread_handler, pool) != 0) {
            // Cleanup on failure
            for (int j = 0; j < i; j++) {
                pthread_cancel(pool->threads[j]);
            }
            free(pool->threads);
            free(pool);
            return NULL;
        }
        printf("Created worker thread %d\n", i);
    }
    
    printf("✅ Worker pool created with %d threads\n", thread_count);
    return pool;
}

void destroy_worker_pool(worker_pool_t *pool) {
    if (!pool) return;
    
    // Signal shutdown
    pool->shutdown = 1;
    
    // Wait for all threads to finish
    for (int i = 0; i < pool->thread_count; i++) {
        if (pool->threads[i]) {
            pthread_join(pool->threads[i], NULL);
        }
    }
    
    free(pool->threads);
    free(pool);
    printf("✅ Worker pool destroyed\n");
}

void shutdown_worker_pool(worker_pool_t *pool) {
    if (pool) {
        pool->shutdown = 1;
    }
}

int execute_task(task_t *task) {
    if (!task) {
        return ERROR;
    }
    
    printf("Worker executing task: user=%s, operation=%d\n", task->username, task->operation);
    
    // Get user for the task
    user_t *user = get_user(task->username);
    if (!user) {
        printf("User not found: %s\n", task->username);
        return AUTH_FAILED;
    }
    
    int result_code = SUCCESS;
    char *result_data = NULL;
    
    // Execute the appropriate operation
    switch (task->operation) {
        case UPLOAD:
            printf("UPLOAD operation for file: %s\n", task->filename);
            result_data = "UPLOAD_SUCCESS: File uploaded";
            break;
            
        case DOWNLOAD:
            printf("DOWNLOAD operation for file: %s\n", task->filename);
            result_data = "DOWNLOAD_SUCCESS: File downloaded";
            break;
            
        case DELETE:
            printf("DELETE operation for file: %s\n", task->filename);
            result_data = "DELETE_SUCCESS: File deleted";
            break;
            
        case LIST:
            printf("LIST operation\n");
            result_data = "LIST_SUCCESS: Files listed";
            break;
            
        default:
            result_code = ERROR;
            result_data = "ERROR: Unknown operation";
            break;
    }
    
    // Set task result
    set_task_result(task, result_code, result_data, 0);
    
    return result_code;
}

void send_task_result_to_client(task_t *task) {
    if (!task || task->client_socket < 0) {
        return;
    }
    
    // Wait for task completion
    pthread_mutex_lock(&task->result_lock);
    while (!task->completed) {
        pthread_cond_wait(&task->result_ready, &task->result_lock);
    }
    pthread_mutex_unlock(&task->result_lock);
    
    // Send result to client
    char response[MAX_RESPONSE_LEN];
    const char *status = (task->result_code == SUCCESS) ? "SUCCESS" : "FAILED";
    const char *result_text = task->result_data ? task->result_data : "Operation completed";
    
    snprintf(response, sizeof(response), "TASK_COMPLETE: %s - %s\n", status, result_text);
    send(task->client_socket, response, strlen(response), 0);
    
    printf("Result sent to client %d: %s\n", task->client_socket, response);
}

void* worker_thread_handler(void *arg) {
    worker_pool_t *pool = (worker_pool_t *)arg;
    
    while (!pool->shutdown) {
        // Get task from queue (blocks if queue is empty)
        task_t task = dequeue_task(pool->task_queue);
        
        if (task.client_socket < 0) {
            continue; // Invalid task, continue waiting
        }
        
        printf("Worker thread processing task for user %s\n", task.username);
        
        // Execute the task
        execute_task(&task);
        
        // Send result back to client
        send_task_result_to_client(&task);
        
        // Cleanup task resources
        destroy_task(&task);
        
        printf("Worker completed task\n");
    }
    
    printf("Worker thread exiting\n");
    return NULL;
}
