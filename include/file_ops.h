#ifndef FILE_OPS_H
#define FILE_OPS_H

#include "common.h"
#include "user_auth.h"

// File operation result codes
#define FILE_OP_SUCCESS 0
#define FILE_OP_ERROR -1
#define FILE_OP_NOT_FOUND -2
#define FILE_OP_QUOTA_EXCEEDED -3
#define FILE_OP_ALREADY_EXISTS -4

// File storage structure
typedef struct {
    char *data;
    size_t size;
    char filename[MAX_FILENAME_LEN];
    time_t last_modified;
} file_data_t;

// File operation functions
int file_ops_init(void);
void file_ops_cleanup(void);

// Core file operations
int handle_upload(user_t *user, const char *filename, const char *data, size_t size);
int handle_download(user_t *user, const char *filename, char **data, size_t *size);
int handle_delete(user_t *user, const char *filename);
char** handle_list(user_t *user, int *file_count);

// Utility functions
int save_file_to_disk(const char *username, const char *filename, const char *data, size_t size);
int load_file_from_disk(const char *username, const char *filename, char **data, size_t *size);
int delete_file_from_disk(const char *username, const char *filename);
int ensure_user_directory(const char *username);

#endif
