#include "../include/common.h"
#include "../include/client_queue.h"
#include "../include/task_queue.h"
#include "../include/client_pool.h"
#include "../include/worker_pool.h"
#include "../include/user_auth.h"
#include "../include/file_ops.h"
#include "../include/utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>

// Global variables for cleanup
static client_queue_t *g_client_queue = NULL;
static task_queue_t *g_task_queue = NULL;
static client_pool_t *g_client_pool = NULL;
static worker_pool_t *g_worker_pool = NULL;
static volatile int g_shutdown = 0;

// Signal handler for graceful shutdown
void handle_signal(int sig) {
    printf("\nReceived signal %d. Shutting down gracefully...\n", sig);
    g_shutdown = 1;
    
    // Shutdown thread pools
    if (g_client_pool) shutdown_client_pool(g_client_pool);
    if (g_worker_pool) shutdown_worker_pool(g_worker_pool);
}

// Initialize all server components
int initialize_server(int client_threads, int worker_threads, int queue_capacity) {
    printf("Initializing Dropbox Server...\n");
    
    // Initialize subsystems
    if (file_ops_init() != FILE_OP_SUCCESS) {
        log_error("Failed to initialize file operations");
        return -1;
    }
    
    if (auth_init() != SUCCESS) {
        log_error("Failed to initialize authentication system");
        return -1;
    }
    
    // Create queues
    g_client_queue = create_client_queue(queue_capacity);
    g_task_queue = create_task_queue(queue_capacity * 2);
    
    if (!g_client_queue || !g_task_queue) {
        log_error("Failed to create queues");
        return -1;
    }
    
    // Create thread pools
    g_client_pool = create_client_pool(client_threads, g_client_queue, g_task_queue);
    g_worker_pool = create_worker_pool(worker_threads, g_task_queue);
    
    if (!g_client_pool || !g_worker_pool) {
        log_error("Failed to create thread pools");
        return -1;
    }
    
    log_info("Server initialized successfully");
    log_info("Client threads: %d, Worker threads: %d, Queue capacity: %d", 
             client_threads, worker_threads, queue_capacity);
    
    return 0;
}

// Cleanup all server components
void cleanup_server() {
    log_info("Cleaning up server resources...");
    
    // Destroy thread pools
    if (g_client_pool) destroy_client_pool(g_client_pool);
    if (g_worker_pool) destroy_worker_pool(g_worker_pool);
    
    // Destroy queues
    if (g_task_queue) destroy_task_queue(g_task_queue);
    if (g_client_queue) destroy_client_queue(g_client_queue);
    
    // Cleanup subsystems
    auth_cleanup();
    file_ops_cleanup();
    
    log_info("Server cleanup completed");
}

// Main server loop
void run_server(int port) {
    int server_socket = create_server_socket(port);
    if (server_socket < 0) {
        log_error("Failed to create server socket");
        return;
    }
    
    log_info("Server listening on port %d", port);
    log_info("Ready to accept client connections...");
    
    while (!g_shutdown) {
        // Accept new client connection
        int client_socket = accept_client_connection(server_socket);
        if (client_socket < 0) {
            if (!g_shutdown) {
                log_error("Failed to accept client connection");
            }
            continue;
        }
        
        // Add client to queue (will be handled by client pool)
        if (enqueue_client(g_client_queue, client_socket) != SUCCESS) {
            log_error("Client queue is full, rejecting connection");
            close(client_socket);
        } else {
            log_info("Client connection queued (socket %d)", client_socket);
        }
    }
    
    close(server_socket);
    log_info("Server socket closed");
}

// Print server status
void print_server_status() {
    printf("\n=== Dropbox Server Status ===\n");
    printf("Client Queue: %d/%d\n", g_client_queue ? g_client_queue->size : 0, 
           g_client_queue ? g_client_queue->capacity : 0);
    printf("Task Queue: %d/%d\n", g_task_queue ? g_task_queue->size : 0,
           g_task_queue ? g_task_queue->capacity : 0);
    printf("Client Threads: %d\n", g_client_pool ? g_client_pool->thread_count : 0);
    printf("Worker Threads: %d\n", g_worker_pool ? g_worker_pool->thread_count : 0);
    printf("=============================\n");
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: %s <port>\n", argv[0]);
        printf("Example: %s 8080\n", argv[0]);
        return 1;
    }
    
    int port = atoi(argv[1]);
    if (port <= 0 || port > 65535) {
        printf("Error: Port must be between 1 and 65535\n");
        return 1;
    }
    
    // Setup signal handlers for graceful shutdown
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    
    printf("=== Dropbox Server Starting ===\n");
    printf("Port: %d\n", port);
    
    // Initialize server components
    if (initialize_server(4, 6, 20) != 0) {
        log_error("Server initialization failed");
        return 1;
    }
    
    // Run the server
    run_server(port);
    
    // Cleanup
    cleanup_server();
    
    printf("=== Dropbox Server Stopped ===\n");
    return 0;
}
