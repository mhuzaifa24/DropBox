#include "../include/client_pool.h"
#include "../include/file_ops.h"
#include "../include/utils.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#define MAX_COMMAND_LEN 1024
#define MAX_RESPONSE_LEN 2048

client_pool_t* create_client_pool(int thread_count, client_queue_t *c_queue, task_queue_t *t_queue) {
    if (thread_count <= 0 || !c_queue || !t_queue) {
        return NULL;
    }

    client_pool_t *pool = malloc(sizeof(client_pool_t));
    if (!pool) return NULL;

    pool->threads = malloc(sizeof(pthread_t) * thread_count);
    if (!pool->threads) { free(pool); return NULL; }

    pool->thread_count = thread_count;
    pool->client_queue = c_queue;
    pool->task_queue = t_queue;
    pool->shutdown = 0;

    for (int i = 0; i < thread_count; i++) {
        if (pthread_create(&pool->threads[i], NULL, client_thread_handler, pool) != 0) {
            for (int j = 0; j < i; j++) pthread_cancel(pool->threads[j]);
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
    pool->shutdown = 1;
    for (int i = 0; i < pool->thread_count; i++) {
        if (pool->threads[i]) pthread_join(pool->threads[i], NULL);
    }
    free(pool->threads);
    free(pool);
    printf("✅ Client pool destroyed\n");
}

void shutdown_client_pool(client_pool_t *pool) {
    if (pool) pool->shutdown = 1;
}

int handle_client_authentication(int client_socket, char *username) {
    char buffer[MAX_COMMAND_LEN];
    char response[MAX_RESPONSE_LEN];

    // Loop until authenticated or connection closes
    while (1) {
        const char *prompt = "AUTH->  Enter:\nSIGNUP <username> <password> \nLOGIN <username> <password>\n";
        if (send_all(client_socket, prompt, strlen(prompt)) != 0) return AUTH_FAILED;

        ssize_t bytes = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
        if (bytes <= 0) return AUTH_FAILED;
        buffer[bytes] = '\0';

        char command[20], user[ MAX_USERNAME_LEN ], password[MAX_PASSWORD_LEN];
        if (sscanf(buffer, "%19s %49s %49s", command, user, password) != 3) {
            const char *error_msg = "ERROR: Invalid authentication format. \n Use 'SIGNUP username password' or 'LOGIN username password'\n";
            send_all(client_socket, error_msg, strlen(error_msg));
            continue; // prompt again
        }

        int auth_result = AUTH_FAILED;
        if (strcasecmp(command, "SIGNUP") == 0) {
            auth_result = user_signup(user, password);
            if (auth_result == USER_EXISTS) {
                const char *msg = "SIGNUP: USER_EXISTS\n";
                send_all(client_socket, msg, strlen(msg));
                continue;
            }
        } else if (strcasecmp(command, "LOGIN") == 0) {
            auth_result = user_login(user, password);
            if (auth_result != SUCCESS) {
                const char *msg = "LOGIN: FAILED\n";
                send_all(client_socket, msg, strlen(msg));
                continue;
            }
        } else {
            const char *err = "ERROR: Unknown command. PLease SIGNUP or LOGIN\n";
            send_all(client_socket, err, strlen(err));
            continue;
        }

        // If we get here and auth_result == SUCCESS
        if (auth_result == SUCCESS) {
            snprintf(response, sizeof(response), "%s: SUCCESS\n", strcasecmp(command, "SIGNUP") == 0 ? "SIGNUP" : "LOGIN");
            send_all(client_socket, response, strlen(response));

            // send single welcome message here
            const char *welcome_msg = "AUTH_SUCCESS: Welcome!!! \nCommands:   UPLOAD,   DOWNLOAD,   DELETE,   LIST,    QUIT\n";
            send_all(client_socket, welcome_msg, strlen(welcome_msg));

            // store username safely
            strncpy(username, user, MAX_USERNAME_LEN - 1);
            username[MAX_USERNAME_LEN - 1] = '\0';
            return SUCCESS;
        }
    }

    return AUTH_FAILED;
}

int parse_client_command(int client_socket, const char *command, char *username, task_t *out_task) {
    char cmd[20];
    char filename[256];

    if (!command || sscanf(command, "%19s %255s", cmd, filename) < 1) return ERROR;

    operation_t op;
    if (strcasecmp(cmd, "UPLOAD") == 0) op = UPLOAD;
    else if (strcasecmp(cmd, "DOWNLOAD") == 0) op = DOWNLOAD;
    else if (strcasecmp(cmd, "DELETE") == 0) op = DELETE;
    else if (strcasecmp(cmd, "LIST") == 0) op = LIST;
    else return ERROR;

    // create_task should allocate and return a heap task; assume create_task returns task_t
    *out_task = create_task(client_socket, username, op, (op != LIST) ? filename : NULL);

    if (op == UPLOAD) {
        const char *msg = "READY: Send file data (single chunk)\n";
        send_all(client_socket, msg, strlen(msg));

        char upload_buffer[4096];
        ssize_t data_received = recv(client_socket, upload_buffer, sizeof(upload_buffer), 0);
        if (data_received > 0) {
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
    client_pool_t *pool = (client_pool_t*)arg;
    char username[MAX_USERNAME_LEN];

    while (!pool->shutdown) {
        int client_socket = dequeue_client(pool->client_queue);
        if (client_socket < 0) continue;

        printf("Client thread handling socket %d\n", client_socket);

        // authenticate before anything else
        if (handle_client_authentication(client_socket, username) != SUCCESS) {
            printf("Authentication failed or client disconnected: socket %d\n", client_socket);
            close(client_socket);
            continue;
        }

        printf("User '%s' authenticated on socket %d\n", username, client_socket);

        int session_active = 1;
        char command_buffer[MAX_COMMAND_LEN];

        while (session_active && !pool->shutdown) {
            ssize_t bytes_received = recv(client_socket, command_buffer, sizeof(command_buffer) - 1, 0);
            if (bytes_received <= 0) {
                printf("Client disconnected: socket %d\n", client_socket);
                break;
            }
            command_buffer[bytes_received] = '\0';

            if (strncasecmp(command_buffer, "QUIT", 4) == 0) {
                const char *bye_msg = "GOODBYE: Session ended\n";
                send_all(client_socket, bye_msg, strlen(bye_msg));
                session_active = 0;
                continue;
            }

            task_t *current_task = NULL;
            // assume create_task returns pointer or create into allocated memory; if your create_task returns value adjust accordingly
            task_t local_task;
            int parse_res = parse_client_command(client_socket, command_buffer, username, &local_task);
            if (parse_res != SUCCESS) {
                const char *error_msg = "ERROR: Invalid command. Use: UPLOAD, DOWNLOAD, DELETE, LIST, QUIT\n";
                send_all(client_socket, error_msg, strlen(error_msg));
                continue;
            }

            // enqueue_task should make a deep copy or take ownership. If it expects heap task, allocate and copy.
            if (enqueue_task(pool->task_queue, &local_task) != SUCCESS) {
                const char *error_msg = "ERROR: Server busy, try again later\n";
                send_all(client_socket, error_msg, strlen(error_msg));
                destroy_task(&local_task);
                continue;
            }

            const char *ack_msg = "ACK: Task queued for processing\n";
            send_all(client_socket, ack_msg, strlen(ack_msg));
            printf("Task queued for user %s: operation=%d, file=%s\n",
                   username, local_task.operation, local_task.filename ? local_task.filename : "(none)");
        }

        close(client_socket);
        printf("Client session ended for user '%s'\n", username);
    }

    printf("Client thread exiting\n");
    return NULL;
}

