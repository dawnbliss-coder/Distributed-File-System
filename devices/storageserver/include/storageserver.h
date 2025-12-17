#ifndef STORAGESERVER_H
#define STORAGESERVER_H

#include "../../common/common.h"

#define LOG_FILE ".sslogs"
extern FILE* log_file;

// ============================================================================
// SENTENCE AND WORD STRUCTURES
// ============================================================================

// Word node in doubly-linked list
typedef struct WordNode {
    char *content;
    struct WordNode *prev;
    struct WordNode *next;
} WordNode;

// Sentence node in linked list (contains word list)
typedef struct SentenceNode {
    WordNode *word_head;
    WordNode *word_tail;
    int word_count;
    char delimiter;  // . ! ? or \0

    // Per-sentence lock (for thread safety within modifications)
    int is_locked;
    char locked_by[MAX_USERNAME_LENGTH];
    pthread_mutex_t sentence_lock;

    struct SentenceNode *next;
} SentenceNode;

// File content structure
typedef struct {
    char filename[MAX_FILENAME_LENGTH];
    SentenceNode *head;
    int sentence_count;
    pthread_mutex_t file_lock;
} FileContent;

// ============================================================================
// GLOBAL SENTENCE LOCK STRUCTURES (for cross-client locking)
// ============================================================================

// Global sentence lock entry (shared across all clients)
typedef struct SentenceLockEntry {
    char filename[MAX_FILENAME_LENGTH];
    int sentence_num;
    int is_locked;
    char locked_by[MAX_USERNAME_LENGTH];
    pthread_mutex_t mutex;
    time_t lock_time;
    struct SentenceLockEntry *next;
} SentenceLockEntry;

// ============================================================================
// STORAGE SERVER CONFIGURATION
// ============================================================================

typedef struct {
    int id;
    char storage_dir[MAX_PATH_LENGTH];
    int client_port;
    int client_socket;
    int is_running;
    pthread_mutex_t storage_lock;

    // Global lock table for sentence-level locking
    SentenceLockEntry *global_locks;
    pthread_mutex_t lock_table_mutex;

} StorageServerConfig;

// ============================================================================
// SENTENCE OPERATIONS
// ============================================================================

FileContent* load_file_content(const char *storage_dir, const char *filename);
int save_file_content(const char *storage_dir, FileContent *file_content);
void free_file_content(FileContent *file_content);

int lock_sentence(FileContent *file, int sentence_num, const char *username);
int unlock_sentence(FileContent *file, int sentence_num, const char *username);
int modify_sentence(FileContent *file, int sentence_num, int word_index, 
                    const char *new_content, const char *username);

// ✅ NEW: Multi-word insertion function
int modify_sentence_multiword(FileContent *file, int sentence_num, int word_index, 
                              const char *new_content, const char *username, int *new_sentence_num);

char* get_sentence_string(SentenceNode *sentence);

// Helper functions
char* word_list_to_string(WordNode *word_head, char delimiter);
WordNode* create_word_node(const char *word_content);

// Global sentence locking functions
int global_try_lock_sentence(StorageServerConfig *ctx, const char *filename, 
                             int sentence_num, const char *username);
int global_unlock_sentence(StorageServerConfig *ctx, const char *filename, 
                          int sentence_num, const char *username);

// ============================================================================
// FILE OPERATIONS
// ============================================================================

int create_storage_directory(const char *storage_dir);
char* get_file_path(const char *storage_dir, const char *filename);

int ss_create_file(const char *storage_dir, const char *filename, const char *owner);
int ss_delete_file(const char *storage_dir, const char *filename);
int ss_read_file(const char *storage_dir, const char *filename, char *buffer, size_t buffer_size);
int ss_write_file(const char *storage_dir, const char *filename, const char *content);
int ss_backup_file(const char *storage_dir, const char *filename);

int list_files(const char *storage_dir, char files[][MAX_FILENAME_LENGTH], int max_files);

// ============================================================================
// METADATA OPERATIONS
// ============================================================================

int save_metadata(const char *storage_dir, const FileMetadata *metadata);
int load_metadata(const char *storage_dir, const char *filename, FileMetadata *metadata);
int update_file_stats(const char *storage_dir, FileMetadata *metadata);

#endif // STORAGESERVER_H