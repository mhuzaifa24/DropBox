#ifndef USER_AUTH_H
#define USER_AUTH_H

#include "common.h"

#define USERS_DB_FILE "users.db"

typedef struct {
    char username[MAX_USERNAME_LEN];
    char password[MAX_PASSWORD_LEN];

    // file list (phase1 usage)
    char **files;
    int file_count;
    int file_capacity;

    // quota
    size_t quota_used;
    size_t quota_limit;

    // per-user lock
    pthread_mutex_t lock;
} user_t;

// Auth lifecycle
int auth_init(void);
void auth_cleanup(void);

// User operations
int user_exists(const char *username);
user_t* get_user(const char *username);
int user_signup(const char *username, const char *password);
int user_login(const char *username, const char *password);

// File/Quota operations on user
int user_add_file(user_t *user, const char *filename, size_t file_size);
int user_remove_file(user_t *user, const char *filename);
int user_has_file(user_t *user, const char *filename);
char** user_list_files(user_t *user, int *file_count);
int check_quota(user_t *user, size_t additional_size);
int update_quota(user_t *user, ssize_t size_change);

#endif // USER_AUTH_H

