#include "../include/user_auth.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#define INITIAL_USER_CAPACITY 10
#define INITIAL_FILE_CAPACITY 5

static user_t **users = NULL;
static int user_count = 0;
static int user_capacity = 0;
static pthread_mutex_t users_lock = PTHREAD_MUTEX_INITIALIZER;

// internal helper to allocate and initialize a user struct
static user_t* allocate_user(const char *username, const char *password) {
    user_t *u = calloc(1, sizeof(user_t));
    if (!u) return NULL;

    // safe copy with explicit null termination
    strncpy(u->username, username, MAX_USERNAME_LEN - 1);
    u->username[MAX_USERNAME_LEN - 1] = '\0';
    strncpy(u->password, password, MAX_PASSWORD_LEN - 1);
    u->password[MAX_PASSWORD_LEN - 1] = '\0';

    u->quota_used = 0;
    u->quota_limit = DEFAULT_QUOTA_LIMIT;
    u->file_count = 0;
    u->file_capacity = INITIAL_FILE_CAPACITY;
    u->files = malloc(sizeof(char *) * u->file_capacity);
    if (!u->files) { free(u); return NULL; }

    if (pthread_mutex_init(&u->lock, NULL) != 0) {
        free(u->files);
        free(u);
        return NULL;
    }
    return u;
}

int auth_init(void) {
    pthread_mutex_lock(&users_lock);

    users = malloc(sizeof(user_t *) * INITIAL_USER_CAPACITY);
    if (!users) {
        pthread_mutex_unlock(&users_lock);
        return ERROR;
    }
    user_capacity = INITIAL_USER_CAPACITY;
    user_count = 0;

    // try to load persistent users (users.db) if present
    FILE *f = fopen(USERS_DB_FILE, "r");
    if (f) {
        char line[512];
        while (fgets(line, sizeof(line), f)) {
            // expected format: username:password\n
            char *nl = strchr(line, '\n');
            if (nl) *nl = '\0';
            char *sep = strchr(line, ':');
            if (!sep) continue;
            *sep = '\0';
            char *uname = line;
            char *pwd = sep + 1;
            // skip empty
            if (strlen(uname) == 0 || strlen(pwd) == 0) continue;

            // resize if needed
            if (user_count >= user_capacity) {
                int new_capacity = user_capacity * 2;
                user_t **new_users = realloc(users, sizeof(user_t *) * new_capacity);
                if (!new_users) break; // stop loading further
                users = new_users;
                user_capacity = new_capacity;
            }
            user_t *u = allocate_user(uname, pwd);
            if (!u) continue;
            users[user_count++] = u;
        }
        fclose(f);
    }

    pthread_mutex_unlock(&users_lock);
    return SUCCESS;
}

void auth_cleanup(void) {
    pthread_mutex_lock(&users_lock);

    for (int i = 0; i < user_count; i++) {
        user_t *user = users[i];
        if (!user) continue;
        // free file list
        for (int j = 0; j < user->file_count; j++) {
            free(user->files[j]);
        }
        free(user->files);
        pthread_mutex_destroy(&user->lock);
        free(user);
    }
    free(users);
    users = NULL;
    user_count = 0;
    user_capacity = 0;

    pthread_mutex_unlock(&users_lock);
    // destroy global lock if desired (not necessary for short-lived program)
    pthread_mutex_destroy(&users_lock);
}

static int find_user_index_locked(const char *username) {
    // caller must hold users_lock
    for (int i = 0; i < user_count; i++) {
        if (strcmp(users[i]->username, username) == 0) {
            return i;
        }
    }
    return -1;
}

int user_exists(const char *username) {
    if (!username) return 0;
    pthread_mutex_lock(&users_lock);
    int exists = (find_user_index_locked(username) != -1);
    pthread_mutex_unlock(&users_lock);
    return exists;
}

user_t* get_user(const char *username) {
    if (!username) return NULL;
    pthread_mutex_lock(&users_lock);
    int idx = find_user_index_locked(username);
    user_t *res = (idx != -1) ? users[idx] : NULL;
    pthread_mutex_unlock(&users_lock);
    return res;
}

static int persist_user_to_disk(const char *username, const char *password) {
    FILE *f = fopen(USERS_DB_FILE, "a");
    if (!f) return ERROR;
    // write as username:password\n
    if (fprintf(f, "%s:%s\n", username, password) < 0) {
        fclose(f);
        return ERROR;
    }
    fclose(f);
    return SUCCESS;
}

int user_signup(const char *username, const char *password) {
    if (!username || !password || strlen(username) == 0 || strlen(password) == 0) {
        return ERROR;
    }

    pthread_mutex_lock(&users_lock);

    if (find_user_index_locked(username) != -1) {
        pthread_mutex_unlock(&users_lock);
        return USER_EXISTS;
    }

    // resize if needed
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

    user_t *new_user = allocate_user(username, password);
    if (!new_user) {
        pthread_mutex_unlock(&users_lock);
        return ERROR;
    }

    users[user_count++] = new_user;
    pthread_mutex_unlock(&users_lock);

    // persist to disk (best-effort)
    if (persist_user_to_disk(username, password) != SUCCESS) {
        // we won't rollback in-memory user on disk failure to keep things simpler,
        // but in production you'd need stronger guarantees
        fprintf(stderr, "Warning: failed to persist user '%s' to disk: %s\n", username, strerror(errno));
    }

    fprintf(stdout, "User '%s' registered successfully\n", username);
    return SUCCESS;
}

int user_login(const char *username, const char *password) {
    if (!username || !password) return AUTH_FAILED;

    pthread_mutex_lock(&users_lock);
    int idx = find_user_index_locked(username);
    if (idx == -1) {
        pthread_mutex_unlock(&users_lock);
        return AUTH_FAILED;
    }

    user_t *u = users[idx];
    // read password under users_lock is OK since password is immutable here
    int res = (strcmp(u->password, password) == 0) ? SUCCESS : AUTH_FAILED;
    pthread_mutex_unlock(&users_lock);

    if (res == SUCCESS) {
        fprintf(stdout, "User '%s' logged in successfully\n", username);
    }
    return res;
}

int user_add_file(user_t *user, const char *filename, size_t file_size) {
    if (!user || !filename) return ERROR;
    if (file_size > 0) {
        // check quota first
        if (check_quota(user, file_size) == QUOTA_EXCEEDED) return QUOTA_EXCEEDED;
    }

    pthread_mutex_lock(&user->lock);

    // ensure not duplicate
    for (int i = 0; i < user->file_count; i++) {
        if (strcmp(user->files[i], filename) == 0) {
            pthread_mutex_unlock(&user->lock);
            return ERROR; // file exists
        }
    }

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

    user->files[user->file_count] = malloc(strlen(filename) + 1);
    if (!user->files[user->file_count]) {
        pthread_mutex_unlock(&user->lock);
        return ERROR;
    }
    strcpy(user->files[user->file_count], filename);
    user->file_count++;

    if (file_size > 0) user->quota_used += file_size;

    pthread_mutex_unlock(&user->lock);
    return SUCCESS;
}

int user_remove_file(user_t *user, const char *filename) {
    if (!user || !filename) return ERROR;
    pthread_mutex_lock(&user->lock);

    int found = -1;
    for (int i = 0; i < user->file_count; i++) {
        if (strcmp(user->files[i], filename) == 0) {
            found = i;
            break;
        }
    }
    if (found == -1) {
        pthread_mutex_unlock(&user->lock);
        return FILE_NOT_FOUND;
    }

    free(user->files[found]);
    for (int i = found; i < user->file_count - 1; i++) {
        user->files[i] = user->files[i + 1];
    }
    user->file_count--;
    pthread_mutex_unlock(&user->lock);
    return SUCCESS;
}

int user_has_file(user_t *user, const char *filename) {
    if (!user || !filename) return 0;
    pthread_mutex_lock(&user->lock);
    int found = 0;
    for (int i = 0; i < user->file_count; i++) {
        if (strcmp(user->files[i], filename) == 0) { found = 1; break; }
    }
    pthread_mutex_unlock(&user->lock);
    return found;
}

char** user_list_files(user_t *user, int *out_file_count) {
    if (!user || !out_file_count) return NULL;
    pthread_mutex_lock(&user->lock);
    *out_file_count = user->file_count;
    if (user->file_count == 0) {
        pthread_mutex_unlock(&user->lock);
        return NULL;
    }
    char **list = malloc(sizeof(char*) * user->file_count);
    if (!list) { pthread_mutex_unlock(&user->lock); return NULL; }
    for (int i = 0; i < user->file_count; i++) {
        list[i] = strdup(user->files[i]);
        if (!list[i]) {
            for (int j = 0; j < i; j++) free(list[j]);
            free(list);
            pthread_mutex_unlock(&user->lock);
            return NULL;
        }
    }
    pthread_mutex_unlock(&user->lock);
    return list;
}

int check_quota(user_t *user, size_t additional_size) {
    if (!user) return ERROR;
    pthread_mutex_lock(&user->lock);
    int ok = (user->quota_used + additional_size <= user->quota_limit);
    pthread_mutex_unlock(&user->lock);
    return ok ? SUCCESS : QUOTA_EXCEEDED;
}

int update_quota(user_t *user, ssize_t size_change) {
    if (!user) return ERROR;
    pthread_mutex_lock(&user->lock);
    if (size_change > 0 && (user->quota_used + size_change > user->quota_limit)) {
        pthread_mutex_unlock(&user->lock);
        return QUOTA_EXCEEDED;
    }
    user->quota_used += size_change;
    pthread_mutex_unlock(&user->lock);
    return SUCCESS;
}

