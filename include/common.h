#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define MAX_USERNAME_LEN 50
#define MAX_FILENAME_LEN 256
#define MAX_PASSWORD_LEN 50
#define DEFAULT_QUOTA_LIMIT (100 * 1024 * 1024) // 100MB

// Network and buffer constants
#define MAX_COMMAND_LEN 1024
#define MAX_RESPONSE_LEN 2048
#define MAX_BUFFER_SIZE 4096

// Debug flag - comment out to disable debug logging
#define DEBUG 1

// Error codes
#define SUCCESS 0
#define ERROR -1
#define USER_EXISTS -2
#define AUTH_FAILED -3
#define QUOTA_EXCEEDED -4
#define FILE_NOT_FOUND -5


#endif
