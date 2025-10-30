#ifndef USER_AUTH_H
#define USER_AUTH_H

#include "common.h"

typedef struct {
    char username[MAX_USERNAME_LEN];
    char password[MAX_PASSWORD_LEN];  // In Phase 1, simple storage
    size_t quota_used;
    size_t quota_limit;
    char **files;           // Array of filenames
    int file_count;
    int file_capacity;
    pthread_mutex_t lock;   // Per-user lock for thread safety
} user_t;

// User management functions
int auth_init(void);
void auth_cleanup(void);
int user_signup(const char *username, const char *password);
int user_login(const char *username, const char *password);
user_t* get_user(const char *username);
int user_exists(const char *username);

// File management for users
int user_add_file(user_t *user, const char *filename, size_t file_size);
int user_remove_file(user_t *user, const char *filename);
int user_has_file(user_t *user, const char *filename);
char** user_list_files(user_t *user, int *file_count);

// Quota management
int check_quota(user_t *user, size_t additional_size);
int update_quota(user_t *user, size_t size_change);

#endif
