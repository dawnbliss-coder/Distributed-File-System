#include "../include/nameserver.h"

void init_hash_table(FileHashTable *table) {
    for (int i = 0; i < HASH_TABLE_SIZE; i++) {
        table->buckets[i] = NULL;
    }
    pthread_mutex_init(&table->lock, NULL);
}

unsigned int hash_filename(const char *filename) {
    unsigned int hash = 5381;
    int c;
    
    while ((c = *filename++)) {
        hash = ((hash << 5) + hash) + c;
    }
    
    return hash % HASH_TABLE_SIZE;
}

int add_file_mapping(FileHashTable *table, const char *filename, int primary_ss_id) {
    pthread_mutex_lock(&table->lock);
    
    unsigned int index = hash_filename(filename);
    
    // Check if already exists
    FileMapping *current = table->buckets[index];
    while (current) {
        if (strcmp(current->filename, filename) == 0) {
            // Update existing
            current->primary_ss_id = primary_ss_id;
            pthread_mutex_unlock(&table->lock);
            return ERR_SUCCESS;
        }
        current = current->next;
    }
    
    // Create new mapping
    FileMapping *new_mapping = malloc(sizeof(FileMapping));
    if (!new_mapping) {
        pthread_mutex_unlock(&table->lock);
        return ERR_OUT_OF_MEMORY;
    }
    
    strncpy(new_mapping->filename, filename, MAX_FILENAME_LENGTH - 1);
    new_mapping->primary_ss_id = primary_ss_id;
    new_mapping->next = table->buckets[index];
    table->buckets[index] = new_mapping;
    
    pthread_mutex_unlock(&table->lock);
    return ERR_SUCCESS;
}

int get_file_primary_ss(FileHashTable *table, const char *filename) {
    pthread_mutex_lock(&table->lock);
    
    unsigned int index = hash_filename(filename);
    FileMapping *current = table->buckets[index];
    
    while (current) {
        if (strcmp(current->filename, filename) == 0) {
            int ss_id = current->primary_ss_id;
            pthread_mutex_unlock(&table->lock);
            return ss_id;
        }
        current = current->next;
    }
    
    pthread_mutex_unlock(&table->lock);
    return -1;
}

int remove_file_mapping(FileHashTable *table, const char *filename) {
    pthread_mutex_lock(&table->lock);
    
    unsigned int index = hash_filename(filename);
    FileMapping *current = table->buckets[index];
    FileMapping *prev = NULL;
    
    while (current) {
        if (strcmp(current->filename, filename) == 0) {
            if (prev) {
                prev->next = current->next;
            } else {
                table->buckets[index] = current->next;
            }
            free(current);
            pthread_mutex_unlock(&table->lock);
            return ERR_SUCCESS;
        }
        prev = current;
        current = current->next;
    }
    
    pthread_mutex_unlock(&table->lock);
    return ERR_FILE_NOT_FOUND;
}

void cleanup_hash_table(FileHashTable *table) {
    pthread_mutex_lock(&table->lock);
    
    for (int i = 0; i < HASH_TABLE_SIZE; i++) {
        FileMapping *current = table->buckets[i];
        while (current) {
            FileMapping *next = current->next;
            free(current);
            current = next;
        }
        table->buckets[i] = NULL;
    }
    
    pthread_mutex_unlock(&table->lock);
    pthread_mutex_destroy(&table->lock);
}