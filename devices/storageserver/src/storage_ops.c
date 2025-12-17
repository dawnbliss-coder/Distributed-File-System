#include "../include/storageserver.h"
#include <dirent.h>


// Initialize storage directory
int create_storage_directory(const char *storage_dir) {
    log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
               "Creating storage directory: '%s'", storage_dir);
    
    struct stat st = {0};
    
    if (stat(storage_dir, &st) == -1) {
        log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
                   "Directory does not exist, attempting to create");
        
        if (mkdir(storage_dir, 0755) == -1) {
            log_message(log_file, LOG_LEVEL_ERROR, NULL, 0, NULL, 
                       "Failed to create directory '%s' (errno=%d: %s)", 
                       storage_dir, errno, strerror(errno));
            perror("mkdir");
            return ERR_INITIALIZATION_FAILED;
        }
        
        log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
                   "Storage directory created successfully: '%s'", storage_dir);
    } else {
        log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
                   "Storage directory already exists: '%s'", storage_dir);
    }
    
    return ERR_SUCCESS;
}

// Create backup before modification (for UNDO)
int ss_backup_file(const char *storage_dir, const char *filename) {
    log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
               "Creating backup: storage_dir='%s', filename='%s'", 
               storage_dir, filename);
    
    char *file_path = get_file_path(storage_dir, filename);
    if (!file_path) {
        log_message(log_file, LOG_LEVEL_ERROR, NULL, 0, NULL, 
                   "Failed to allocate file path for backup: %s", filename);
        return ERR_OUT_OF_MEMORY;
    }
    
    // Check if source file exists
    if (access(file_path, F_OK) != 0) {
        log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
                   "Source file does not exist, skipping backup: %s", file_path);
        free(file_path);
        return ERR_SUCCESS;  // Not an error if file doesn't exist yet
    }
    
    char backup_path[MAX_PATH_LENGTH];
    snprintf(backup_path, sizeof(backup_path), "%s.backup", file_path);
    
    // Copy file to backup
    char cmd[BUFFER_SIZE];
    snprintf(cmd, sizeof(cmd), "cp %s %s", file_path, backup_path);
    
    log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
               "Executing backup command: %s", cmd);
    
    int result = system(cmd);
    
    if (result == 0) {
        log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
                   "Backup created successfully: %s -> %s", file_path, backup_path);
    } else {
        log_message(log_file, LOG_LEVEL_ERROR, NULL, 0, NULL, 
                   "Backup failed: %s (system result=%d)", file_path, result);
    }
    
    free(file_path);
    return (result == 0) ? ERR_SUCCESS : ERR_FILE_WRITE_FAILED;
}

// Get file path
char* get_file_path(const char *storage_dir, const char *filename) {
    log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
               "Generating file path: storage_dir='%s', filename='%s'", 
               storage_dir, filename);
    
    char *path = malloc(MAX_PATH_LENGTH);
    if (!path) {
        log_message(log_file, LOG_LEVEL_ERROR, NULL, 0, NULL, 
                   "Failed to allocate memory for file path");
        return NULL;
    }
    
    snprintf(path, MAX_PATH_LENGTH, "%s/%s", storage_dir, filename);
    
    log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
               "Generated file path: %s", path);
    
    return path;
}

// Get metadata file path
char* get_metadata_path(const char *storage_dir, const char *filename) {
    log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
               "Generating metadata path: storage_dir='%s', filename='%s'", 
               storage_dir, filename);
    
    char *path = malloc(MAX_PATH_LENGTH);
    if (!path) {
        log_message(log_file, LOG_LEVEL_ERROR, NULL, 0, NULL, 
                   "Failed to allocate memory for metadata path");
        return NULL;
    }
    
    snprintf(path, MAX_PATH_LENGTH, "%s/%s.meta", storage_dir, filename);
    
    log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
               "Generated metadata path: %s", path);
    
    return path;
}

// Create new file
int ss_create_file(const char *storage_dir, const char *filename, const char *owner) {
    log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
               "Creating file: storage_dir='%s', filename='%s', owner='%s'", 
               storage_dir, filename, owner);
    
    if (!is_valid_filename(filename)) {
        log_message(log_file, LOG_LEVEL_WARNING, NULL, 0, NULL, 
                   "Invalid filename rejected: '%s'", filename);
        return ERR_INVALID_FILENAME;
    }
    
    char *file_path = get_file_path(storage_dir, filename);
    if (!file_path) {
        log_message(log_file, LOG_LEVEL_ERROR, NULL, 0, NULL, 
                   "Memory allocation failed for file path");
        return ERR_OUT_OF_MEMORY;
    }
    
    // Check if file already exists
    if (access(file_path, F_OK) == 0) {
        log_message(log_file, LOG_LEVEL_WARNING, NULL, 0, NULL, 
                   "File already exists: %s", file_path);
        free(file_path);
        return ERR_FILE_ALREADY_EXISTS;
    }
    
    // Create empty file
    FILE *fp = fopen(file_path, "w");
    if (!fp) {
        log_message(log_file, LOG_LEVEL_ERROR, NULL, 0, NULL, 
                   "Failed to create file: %s (errno=%d: %s)", 
                   file_path, errno, strerror(errno));
        free(file_path);
        return ERR_FILE_OPEN_FAILED;
    }
    fclose(fp);
    
    log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
               "Empty file created: %s", file_path);
    
    // Create metadata
    FileMetadata metadata = {0};
    strncpy(metadata.filename, filename, MAX_FILENAME_LENGTH - 1);
    strncpy(metadata.owner, owner, MAX_USERNAME_LENGTH - 1);
    strncpy(metadata.path, file_path, MAX_PATH_LENGTH - 1);
    metadata.size = 0;
    metadata.word_count = 0;
    metadata.char_count = 0;
    metadata.sentence_count = 0;
    metadata.created_time = time(NULL);
    metadata.modified_time = metadata.created_time;
    metadata.accessed_time = metadata.created_time;
    metadata.is_folder = 0;
    
    // Initialize access rights (owner has full access)
    for (int i = 0; i < MAX_USERS; i++) {
        metadata.access_rights[i] = ACCESS_NONE;
    }
    // For simplicity, owner is at index 0 - you'll need a proper user mapping
    metadata.access_rights[0] = ACCESS_OWNER;
    
    log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
               "Metadata initialized for file: %s", filename);
    
    int result = save_metadata(storage_dir, &metadata);
    free(file_path);
    
    if (result == ERR_SUCCESS) {
        log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
                   "File created successfully: filename='%s', owner='%s'", 
                   filename, owner);
    } else {
        log_message(log_file, LOG_LEVEL_ERROR, NULL, 0, NULL, 
                   "File creation failed during metadata save: %s (error=%d)", 
                   filename, result);
    }
    
    return result;
}

// Delete file
int ss_delete_file(const char *storage_dir, const char *filename) {
    log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
               "Deleting file: storage_dir='%s', filename='%s'", 
               storage_dir, filename);
    
    char *file_path = get_file_path(storage_dir, filename);
    char *meta_path = get_metadata_path(storage_dir, filename);
    
    if (!file_path || !meta_path) {
        log_message(log_file, LOG_LEVEL_ERROR, NULL, 0, NULL, 
                   "Memory allocation failed for delete paths: filename='%s'", filename);
        free(file_path);
        free(meta_path);
        return ERR_OUT_OF_MEMORY;
    }
    
    // Check if file exists before attempting delete
    if (access(file_path, F_OK) != 0) {
        log_message(log_file, LOG_LEVEL_WARNING, NULL, 0, NULL, 
                   "File not found for deletion: %s", file_path);
        free(file_path);
        free(meta_path);
        return ERR_FILE_NOT_FOUND;
    }
    
    // Delete data file
    if (unlink(file_path) != 0) {
        log_message(log_file, LOG_LEVEL_ERROR, NULL, 0, NULL, 
                   "Failed to delete file: %s (errno=%d: %s)", 
                   file_path, errno, strerror(errno));
        free(file_path);
        free(meta_path);
        return ERR_FILE_DELETE_FAILED;
    }
    
    log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
               "Data file deleted: %s", file_path);
    
    // Delete metadata file
    if (unlink(meta_path) != 0) {
        log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
                   "Metadata file deletion failed or not found: %s (errno=%d)", 
                   meta_path, errno);
    } else {
        log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
                   "Metadata file deleted: %s", meta_path);
    }
    
    // Delete backup file if exists
    char backup_path[MAX_PATH_LENGTH];
    snprintf(backup_path, sizeof(backup_path), "%s.backup", file_path);
    if (unlink(backup_path) == 0) {
        log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
                   "Backup file deleted: %s", backup_path);
    }
    
    free(file_path);
    free(meta_path);
    
    log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
               "File deleted successfully: %s", filename);
    
    return ERR_SUCCESS;
}

// Read entire file
int ss_read_file(const char *storage_dir, const char *filename, char *buffer, size_t buffer_size) {
    log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
               "Reading file: storage_dir='%s', filename='%s', buffer_size=%zu", 
               storage_dir, filename, buffer_size);
    
    char *file_path = get_file_path(storage_dir, filename);
    if (!file_path) {
        log_message(log_file, LOG_LEVEL_ERROR, NULL, 0, NULL, 
                   "Memory allocation failed for read operation: %s", filename);
        return ERR_OUT_OF_MEMORY;
    }
    
    FILE *fp = fopen(file_path, "r");
    if (!fp) {
        log_message(log_file, LOG_LEVEL_WARNING, NULL, 0, NULL, 
                   "File not found for reading: %s (errno=%d: %s)", 
                   file_path, errno, strerror(errno));
        free(file_path);
        return ERR_FILE_NOT_FOUND;
    }
    
    // Get file size
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    
    log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
               "File opened for reading: %s (size=%ld bytes)", file_path, file_size);
    
    size_t bytes_read = fread(buffer, 1, buffer_size - 1, fp);
    buffer[bytes_read] = '\0';
    
    fclose(fp);
    
    if (bytes_read < (size_t)file_size && file_size >= (long)buffer_size) {
        log_message(log_file, LOG_LEVEL_WARNING, NULL, 0, NULL, 
                   "File truncated during read: %s (file_size=%ld, buffer_size=%zu, read=%zu)", 
                   filename, file_size, buffer_size, bytes_read);
    }
    
    log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
               "File read successfully: filename='%s', bytes_read=%zu", 
               filename, bytes_read);
    
    free(file_path);
    
    // Update access time
    FileMetadata metadata;
    if (load_metadata(storage_dir, filename, &metadata) == ERR_SUCCESS) {
        metadata.accessed_time = time(NULL);
        save_metadata(storage_dir, &metadata);
        log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
                   "Access time updated for: %s", filename);
    } else {
        log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
                   "Could not update access time (metadata not found): %s", filename);
    }
    
    return ERR_SUCCESS;
}

int ss_write_file(const char *storage_dir, const char *filename, const char *content) {
    size_t content_length = strlen(content);
    
    log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
               "Writing file: storage_dir='%s', filename='%s', content_length=%zu", 
               storage_dir, filename, content_length);
    
    // Create backup first
    int backup_result = ss_backup_file(storage_dir, filename);
    if (backup_result != ERR_SUCCESS) {
        log_message(log_file, LOG_LEVEL_WARNING, NULL, 0, NULL, 
                   "Backup creation failed, continuing with write: %s", filename);
    }
    
    char *file_path = get_file_path(storage_dir, filename);
    if (!file_path) {
        log_message(log_file, LOG_LEVEL_ERROR, NULL, 0, NULL, 
                   "Memory allocation failed for write operation: %s", filename);
        return ERR_OUT_OF_MEMORY;
    }
    
    FILE *fp = fopen(file_path, "w");
    if (!fp) {
        log_message(log_file, LOG_LEVEL_ERROR, NULL, 0, NULL, 
                   "Failed to open file for writing: %s (errno=%d: %s)", 
                   file_path, errno, strerror(errno));
        free(file_path);
        return ERR_FILE_WRITE_FAILED;
    }
    
    size_t len = strlen(content);
    size_t written = fwrite(content, 1, len, fp);
    fclose(fp);
    
    if (written != len) {
        log_message(log_file, LOG_LEVEL_ERROR, NULL, 0, NULL, 
                   "Incomplete write: %s (expected=%zu, written=%zu)", 
                   file_path, len, written);
        free(file_path);
        return ERR_FILE_WRITE_FAILED;
    }
    
    log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
               "File written successfully: %s (%zu bytes)", file_path, written);
    
    free(file_path);
    
    // Update metadata
    FileMetadata metadata;
    if (load_metadata(storage_dir, filename, &metadata) == ERR_SUCCESS) {
        time_t old_modified = metadata.modified_time;
        size_t old_size = metadata.size;
        
        metadata.modified_time = time(NULL);
        metadata.size = len;
        
        int stats_result = update_file_stats(storage_dir, &metadata);
        if (stats_result != ERR_SUCCESS) {
            log_message(log_file, LOG_LEVEL_WARNING, NULL, 0, NULL, 
                       "Failed to update file stats: %s", filename);
        }
        
        int meta_result = save_metadata(storage_dir, &metadata);
        if (meta_result != ERR_SUCCESS) {
            log_message(log_file, LOG_LEVEL_WARNING, NULL, 0, NULL, 
                       "Failed to save metadata after write: %s", filename);
        } else {
            log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
                       "Metadata updated: %s (size: %zu->%zu, words=%d, sentences=%d)", 
                       filename, old_size, metadata.size, 
                       metadata.word_count, metadata.sentence_count);
        }
    } else {
        log_message(log_file, LOG_LEVEL_WARNING, NULL, 0, NULL, 
                   "Could not load metadata for update: %s", filename);
    }
    
    log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
               "File write operation completed: filename='%s', size=%zu", 
               filename, len);
    
    return ERR_SUCCESS;
}

// List all files in storage directory
int list_files(const char *storage_dir, char files[][MAX_FILENAME_LENGTH], int max_files) {
    log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
               "Listing files: storage_dir='%s', max_files=%d", 
               storage_dir, max_files);
    
    DIR *dir = opendir(storage_dir);
    if (!dir) {
        log_message(log_file, LOG_LEVEL_ERROR, NULL, 0, NULL, 
                   "Failed to open directory: %s (errno=%d: %s)", 
                   storage_dir, errno, strerror(errno));
        return 0;
    }
    
    struct dirent *entry;
    int count = 0;
    int skipped_meta = 0;
    int skipped_backup = 0;
    int skipped_hidden = 0;
    
    while ((entry = readdir(dir)) != NULL && count < max_files) {
        // Skip . and ..
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            skipped_hidden++;
            continue;
        }
        
        // Skip .meta files
        size_t len = strlen(entry->d_name);
        if (len > 5 && strcmp(entry->d_name + len - 5, ".meta") == 0) {
            skipped_meta++;
            log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
                       "Skipping metadata file: %s", entry->d_name);
            continue;
        }
        if (len > 7 && strcmp(entry->d_name + len - 7, ".backup") == 0) {
            skipped_backup++;
            log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
                       "Skipping backup file: %s", entry->d_name);
            continue;
        }
        
        strncpy(files[count], entry->d_name, MAX_FILENAME_LENGTH - 1);
        files[count][MAX_FILENAME_LENGTH - 1] = '\0';
        
        log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
                   "Listed file [%d]: %s", count, entry->d_name);
        count++;
    }
    
    closedir(dir);
    
    log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
               "Directory listing complete: %d files found (skipped: %d meta, %d backup, %d hidden)", 
               count, skipped_meta, skipped_backup, skipped_hidden);
    
    return count;
}
