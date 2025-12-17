#include "../include/storageserver.h"


static char* get_metadata_path(const char *storage_dir, const char *filename) {
    char *meta_path = malloc(MAX_PATH_LENGTH);
    if (!meta_path) {
        log_message(log_file, LOG_LEVEL_ERROR, NULL, 0, NULL, 
                   "Memory allocation failed for metadata path: storage_dir='%s', filename='%s'", 
                   storage_dir, filename);
        return NULL;
    }
    
    snprintf(meta_path, MAX_PATH_LENGTH, "%s/%s.meta", storage_dir, filename);
    log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
               "Generated metadata path: %s", meta_path);
    return meta_path;
}

// Save metadata to .meta file
int save_metadata(const char *storage_dir, const FileMetadata *metadata) {
    log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
               "Saving metadata: filename='%s', storage_dir='%s'", 
               metadata->filename, storage_dir);
    
    char *meta_path = get_metadata_path(storage_dir, metadata->filename);
    if (!meta_path) {
        log_message(log_file, LOG_LEVEL_ERROR, NULL, 0, NULL, 
                   "Failed to get metadata path for '%s'", metadata->filename);
        return ERR_OUT_OF_MEMORY;
    }
    
    FILE *fp = fopen(meta_path, "wb");
    if (!fp) {
        log_message(log_file, LOG_LEVEL_ERROR, NULL, 0, NULL, 
                   "Failed to open metadata file for writing: %s (errno=%d)", 
                   meta_path, errno);
        free(meta_path);
        return ERR_FILE_WRITE_FAILED;
    }
    
    // Write metadata structure
    size_t written = fwrite(metadata, sizeof(FileMetadata), 1, fp);
    fclose(fp);
    
    if (written != 1) {
        log_message(log_file, LOG_LEVEL_ERROR, NULL, 0, NULL, 
                   "Failed to write metadata: %s (written=%zu, expected=1)", 
                   meta_path, written);
        free(meta_path);
        return ERR_FILE_WRITE_FAILED;
    }
    
    log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
               "Metadata saved successfully: filename='%s', size=%zu, words=%d, sentences=%d, chars=%d", 
               metadata->filename, metadata->size, metadata->word_count, 
               metadata->sentence_count, metadata->char_count);
    
    free(meta_path);
    return ERR_SUCCESS;
}

// Load metadata from .meta file
int load_metadata(const char *storage_dir, const char *filename, FileMetadata *metadata) {
    log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
               "Loading metadata: filename='%s', storage_dir='%s'", 
               filename, storage_dir);
    
    char *meta_path = get_metadata_path(storage_dir, filename);
    if (!meta_path) {
        log_message(log_file, LOG_LEVEL_ERROR, NULL, 0, NULL, 
                   "Failed to get metadata path for '%s'", filename);
        return ERR_OUT_OF_MEMORY;
    }
    
    FILE *fp = fopen(meta_path, "rb");
    if (!fp) {
        log_message(log_file, LOG_LEVEL_WARNING, NULL, 0, NULL, 
                   "Metadata file not found: %s (errno=%d)", meta_path, errno);
        free(meta_path);
        return ERR_FILE_NOT_FOUND;
    }
    
    // Read metadata structure
    size_t read = fread(metadata, sizeof(FileMetadata), 1, fp);
    fclose(fp);
    
    if (read != 1) {
        log_message(log_file, LOG_LEVEL_ERROR, NULL, 0, NULL, 
                   "Failed to read metadata from %s (read=%zu, expected=1)", 
                   meta_path, read);
        free(meta_path);
        return ERR_FILE_READ_FAILED;
    }
    
    log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
               "Metadata loaded successfully: filename='%s', owner='%s', size=%zu, words=%d, sentences=%d", 
               metadata->filename, metadata->owner, metadata->size, 
               metadata->word_count, metadata->sentence_count);
    
    free(meta_path);
    return ERR_SUCCESS;
}

// Update file statistics (word count, sentence count, etc.)
int update_file_stats(const char *storage_dir, FileMetadata *metadata) {
    log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
               "Updating file statistics: filename='%s'", metadata->filename);
    
    char content[LARGE_BUFFER_SIZE];
    int read_result = ss_read_file(storage_dir, metadata->filename, content, sizeof(content));
    
    if (read_result != ERR_SUCCESS) {
        log_message(log_file, LOG_LEVEL_ERROR, NULL, 0, NULL, 
                   "Failed to read file for stats update: filename='%s', error=%d", 
                   metadata->filename, read_result);
        return ERR_FILE_READ_FAILED;
    }
    
    // Store old values for logging comparison
    int old_word_count = metadata->word_count;
    int old_sentence_count = metadata->sentence_count;
    int old_char_count = metadata->char_count;
    
    // Count characters
    metadata->char_count = strlen(content);
    
    // Count words
    metadata->word_count = 0;
    int in_word = 0;
    for (char *p = content; *p; p++) {
        if (*p == ' ' || *p == '\t' || *p == '\n') {
            in_word = 0;
        } else if (!in_word) {
            in_word = 1;
            metadata->word_count++;
        }
    }
    
    // Count sentences
    metadata->sentence_count = 0;
    for (char *p = content; *p; p++) {
        if (is_sentence_delimiter(*p)) {
            metadata->sentence_count++;
        }
    }
    
    if (metadata->sentence_count == 0 && metadata->word_count > 0) {
        log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
                   "No sentence delimiters found, defaulting to 1 sentence");
        metadata->sentence_count = 1;  // Content without delimiter is one sentence
    }
    
    log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
               "File statistics updated: filename='%s', chars=%d (%+d), words=%d (%+d), sentences=%d (%+d)", 
               metadata->filename, 
               metadata->char_count, metadata->char_count - old_char_count,
               metadata->word_count, metadata->word_count - old_word_count,
               metadata->sentence_count, metadata->sentence_count - old_sentence_count);
    
    return ERR_SUCCESS;
}
