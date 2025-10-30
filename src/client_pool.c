#include "../include/client_pool.h"
#include "../include/file_ops.h"
#include "../include/utils.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define MAX_COMMAND_LEN 1024
#define MAX_RESPONSE_LEN 2048

client_pool_t* create_client_pool(int thread_count, client_queue_t *c_queue, task_queue_t *t_queue) {
    if (thread_count <= 0 || !c_queue || !t_queue) {
        return NULL;
    }
    
    client_pool_t *pool = malloc(sizeof(client_pool_t));
    if (!pool) {
        return NULL;
    }
    
    pool->threads = malloc(sizeof(pthread_t) * thread_count);
    if (!pool->threads) {
        free(pool);
        return NULL;
    }
    
    pool->thread_count = thread_count;
    pool->client_queue = c_queue;
    pool->task_queue = t_queue;
    pool->shutdown = 0;
    
    // Create client threads
    for (int i = 0; i < thread_count; i++) {
        if (pthread_create(&pool->threads[i], NULL, client_thread_handler, pool) != 0) {
            // Cleanup on failure
            for (int j = 0; j < i; j++) {
                pthread_cancel(pool->threads[j]);
            }
            free(pool->threads);
            free(pool);
            return NULL;
        }
        printf("Created client thread %d\n", i);
    }
    
    printf("✅ Client pool created with %d threads\n", thread_count);
    return pool;
}

void destroy_client_pool(client_pool_t *pool) {
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
    printf("✅ Client pool destroyed\n");
}

void shutdown_client_pool(client_pool_t *pool) {
    if (pool) {
        pool->shutdown = 1;
    }
}

int handle_client_authentication(int client_socket, char *username) {
    char buffer[MAX_COMMAND_LEN];
    char response[MAX_RESPONSE_LEN];
    
    // Send authentication prompt
    const char *prompt = "AUTH: Enter 'SIGNUP <username> <password>' or 'LOGIN <username> <password>': ";
    if (send_all(client_socket, prompt, strlen(prompt)) != 0) {
        return AUTH_FAILED;
    }
    
    // Receive authentication command
    ssize_t bytes_received = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
    if (bytes_received <= 0) {
        return AUTH_FAILED;
    }
    
    buffer[bytes_received] = '\0';
    printf("Auth command received: %s", buffer);
    
    // Parse authentication command
    char command[20], user[50], password[50];
    if (sscanf(buffer, "%19s %49s %49s", command, user, password) != 3) {
        const char *error_msg = "ERROR: Invalid authentication format. Use 'SIGNUP username password' or 'LOGIN username password'\n";
        send_all(client_socket, error_msg, strlen(error_msg));
        return AUTH_FAILED;
    }
    
    // Process authentication
    int auth_result;
    if (strcasecmp(command, "SIGNUP") == 0) {
        auth_result = user_signup(user, password);
        snprintf(response, sizeof(response), "SIGNUP: %s\n", 
                auth_result == SUCCESS ? "SUCCESS" : 
                auth_result == USER_EXISTS ? "USER_EXISTS" : "FAILED");
    } else if (strcasecmp(command, "LOGIN") == 0) {
        auth_result = user_login(user, password);
        snprintf(response, sizeof(response), "LOGIN: %s\n", 
                auth_result == SUCCESS ? "SUCCESS" : "FAILED");
    } else {
        const char *error_msg = "ERROR: Unknown command. Use SIGNUP or LOGIN\n";
        send_all(client_socket, error_msg, strlen(error_msg));
        return AUTH_FAILED;
    }
    
    // Send authentication result
    if (send_all(client_socket, response, strlen(response)) != 0) {
        return AUTH_FAILED;
    }
    
    if (auth_result == SUCCESS) {
        // Send welcome message
        const char *welcome_msg = "AUTH_SUCCESS: Welcome! Commands: UPLOAD, DOWNLOAD, DELETE, LIST, QUIT\n";
        if (send_all(client_socket, welcome_msg, strlen(welcome_msg)) != 0) {
            return AUTH_FAILED;
        }
        
        strncpy(username, user, MAX_USERNAME_LEN - 1);
        return SUCCESS;
    }
    
    return AUTH_FAILED;
}

int parse_client_command(int client_socket, const char *command, char *username, task_t *out_task) {  // Renamed parameter
    char cmd[20], filename[256];
    
    if (sscanf(command, "%19s %255s", cmd, filename) < 1) {
        return ERROR;
    }
    
    // Determine operation type
    operation_t op;
    if (strcasecmp(cmd, "UPLOAD") == 0) {
        op = UPLOAD;
    } else if (strcasecmp(cmd, "DOWNLOAD") == 0) {
        op = DOWNLOAD;
    } else if (strcasecmp(cmd, "DELETE") == 0) {
        op = DELETE;
    } else if (strcasecmp(cmd, "LIST") == 0) {
        op = LIST;
    } else {
        return ERROR; // Unknown command
    }
    
    // Create task using the output parameter
    *out_task = create_task(client_socket, username, op, (op != LIST) ? filename : NULL);
    
    // For UPLOAD, receive file data
    if (op == UPLOAD) {
        const char *msg = "READY: Send file data\n";
        send(client_socket, msg, strlen(msg), 0);
        
        // Receive file data with bounds checking
        char upload_buffer[4096];
        ssize_t data_received = recv(client_socket, upload_buffer, sizeof(upload_buffer) - 1, 0);
        
        if (data_received > 0) {
            upload_buffer[data_received] = '\0';
            
            // Store the file data
            out_task->file_data = malloc(data_received);
            if (out_task->file_data) {
                memcpy(out_task->file_data, upload_buffer, data_received);
                out_task->data_size = data_received;
            }
        }
    }
    
    return SUCCESS;
}

void* client_thread_handler(void *arg) {
    client_pool_t *pool = (client_pool_t *)arg;
    char username[MAX_USERNAME_LEN];
    
    while (!pool->shutdown) {
        // Get client from queue (blocks if queue is empty)
        int client_socket = dequeue_client(pool->client_queue);
        
        if (client_socket < 0) {
            continue; // Invalid socket, continue waiting
        }
        
        printf("Client thread handling socket %d\n", client_socket);
        
        // Handle authentication
        if (handle_client_authentication(client_socket, username) != SUCCESS) {
            printf("Authentication failed for socket %d\n", client_socket);
            close(client_socket);
            continue;
        }
        
        printf("User '%s' authenticated on socket %d\n", username, client_socket);
        
        // Command processing loop
        char command_buffer[MAX_COMMAND_LEN];
        const char *welcome_msg = "AUTH_SUCCESS: Welcome! Commands: UPLOAD, DOWNLOAD, DELETE, LIST, QUIT\n";
        send(client_socket, welcome_msg, strlen(welcome_msg), 0);
        
        int session_active = 1;
        while (session_active && !pool->shutdown) {
            // Receive command from client
            ssize_t bytes_received = recv(client_socket, command_buffer, sizeof(command_buffer) - 1, 0);
            
            if (bytes_received <= 0) {
                printf("Client disconnected: socket %d\n", client_socket);
                break;
            }
            
            command_buffer[bytes_received] = '\0';
            printf("Command from %s: %s", username, command_buffer);
            
            // Check for QUIT command
            if (strncasecmp(command_buffer, "QUIT", 4) == 0) {
                const char *bye_msg = "GOODBYE: Session ended\n";
                send(client_socket, bye_msg, strlen(bye_msg), 0);
                session_active = 0;
                continue;
            }
            
            // Parse command and create task
            task_t current_task;  // Use different variable name
            if (parse_client_command(client_socket, command_buffer, username, &current_task) != SUCCESS) {
                const char *error_msg = "ERROR: Invalid command. Use: UPLOAD, DOWNLOAD, DELETE, LIST, QUIT\n";
                send(client_socket, error_msg, strlen(error_msg), 0);
                continue;
            }
            
            // Send task to worker pool via task queue
            if (enqueue_task(pool->task_queue, &current_task) != SUCCESS) {
                const char *error_msg = "ERROR: Server busy, try again later\n";
                send(client_socket, error_msg, strlen(error_msg), 0);
                destroy_task(&current_task);
                continue;
            }
            
            const char *ack_msg = "ACK: Task queued for processing\n";
            send(client_socket, ack_msg, strlen(ack_msg), 0);
            
            printf("Task queued for user %s: operation=%d, file=%s\n", 
                   username, current_task.operation, current_task.filename);
        }
        
        close(client_socket);
        printf("Client session ended for user '%s'\n", username);
    }
    
    printf("Client thread exiting\n");
    return NULL;
}
