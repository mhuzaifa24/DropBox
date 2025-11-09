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
        set_task_result(task, AUTH_FAILED, "User not authenticated.\n", strlen("User not authenticated.\n") + 1);
        send_task_result_to_client(task);
        return AUTH_FAILED;
    }

    int result_code = SUCCESS;
    char *response = NULL;

    switch (task->operation) {

        case UPLOAD: {
            printf("[WORKER] UPLOAD: %s\n", task->filename);
            if (!task->file_data || task->data_size == 0) {
                set_task_result(task, FILE_OP_ERROR, "Upload failed: no data.\n", strlen("Upload failed: no data.\n") + 1);
            } else {
                int res = handle_upload(user, task->filename, task->file_data, task->data_size);
                if (res == FILE_OP_SUCCESS)
                    set_task_result(task, FILE_OP_SUCCESS, "Upload successful.\n", strlen("Upload successful.\n") + 1);
                else
                    set_task_result(task, FILE_OP_ERROR, "Upload failed.\n", strlen("Upload failed.\n") + 1);
            }
            break;
        }

        case DOWNLOAD: {
            printf("[WORKER] DOWNLOAD: %s\n", task->filename);
            char *data = NULL;
            size_t size = 0;
            int res = handle_download(user, task->filename, &data, &size);
            if (res == FILE_OP_SUCCESS)
                set_task_result(task, FILE_OP_SUCCESS, data, size);
            else
                set_task_result(task, FILE_OP_ERROR, "Download failed.\n", strlen("Download failed.\n") + 1);
            free(data);
            break;
        }

        case DELETE: {
            printf("[WORKER] DELETE: %s\n", task->filename);
            int res = handle_delete(user, task->filename);
            if (res == FILE_OP_SUCCESS)
                set_task_result(task, FILE_OP_SUCCESS, "Delete successful.\n", strlen("Delete successful.\n") + 1);
            else if (res == FILE_OP_NOT_FOUND)
                set_task_result(task, FILE_OP_NOT_FOUND, "Delete failed: file not found.\n", strlen("Delete failed: file not found.\n") + 1);
            else
                set_task_result(task, FILE_OP_ERROR, "Delete failed: error occurred.\n", strlen("Delete failed: error occurred.\n") + 1);
            break;
        }

        case LIST: {
            printf("[WORKER] LIST for user: %s\n", task->username);
            int file_count = 0;
            char **files = handle_list(user, &file_count);

            if (files && file_count > 0) {
                // Build result string
                size_t buffer_size = 0;
                for (int i = 0; i < file_count; i++)
                    buffer_size += strlen(files[i]) + 2; // + newline

                buffer_size += 32;
                char *list_result = malloc(buffer_size);
                if (!list_result) {
                    set_task_result(task, FILE_OP_ERROR, "Memory allocation failed.\n", strlen("Memory allocation failed.\n") + 1);
                    break;
                }

                strcpy(list_result, "Files:\n");
                for (int i = 0; i < file_count; i++) {
                    strcat(list_result, files[i]);
                    strcat(list_result, "\n");
                    free(files[i]); // free individual filename
                }
                free(files);

                set_task_result(task, FILE_OP_SUCCESS, list_result, strlen(list_result) + 1);
                free(list_result);
            } else {
                set_task_result(task, FILE_OP_SUCCESS, "No files found.\n", strlen("No files found.\n") + 1);
            }
            break;
        }

        default:
            set_task_result(task, ERROR, "Unknown operation.\n", strlen("Unknown operation.\n") + 1);
            break;
    }

    send_task_result_to_client(task);
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

