#include "../include/common.h"
#include "../include/client_queue.h"
#include "../include/task_queue.h"
#include "../include/client_pool.h"
#include "../include/worker_pool.h"
#include "../include/user_auth.h"
#include "../include/file_ops.h"
#include "../include/utils.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>

// ===== Color Codes =====
#define GREEN   "\033[1;32m"
#define YELLOW  "\033[1;33m"
#define RED     "\033[1;31m"
#define CYAN    "\033[1;36m"
#define GRAY    "\033[0;37m"
#define RESET   "\033[0m"

// ===== Global Variables =====
static client_queue_t *g_client_queue = NULL;
static task_queue_t   *g_task_queue   = NULL;
static client_pool_t  *g_client_pool  = NULL;
static worker_pool_t  *g_worker_pool  = NULL;
static volatile int g_shutdown = 0;

// ===== Utility: Timestamped Log =====
void log_with_time(const char *color, const char *icon, const char *level, const char *fmt, ...) {
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char time_str[10];
    strftime(time_str, sizeof(time_str), "%H:%M:%S", tm_info);

    printf("%s[%s]%s %s%s %s: ", GRAY, time_str, RESET, color, icon, level);
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    printf("%s\n", RESET);
}

// ===== Signal Handler =====
_Atomic int shutdown_requested = 0;

void handle_signal(int sig) {
    const char msg[] = "Signal received. Shutting down...\n";
    write(STDOUT_FILENO, msg, sizeof(msg)-1);  // async-signal-safe
    shutdown_requested = 1;                     // notify main thread
}

// ===== Initialization =====
int initialize_server(int client_threads, int worker_threads, int queue_capacity) {
    log_with_time(CYAN, "🚀", "INIT", "Initializing Dropbox Server...");

    if (file_ops_init() != FILE_OP_SUCCESS) {
        log_with_time(RED, "❌", "ERROR", "File operations init failed.");
        return -1;
    }

    if (auth_init() != SUCCESS) {
        log_with_time(RED, "❌", "ERROR", "Authentication system init failed.");
        return -1;
    }

    g_client_queue = create_client_queue(queue_capacity);
    g_task_queue   = create_task_queue(queue_capacity * 2);

    if (!g_client_queue || !g_task_queue) {
        log_with_time(RED, "❌", "ERROR", "Queue initialization failed.");
        return -1;
    }

    g_client_pool = create_client_pool(client_threads, g_client_queue, g_task_queue);
    g_worker_pool = create_worker_pool(worker_threads, g_task_queue);

    if (!g_client_pool || !g_worker_pool) {
        log_with_time(RED, "❌", "ERROR", "Thread pool creation failed.");
        return -1;
    }

    log_with_time(GREEN, "✅", "READY", "Server initialized successfully.");
    log_with_time(GREEN, "🧩", "CONFIG", "Clients: %d | Workers: %d | Queue Cap: %d",
                  client_threads, worker_threads, queue_capacity);
    return 0;
}

// ===== Cleanup =====
void cleanup_server() {
    log_with_time(YELLOW, "🧹", "CLEANUP", "Cleaning up server resources...");

    if (g_client_pool) destroy_client_pool(g_client_pool);
    if (g_worker_pool) destroy_worker_pool(g_worker_pool);
    if (g_task_queue) destroy_task_queue(g_task_queue);
    if (g_client_queue) destroy_client_queue(g_client_queue);

    auth_cleanup();
    file_ops_cleanup();

    log_with_time(GREEN, "🏁", "DONE", "Server cleanup completed.");
}

// ===== Main Loop =====
void run_server(int port) {
    int server_socket = create_server_socket(port);
    if (server_socket < 0) {
        log_with_time(RED, "❌", "ERROR", "Failed to create server socket.");
        return;
    }

    printf("\n%s🌍 Server running on port %d%s\n", GREEN, port, RESET);
    printf("%s-------------------------------------------%s\n", GRAY, RESET);

    while (!g_shutdown) {
        int client_socket = accept_client_connection(server_socket);
        if (client_socket < 0) {
            if (!g_shutdown)
                log_with_time(RED, "⚠️", "ERROR", "Error accepting client connection.");
            continue;
        }

        if (enqueue_client(g_client_queue, client_socket) != SUCCESS) {
            log_with_time(RED, "🚫", "QUEUE", "Client queue full — rejecting socket %d.", client_socket);
            close(client_socket);
        } else {
            log_with_time(GREEN, "🟢", "CLIENT", "New connection accepted (socket %d).", client_socket);
        }
    }

    close(server_socket);
    log_with_time(YELLOW, "🔒", "SHUTDOWN", "Server socket closed.");
}

// ===== Debug: Server Status =====
void print_server_status() {
    printf("\n%s========= SERVER STATUS =========%s\n", CYAN, RESET);
    printf("Client Queue: %d/%d\n", g_client_queue ? g_client_queue->size : 0,
           g_client_queue ? g_client_queue->capacity : 0);
    printf("Task Queue:   %d/%d\n", g_task_queue ? g_task_queue->size : 0,
           g_task_queue ? g_task_queue->capacity : 0);
    printf("Client Threads: %d\n", g_client_pool ? g_client_pool->thread_count : 0);
    printf("Worker Threads: %d\n", g_worker_pool ? g_worker_pool->thread_count : 0);
    printf("%s==================================%s\n", CYAN, RESET);
}

// ===== Entry Point =====
int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: %s <port>\nExample: %s 8080\n", argv[0], argv[0]);
        return 1;
    }

    int port = atoi(argv[1]);
    if (port <= 0 || port > 65535) {
        printf("%sInvalid port number.%s\n", RED, RESET);
        return 1;
    }

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    printf("%s==============================\n", CYAN);
    printf("  🗂️  Dropbox Server Starting  \n");
    printf("==============================%s\n", RESET);
    printf("Port: %d\n", port);

    if (initialize_server(4, 6, 20) != 0) {
        log_with_time(RED, "💥", "FATAL", "Initialization failed. Exiting.");
        return 1;
    }

    run_server(port);
    cleanup_server();

    return 0;
}

