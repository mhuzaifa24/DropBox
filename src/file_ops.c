#include "../include/file_ops.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>  // Add this for error handling

#define STORAGE_BASE_DIR "server_storage"
#define MAX_FILE_PATH 512

int file_ops_init(void) {
    // Create base storage directory if it doesn't exist
    struct stat st = {0};
    if (stat(STORAGE_BASE_DIR, &st) == -1) {
        if (mkdir(STORAGE_BASE_DIR, 0755) != 0) {
            perror("Failed to create storage directory");
            return FILE_OP_ERROR;
        }
        printf("Created storage directory: %s\n", STORAGE_BASE_DIR);
    }
    return FILE_OP_SUCCESS;
}

void file_ops_cleanup(void) {
    // Cleanup any temporary resources if needed
    printf("File operations system cleaned up\n");
}

int ensure_user_directory(const char *username) {
    if (!username) return FILE_OP_ERROR;
    
    char user_dir[MAX_FILE_PATH];
    snprintf(user_dir, sizeof(user_dir), "%s/%s", STORAGE_BASE_DIR, username);
    
    struct stat st = {0};
    if (stat(user_dir, &st) == -1) {
        if (mkdir(user_dir, 0755) != 0) {
            perror("Failed to create user directory");
            return FILE_OP_ERROR;
        }
        printf("Created user directory: %s\n", user_dir);
    }
    return FILE_OP_SUCCESS;
}

int save_file_to_disk(const char *username, const char *filename, const char *data, size_t size) {
    if (!username || !filename || !data) {
        return FILE_OP_ERROR;
    }
    
    // Ensure user directory exists
    if (ensure_user_directory(username) != FILE_OP_SUCCESS) {
        return FILE_OP_ERROR;
    }
    
    // Create full file path
    char file_path[MAX_FILE_PATH];
    snprintf(file_path, sizeof(file_path), "%s/%s/%s", STORAGE_BASE_DIR, username, filename);
    
    // Save file to disk
    FILE *file = fopen(file_path, "wb");
    if (!file) {
        perror("Failed to open file for writing");
        return FILE_OP_ERROR;
    }
    
    size_t bytes_written = fwrite(data, 1, size, file);
    fclose(file);
    
    if (bytes_written != size) {
        fprintf(stderr, "Failed to write complete file. Expected: %zu, Written: %zu\n", size, bytes_written);
        return FILE_OP_ERROR;
    }
    
    printf("Saved file: %s (size: %zu bytes)\n", file_path, size);
    return FILE_OP_SUCCESS;
}

int load_file_from_disk(const char *username, const char *filename, char **data, size_t *size) {
    if (!username || !filename || !data || !size) {
        return FILE_OP_ERROR;
    }
    
    // Create full file path
    char file_path[MAX_FILE_PATH];
    snprintf(file_path, sizeof(file_path), "%s/%s/%s", STORAGE_BASE_DIR, username, filename);
    
    // Check if file exists
    struct stat st;
    if (stat(file_path, &st) != 0) {
        return FILE_OP_NOT_FOUND;
    }
    
    // Open file for reading
    FILE *file = fopen(file_path, "rb");
    if (!file) {
        perror("Failed to open file for reading");
        return FILE_OP_ERROR;
    }
    
    // Allocate memory for file data
    *data = malloc(st.st_size);
    if (!*data) {
        fclose(file);
        return FILE_OP_ERROR;
    }
    
    // Read file content
    size_t bytes_read = fread(*data, 1, st.st_size, file);
    fclose(file);
    
    if (bytes_read != (size_t)st.st_size) {
        free(*data);
        *data = NULL;
        return FILE_OP_ERROR;
    }
    
    *size = bytes_read;
    printf("Loaded file: %s (size: %zu bytes)\n", file_path, bytes_read);
    return FILE_OP_SUCCESS;
}

int delete_file_from_disk(const char *username, const char *filename) {
    if (!username || !filename) {
        return FILE_OP_ERROR;
    }

    char file_path[MAX_FILE_PATH];
    snprintf(file_path, sizeof(file_path), "%s/%s/%s", STORAGE_BASE_DIR, username, filename);

    // Check if file exists
    struct stat st;
    if (stat(file_path, &st) != 0) {
        perror("File not found for deletion");
        return FILE_OP_NOT_FOUND;
    }

    if (remove(file_path) != 0) {
        perror("Failed to delete file from disk");
        return FILE_OP_ERROR;
    }

    printf("File deleted from disk: %s\n", file_path);
    return FILE_OP_SUCCESS;
}




int handle_delete(user_t *user, const char *filename) {
    if (!user || !filename) {
        return FILE_OP_ERROR;
    }

    printf("Processing DELETE (disk mode): user=%s, file=%s\n", user->username, filename);

    // Build file path
    char file_path[MAX_FILE_PATH];
    snprintf(file_path, sizeof(file_path), "%s/%s/%s", STORAGE_BASE_DIR, user->username, filename);

    // Check if file exists
    struct stat st;
    if (stat(file_path, &st) != 0) {
        perror("File not found for deletion");
        return FILE_OP_NOT_FOUND;
    }

    size_t file_size = st.st_size;

    // Attempt deletion
    if (remove(file_path) != 0) {
        perror("Failed to delete file");
        return FILE_OP_ERROR;
    }

    printf("Deleted file from disk: %s\n", file_path);

    // Optional: update quota if implemented
    if (user->quota_used >= file_size) {
        update_quota(user, -((ssize_t)file_size));
    }

    // Optional: remove from in-memory list (won’t hurt if absent)
    user_remove_file(user, filename);

    return FILE_OP_SUCCESS;
}


int handle_upload(user_t *user, const char *filename, const char *data, size_t size) {
    if (!user || !filename || !data || size == 0) {
        return FILE_OP_ERROR;
    }
    
    printf("Processing UPLOAD: user=%s, file=%s, size=%zu\n", user->username, filename, size);
    
    // Check quota
    if (check_quota(user, size) != SUCCESS) {
        printf("Quota exceeded for user %s. Required: %zu, Available: %zu\n", 
               user->username, size, user->quota_limit - user->quota_used);
        return FILE_OP_QUOTA_EXCEEDED;
    }
    
    // Check if file already exists
    if (user_has_file(user, filename)) {
        printf("File already exists: %s\n", filename);
        return FILE_OP_ALREADY_EXISTS;
    }
    
    // Save file to disk
    if (save_file_to_disk(user->username, filename, data, size) != FILE_OP_SUCCESS) {
        return FILE_OP_ERROR;
    }
    
    // Add file to user's file list and update quota
    if (user_add_file(user, filename, size) != SUCCESS) {
        // If adding to user list fails, delete the file from disk to maintain consistency
        delete_file_from_disk(user->username, filename);
        return FILE_OP_ERROR;
    }
    
    printf("Upload successful: %s (%zu bytes)\n", filename, size);
    return FILE_OP_SUCCESS;
}

int handle_download(user_t *user, const char *filename, char **data, size_t *size) {
    if (!user || !filename || !data || !size) {
        return FILE_OP_ERROR;
    }
    
    printf("Processing DOWNLOAD: user=%s, file=%s\n", user->username, filename);
    
    // Check if user has the file
    if (!user_has_file(user, filename)) {
        printf("File not found in user's file list: %s\n", filename);
        return FILE_OP_NOT_FOUND;
    }
    
    // Load file from disk
    int result = load_file_from_disk(user->username, filename, data, size);
    if (result != FILE_OP_SUCCESS) {
        printf("Failed to load file from disk: %s\n", filename);
        
        // If file exists in user list but not on disk, remove it from list
        if (result == FILE_OP_NOT_FOUND) {
            user_remove_file(user, filename);
        }
    } else {
        printf("Download successful: %s (%zu bytes)\n", filename, *size);
    }
    
    return result;
}


char** handle_list(user_t *user, int *file_count) {
    if (!user || !file_count) return NULL;

    printf("Processing LIST (disk scan): user=%s\n", user->username);
    *file_count = 0;

    char user_dir[MAX_FILE_PATH];
    snprintf(user_dir, sizeof(user_dir), "%s/%s", STORAGE_BASE_DIR, user->username);

    DIR *dir = opendir(user_dir);
    if (!dir) {
        perror("Failed to open user directory");
        return NULL;
    }

    char **files = NULL;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_REG) {  // regular file
            files = realloc(files, sizeof(char*) * (*file_count + 1));
            files[*file_count] = strdup(entry->d_name);
            (*file_count)++;
        }
    }
    closedir(dir);

    if (*file_count == 0) {
        printf("No files found for user %s\n", user->username);
        free(files);
        return NULL;
    }

    printf("List successful: found %d files\n", *file_count);
    return files;
}


