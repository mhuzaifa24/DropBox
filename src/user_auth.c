#include "../include/user_auth.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define INITIAL_USER_CAPACITY 10
#define INITIAL_FILE_CAPACITY 5

// Global users database
static user_t **users = NULL;
static int user_count = 0;
static int user_capacity = 0;
static pthread_mutex_t users_lock;

int auth_init(void) {
    // Initialize the mutex dynamically
    if (pthread_mutex_init(&users_lock, NULL) != 0) {
        fprintf(stderr, "Failed to initialize users mutex\n");
        return ERROR;
    }
    pthread_mutex_lock(&users_lock);
    
    users = malloc(sizeof(user_t *) * INITIAL_USER_CAPACITY);
    if (!users) {
        pthread_mutex_unlock(&users_lock);
        return ERROR;
    }
    
    user_capacity = INITIAL_USER_CAPACITY;
    user_count = 0;
    
    pthread_mutex_unlock(&users_lock);
    return SUCCESS;
}

void auth_cleanup(void) {
    pthread_mutex_lock(&users_lock);
    
    for (int i = 0; i < user_count; i++) {
        user_t *user = users[i];
        
        // Free file list
        for (int j = 0; j < user->file_count; j++) {
            free(user->files[j]);
        }
        free(user->files);
        
        // Destroy user lock and free user
        pthread_mutex_destroy(&user->lock);
        free(user);
    }
    
    free(users);
    users = NULL;
    user_count = 0;
    user_capacity = 0;
    
    pthread_mutex_unlock(&users_lock);
}

static int find_user_index(const char *username) {
    for (int i = 0; i < user_count; i++) {
        if (strcmp(users[i]->username, username) == 0) {
            return i;
        }
    }
    return -1;
}

int user_exists(const char *username) {
    pthread_mutex_lock(&users_lock);
    int exists = (find_user_index(username) != -1);
    pthread_mutex_unlock(&users_lock);
    return exists;
}

user_t* get_user(const char *username) {
    pthread_mutex_lock(&users_lock);
    
    int index = find_user_index(username);
    user_t *user = (index != -1) ? users[index] : NULL;
    
    pthread_mutex_unlock(&users_lock);
    return user;
}

int user_signup(const char *username, const char *password) {
    if (!username || !password || strlen(username) == 0 || strlen(password) == 0) {
        return ERROR;
    }
    
    pthread_mutex_lock(&users_lock);
    
    // Check if user already exists
    if (find_user_index(username) != -1) {
        pthread_mutex_unlock(&users_lock);
        return USER_EXISTS;
    }
    
    // Resize users array if needed
    if (user_count >= user_capacity) {
        int new_capacity = user_capacity * 2;
        user_t **new_users = realloc(users, sizeof(user_t *) * new_capacity);
        if (!new_users) {
            pthread_mutex_unlock(&users_lock);
            return ERROR;
        }
        users = new_users;
        user_capacity = new_capacity;
    }
    
    // Create new user
    user_t *new_user = malloc(sizeof(user_t));
    if (!new_user) {
        pthread_mutex_unlock(&users_lock);
        return ERROR;
    }
    
    // Initialize user fields
    strncpy(new_user->username, username, MAX_USERNAME_LEN - 1);
    strncpy(new_user->password, password, MAX_PASSWORD_LEN - 1);
    new_user->quota_used = 0;
    new_user->quota_limit = DEFAULT_QUOTA_LIMIT;
    new_user->file_count = 0;
    new_user->file_capacity = INITIAL_FILE_CAPACITY;
    
    // Initialize file list
    new_user->files = malloc(sizeof(char *) * new_user->file_capacity);
    if (!new_user->files) {
        free(new_user);
        pthread_mutex_unlock(&users_lock);
        return ERROR;
    }
    
    // Initialize user lock
    if (pthread_mutex_init(&new_user->lock, NULL) != 0) {
        free(new_user->files);
        free(new_user);
        pthread_mutex_unlock(&users_lock);
        return ERROR;
    }
    
    // Add user to database
    users[user_count++] = new_user;
    
    pthread_mutex_unlock(&users_lock);
    
    printf("User '%s' registered successfully\n", username);
    return SUCCESS;
}

int user_login(const char *username, const char *password) {
    if (!username || !password) {
        return AUTH_FAILED;
    }
    
    pthread_mutex_lock(&users_lock);
    
    int index = find_user_index(username);
    if (index == -1) {
        pthread_mutex_unlock(&users_lock);
        return AUTH_FAILED;
    }
    
    user_t *user = users[index];
    int auth_result = (strcmp(user->password, password) == 0) ? SUCCESS : AUTH_FAILED;
    
    pthread_mutex_unlock(&users_lock);
    
    if (auth_result == SUCCESS) {
        printf("User '%s' logged in successfully\n", username);
    }
    
    return auth_result;
}

int user_add_file(user_t *user, const char *filename, size_t file_size) {
    if (!user || !filename) {
        return ERROR;
    }
    
    pthread_mutex_lock(&user->lock);
    
    // Check if file already exists
    for (int i = 0; i < user->file_count; i++) {
        if (strcmp(user->files[i], filename) == 0) {
            pthread_mutex_unlock(&user->lock);
            return ERROR; // File already exists
        }
    }
    
    // Resize files array if needed
    if (user->file_count >= user->file_capacity) {
        int new_capacity = user->file_capacity * 2;
        char **new_files = realloc(user->files, sizeof(char *) * new_capacity);
        if (!new_files) {
            pthread_mutex_unlock(&user->lock);
            return ERROR;
        }
        user->files = new_files;
        user->file_capacity = new_capacity;
    }
    
    // Add new file
    user->files[user->file_count] = malloc(strlen(filename) + 1);
    if (!user->files[user->file_count]) {
        pthread_mutex_unlock(&user->lock);
        return ERROR;
    }
    
    strcpy(user->files[user->file_count], filename);
    user->file_count++;
    
    // Update quota
    user->quota_used += file_size;
    
    pthread_mutex_unlock(&user->lock);
    return SUCCESS;
}

int user_remove_file(user_t *user, const char *filename) {
    if (!user || !filename) {
        return ERROR;
    }
    
    pthread_mutex_lock(&user->lock);
    
    int found_index = -1;
    for (int i = 0; i < user->file_count; i++) {
        if (strcmp(user->files[i], filename) == 0) {
            found_index = i;
            break;
        }
    }
    
    if (found_index == -1) {
        pthread_mutex_unlock(&user->lock);
        return FILE_NOT_FOUND;
    }
    
    // Free the filename and remove from array
    free(user->files[found_index]);
    
    // Shift remaining files
    for (int i = found_index; i < user->file_count - 1; i++) {
        user->files[i] = user->files[i + 1];
    }
    user->file_count--;
    
    pthread_mutex_unlock(&user->lock);
    return SUCCESS;
}

int user_has_file(user_t *user, const char *filename) {
    if (!user || !filename) {
        return 0;
    }
    
    pthread_mutex_lock(&user->lock);
    
    int found = 0;
    for (int i = 0; i < user->file_count; i++) {
        if (strcmp(user->files[i], filename) == 0) {
            found = 1;
            break;
        }
    }
    
    pthread_mutex_unlock(&user->lock);
    return found;
}

// In the user_list_files function, add bounds checking:
char** user_list_files(user_t *user, int *file_count) {
    if (!user || !file_count) {
        return NULL;
    }
    
    pthread_mutex_lock(&user->lock);
    
    *file_count = user->file_count;
    
    // Safety check - if no files, return NULL
    if (user->file_count == 0) {
        pthread_mutex_unlock(&user->lock);
        return NULL;
    }
    
    // Create a copy of the file list
    char **file_list = malloc(sizeof(char *) * user->file_count);
    if (!file_list) {
        pthread_mutex_unlock(&user->lock);
        return NULL;
    }
    
    for (int i = 0; i < user->file_count; i++) {
        file_list[i] = malloc(strlen(user->files[i]) + 1);
        if (!file_list[i]) {
            // Cleanup on failure
            for (int j = 0; j < i; j++) {
                free(file_list[j]);
            }
            free(file_list);
            pthread_mutex_unlock(&user->lock);
            return NULL;
        }
        strcpy(file_list[i], user->files[i]);
    }
    
    pthread_mutex_unlock(&user->lock);
    return file_list;
}

int check_quota(user_t *user, size_t additional_size) {
    if (!user) {
        return ERROR;
    }
    
    pthread_mutex_lock(&user->lock);
    int has_quota = (user->quota_used + additional_size <= user->quota_limit);
    pthread_mutex_unlock(&user->lock);
    
    return has_quota ? SUCCESS : QUOTA_EXCEEDED;
}

int update_quota(user_t *user, size_t size_change) {
    if (!user) {
        return ERROR;
    }
    
    pthread_mutex_lock(&user->lock);
    
    // Check if quota would be exceeded
    if (size_change > 0 && (user->quota_used + size_change > user->quota_limit)) {
        pthread_mutex_unlock(&user->lock);
        return QUOTA_EXCEEDED;
    }
    
    user->quota_used += size_change;
    
    pthread_mutex_unlock(&user->lock);
    return SUCCESS;
}
