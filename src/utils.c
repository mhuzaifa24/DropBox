#include "../include/utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/stat.h>
#include <time.h>
#include <errno.h>
#include <ctype.h>

// Memory management utilities - NON-RECURSIVE VERSION
void* safe_malloc(size_t size) {
    if (size == 0) {
        fprintf(stderr, "ERROR: Attempted to allocate 0 bytes\n");
        return NULL;
    }
    
    void *ptr = malloc(size);
    if (!ptr) {
        fprintf(stderr, "ERROR: Memory allocation failed for %zu bytes\n", size);
        return NULL;
    }
    
    return ptr;
}

void* safe_calloc(size_t num, size_t size) {
    if (num == 0 || size == 0) {
        fprintf(stderr, "ERROR: Attempted to calloc 0 elements or 0 size\n");
        return NULL;
    }
    
    void *ptr = calloc(num, size);
    if (!ptr) {
        fprintf(stderr, "ERROR: calloc failed for %zu elements of %zu bytes\n", num, size);
        return NULL;
    }
    return ptr;
}

void* safe_realloc(void *ptr, size_t size) {
    if (size == 0) {
        fprintf(stderr, "ERROR: Attempted to realloc to 0 size\n");
        safe_free(ptr);
        return NULL;
    }
    
    void *new_ptr = realloc(ptr, size);
    if (!new_ptr) {
        fprintf(stderr, "ERROR: realloc failed for %zu bytes\n", size);
        return NULL;
    }
    return new_ptr;
}

void safe_free(void *ptr) {
    if (ptr) {
        free(ptr);
    }
}

// String utilities
char* safe_strdup(const char *str) {
    if (!str) return NULL;
    
    size_t len = strlen(str) + 1;
    char *new_str = malloc(len);
    if (!new_str) {
        fprintf(stderr, "ERROR: String duplication failed\n");
        return NULL;
    }
    
    memcpy(new_str, str, len);
    return new_str;
}

char* trim_whitespace(char *str) {
    if (!str) return NULL;
    
    char *end;
    
    // Trim leading space
    while (*str == ' ' || *str == '\t' || *str == '\n' || *str == '\r') {
        str++;
    }
    
    if (*str == 0) return str; // All spaces
    
    // Trim trailing space
    end = str + strlen(str) - 1;
    while (end > str && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')) {
        end--;
    }
    
    // Write new null terminator
    *(end + 1) = '\0';
    
    return str;
}

int starts_with(const char *str, const char *prefix) {
    if (!str || !prefix) return 0;
    return strncmp(str, prefix, strlen(prefix)) == 0;
}

// ... (keep the rest of the utils.c functions the same, but REMOVE the custom_strdup function)

// Network utilities
int send_all(int socket, const char *buffer, size_t length) {
    size_t total_sent = 0;
    
    while (total_sent < length) {
        ssize_t sent = send(socket, buffer + total_sent, length - total_sent, 0);
        if (sent < 0) {
            if (errno == EINTR) continue; // Interrupted, try again
            log_error("Failed to send data to socket %d: %s", socket, strerror(errno));
            return -1;
        }
        total_sent += sent;
    }
    
    return 0;
}

int recv_all(int socket, char *buffer, size_t length) {
    size_t total_received = 0;
    
    while (total_received < length) {
        ssize_t received = recv(socket, buffer + total_received, length - total_received, 0);
        if (received < 0) {
            if (errno == EINTR) continue; // Interrupted, try again
            log_error("Failed to receive data from socket %d: %s", socket, strerror(errno));
            return -1;
        } else if (received == 0) {
            log_debug("Connection closed by peer on socket %d", socket);
            return -1; // Connection closed
        }
        total_received += received;
    }
    
    return 0;
}

int create_server_socket(int port) {
    int server_socket;
    struct sockaddr_in server_addr;
    
    // Create socket
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        log_error("Failed to create server socket: %s", strerror(errno));
        return -1;
    }
    
    // Set socket options to reuse address
    int opt = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        log_error("Failed to set socket options: %s", strerror(errno));
        close(server_socket);
        return -1;
    }
    
    // Configure server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);
    
    // Bind socket
    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        log_error("Failed to bind server socket to port %d: %s", port, strerror(errno));
        close(server_socket);
        return -1;
    }
    
    // Listen for connections
    if (listen(server_socket, 10) < 0) { // Backlog of 10 connections
        log_error("Failed to listen on server socket: %s", strerror(errno));
        close(server_socket);
        return -1;
    }
    
    log_info("Server socket created and listening on port %d", port);
    return server_socket;
}

int accept_client_connection(int server_socket) {
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    
    int client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &client_len);
    if (client_socket < 0) {
        log_error("Failed to accept client connection: %s", strerror(errno));
        return -1;
    }
    
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
    log_info("New client connection from %s:%d", client_ip, ntohs(client_addr.sin_port));
    
    return client_socket;
}

// Logging utilities
void log_info(const char *format, ...) {
    char timestamp[64];
    get_current_timestamp(timestamp, sizeof(timestamp));
    
    printf("[%s] INFO: ", timestamp);
    
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    
    printf("\n");
}

void log_error(const char *format, ...) {
    char timestamp[64];
    get_current_timestamp(timestamp, sizeof(timestamp));
    
    fprintf(stderr, "[%s] ERROR: ", timestamp);
    
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    
    fprintf(stderr, "\n");
}

void log_debug(const char *format, ...) {
#ifdef DEBUG
    char timestamp[64];
    get_current_timestamp(timestamp, sizeof(timestamp));
    
    printf("[%s] DEBUG: ", timestamp);
    
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    
    printf("\n");
#else
    // Suppress unused parameter warning when DEBUG is not defined
    (void)format;
#endif
}

// Validation utilities
int is_valid_filename(const char *filename) {
    if (!filename || strlen(filename) == 0 || strlen(filename) > MAX_FILENAME_LEN - 1) {
        return 0;
    }
    
    // Check for invalid characters
    const char *invalid_chars = "/\\?%*:|\"<>";
    for (const char *c = invalid_chars; *c; c++) {
        if (strchr(filename, *c)) {
            return 0;
        }
    }
    
    // Check for reserved names (Windows, but good practice)
    const char *reserved_names[] = {"CON", "PRN", "AUX", "NUL", 
                                   "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
                                   "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9", NULL};
    
    for (int i = 0; reserved_names[i]; i++) {
        if (strcasecmp(filename, reserved_names[i]) == 0) {
            return 0;
        }
    }
    
    return 1;
}

int is_valid_username(const char *username) {
    if (!username || strlen(username) == 0 || strlen(username) > MAX_USERNAME_LEN - 1) {
        return 0;
    }
    
    // Check for invalid characters (only alphanumeric and underscore)
    for (const char *c = username; *c; c++) {
        if (!isalnum((unsigned char)*c) && *c != '_') {
            return 0;
        }
    }
    
    return 1;
}

int is_valid_password(const char *password) {
    if (!password || strlen(password) < 4 || strlen(password) > MAX_PASSWORD_LEN - 1) {
        return 0;
    }
    
    return 1;
}

// File utilities
size_t get_file_size(const char *filename) {
    struct stat st;
    if (stat(filename, &st) == 0) {
        return st.st_size;
    }
    return 0;
}

int file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

// Time utilities
void get_current_timestamp(char *buffer, size_t buffer_size) {
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    
    strftime(buffer, buffer_size, "%Y-%m-%d %H:%M:%S", tm_info);
}
