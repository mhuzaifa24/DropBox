#ifndef UTILS_H
#define UTILS_H

#include "common.h"

// Memory management utilities
void* safe_malloc(size_t size);
void* safe_calloc(size_t num, size_t size);
void* safe_realloc(void *ptr, size_t size);
void safe_free(void *ptr);

// String utilities
char* safe_strdup(const char *str);
char* trim_whitespace(char *str);
int starts_with(const char *str, const char *prefix);

// Network utilities
int send_all(int socket, const char *buffer, size_t length);
int recv_all(int socket, char *buffer, size_t length);
int create_server_socket(int port);
int accept_client_connection(int server_socket);

// Logging utilities
void log_info(const char *format, ...);
void log_error(const char *format, ...);
void log_debug(const char *format, ...);

// Validation utilities
int is_valid_filename(const char *filename);
int is_valid_username(const char *username);
int is_valid_password(const char *password);

// File utilities
size_t get_file_size(const char *filename);
int file_exists(const char *path);

// Time utilities
void get_current_timestamp(char *buffer, size_t buffer_size);

#endif
