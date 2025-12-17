#ifndef COMMON_H
#define COMMON_H

#define _XOPEN_SOURCE 700
#include<sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include <errno.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <ctype.h>
#include <stdarg.h>

// ============================================================================
// SYSTEM-WIDE CONSTANTS
// ============================================================================

#define BUFFER_SIZE 8192
#define LARGE_BUFFER_SIZE 16384
#define MAX_COMMAND_LENGTH 4096
#define MAX_FILENAME_LENGTH 256
#define MAX_FOLDERNAME_LENGTH 256
#define MAX_USERNAME_LENGTH 64
#define MAX_PATH_LENGTH 512
#define MAX_CLIENTS 100
#define MAX_STORAGE_SERVERS 50
#define MAX_FILES_PER_SS 1000
#define MAX_USERS 500
#define MAX_SENTENCE_LENGTH 2048
#define MAX_WORD_LENGTH 256

// Timing constants
#define STREAM_DELAY_MS 100
#define CONNECTION_TIMEOUT_SEC 30
#define HEARTBEAT_INTERVAL_SEC 10
#define MAX_RETRIES 3
#define RETRY_DELAY_MS 500

// Protocol delimiters
#define PROTOCOL_DELIMITER "|"
#define SENTENCE_DELIMITERS ".!?"
#define WORD_DELIMITER " "

// ============================================================================
// PROTOCOL MESSAGE TYPES
// ============================================================================

// Initialization messages
#define MSG_INIT "INIT"
#define MSG_REGISTER_SS "REGISTER_SS"
#define MSG_REGISTER_CLIENT "INIT"
#define MSG_HEARTBEAT "HEARTBEAT"
#define MSG_DISCONNECT "DISCONNECT"

// File operation messages
#define MSG_VIEW "VIEW"
#define MSG_READ "READ"
#define MSG_CREATE "CREATE"
#define MSG_WRITE "WRITE"
#define MSG_UNDO "UNDO"
#define MSG_INFO "INFO"
#define MSG_DELETE "DELETE"
#define MSG_STREAM "STREAM"
#define MSG_COPY "COPY"
#define MSG_MOVE "MOVE"

// User and access control messages
#define MSG_LIST "LIST"
#define MSG_LIST_USERS "LIST_USERS"
#define MSG_ADDACCESS "ADDACCESS"
#define MSG_REMACCESS "REMACCESS"
#define MSG_REQUESTACCESS "REQUESTACCESS"
#define MSG_APPROVEACCESS "APPROVEACCESS"
#define MSG_DENYACCESS "DENYACCESS"

// Execution messages
#define MSG_EXEC "EXEC"

// Folder operations (bonus)
#define MSG_CREATEFOLDER "CREATEFOLDER"
#define MSG_VIEWFOLDER "VIEWFOLDER"
#define MSG_MOVEFILE "MOVEFILE"

// Checkpoint operations (bonus)
#define MSG_CHECKPOINT "CHECKPOINT"
#define MSG_VIEWCHECKPOINT "VIEWCHECKPOINT"
#define MSG_REVERT "REVERT"
#define MSG_LISTCHECKPOINTS "LISTCHECKPOINTS"

// Locking messages
#define MSG_LOCK "LOCK"
#define MSG_UNLOCK "UNLOCK"
#define MSG_LOCK_SENTENCE "LOCK_SENTENCE"
#define MSG_UNLOCK_SENTENCE "UNLOCK_SENTENCE"

// Response messages
#define MSG_STOP "STOP"
#define MSG_ACK "ACK"
#define MSG_NACK "NACK"
#define MSG_SUCCESS "SUCCESS"
#define MSG_ERROR "ERROR"
#define MSG_REDIRECT "REDIRECT"
#define MSG_SS_INFO "SS_INFO"

// Write operation special markers
#define MSG_WRITE_END "ETIRW"
#define MSG_WRITE_CONTINUE "CONTINUE"

// ============================================================================
// ERROR CODES
// ============================================================================

// Success
#define ERR_SUCCESS 0

// Connection errors (1xx)
#define ERR_CONNECTION_FAILED 100
#define ERR_SOCKET_CREATE_FAILED 101
#define ERR_BIND_FAILED 102
#define ERR_LISTEN_FAILED 103
#define ERR_ACCEPT_FAILED 104
#define ERR_CONNECT_FAILED 105
#define ERR_DISCONNECTED 106
#define ERR_TIMEOUT 107
#define ERR_SS_UNAVAILABLE 108
#define ERR_NM_UNAVAILABLE 109

// Communication errors (2xx)
#define ERR_SEND_FAILED 200
#define ERR_RECV_FAILED 201
#define ERR_INVALID_MESSAGE 202
#define ERR_PROTOCOL_ERROR 203
#define ERR_BUFFER_OVERFLOW 204
#define ERR_MALFORMED_REQUEST 205

// File operation errors (3xx)
#define ERR_FILE_NOT_FOUND 300
#define ERR_FILE_ALREADY_EXISTS 301
#define ERR_FILE_OPEN_FAILED 302
#define ERR_FILE_READ_FAILED 303
#define ERR_FILE_WRITE_FAILED 304
#define ERR_FILE_DELETE_FAILED 305
#define ERR_FILE_LOCKED 306
#define ERR_FILE_CORRUPTED 307
#define ERR_FILE_TOO_LARGE 308
#define ERR_INVALID_FILENAME 309

// Access control errors (4xx)
#define ERR_ACCESS_DENIED 400
#define ERR_PERMISSION_DENIED 401
#define ERR_NOT_OWNER 402
#define ERR_USER_NOT_FOUND 403
#define ERR_INVALID_USERNAME 404
#define ERR_ALREADY_HAS_ACCESS 405
#define ERR_NO_ACCESS 406

// Operation errors (5xx)
#define ERR_INVALID_COMMAND 500
#define ERR_INVALID_PARAMETER 501
#define ERR_INVALID_INDEX 502
#define ERR_SENTENCE_INDEX_OUT_OF_RANGE 503
#define ERR_WORD_INDEX_OUT_OF_RANGE 504
#define ERR_INVALID_OPERATION 505
#define ERR_OPERATION_FAILED 506
#define ERR_NOTHING_TO_UNDO 507
#define ERR_UNDO_FAILED 508

// Resource errors (6xx)
#define ERR_OUT_OF_MEMORY 600
#define ERR_MAX_CLIENTS_REACHED 601
#define ERR_MAX_SERVERS_REACHED 602
#define ERR_MAX_FILES_REACHED 603
#define ERR_RESOURCE_BUSY 604
#define ERR_DEADLOCK_DETECTED 605

// Storage server errors (7xx)
#define ERR_SS_FAILURE 700
#define ERR_SS_NOT_REGISTERED 701
#define ERR_SS_ALREADY_REGISTERED 702
#define ERR_NO_SS_AVAILABLE 703
#define ERR_REPLICATION_FAILED 704
#define ERR_SYNC_FAILED 705

// System errors (8xx)
#define ERR_INTERNAL_ERROR 800
#define ERR_NOT_IMPLEMENTED 801
#define ERR_INITIALIZATION_FAILED 802
#define ERR_CONFIGURATION_ERROR 803
#define ERR_SYSTEM_FAILURE 804

// Folder errors (9xx - bonus)
#define ERR_FOLDER_NOT_FOUND 900
#define ERR_FOLDER_ALREADY_EXISTS 901
#define ERR_NOT_A_FOLDER 902
#define ERR_FOLDER_NOT_EMPTY 903

// Checkpoint errors (10xx - bonus)
#define ERR_CHECKPOINT_NOT_FOUND 1000
#define ERR_CHECKPOINT_ALREADY_EXISTS 1001
#define ERR_CHECKPOINT_FAILED 1002

// ============================================================================
// ACCESS CONTROL CONSTANTS
// ============================================================================

#define ACCESS_NONE 0
#define ACCESS_READ 1
#define ACCESS_WRITE 2
#define ACCESS_READ_WRITE 3
#define ACCESS_OWNER 4

#define ACCESS_FLAG_READ "-R"
#define ACCESS_FLAG_WRITE "-W"
#define ACCESS_FLAG_READWRITE "-RW"

// ============================================================================
// VIEW FLAGS
// ============================================================================

#define VIEW_FLAG_ALL "-a"
#define VIEW_FLAG_LONG "-l"
#define VIEW_FLAG_ALL_LONG "-al"
#define VIEW_FLAG_LONG_ALL "-la"

// ============================================================================
// LOGGING LEVELS
// ============================================================================

typedef enum {
    LOG_LEVEL_DEBUG = 0,
    LOG_LEVEL_INFO = 1,
    LOG_LEVEL_WARNING = 2,
    LOG_LEVEL_ERROR = 3,
    LOG_LEVEL_CRITICAL = 4
} LogLevel;


// ============================================================================
// COMMON STRUCTURES
// ============================================================================

// File metadata structure
typedef struct {
    char filename[MAX_FILENAME_LENGTH];
    char owner[MAX_USERNAME_LENGTH];
    char path[MAX_PATH_LENGTH];
    size_t size;
    int word_count;
    int char_count;
    int sentence_count;
    time_t created_time;
    time_t modified_time;
    time_t accessed_time;
    int access_rights[MAX_USERS];
    int locked_sentences[1000];  // Bitmap for locked sentences
    int is_folder;
} FileMetadata;

// User information structure
typedef struct {
    char username[MAX_USERNAME_LENGTH];
    char ip[INET_ADDRSTRLEN];
    int port;
    int is_connected;
    time_t connected_time;
} UserInfo;

// Storage server information structure
typedef struct {
    int id;
    char ip[INET_ADDRSTRLEN];
    int nm_port;
    int client_port;
    int is_active;
    time_t last_heartbeat;
    char files[MAX_FILES_PER_SS][MAX_FILENAME_LENGTH];
    int file_count;
    int socket_fd;
} StorageServerInfo;

// Message structure
typedef struct {
    char type[32];
    char payload[LARGE_BUFFER_SIZE];
    int error_code;
    time_t timestamp;
} Message;

// ============================================================================
// INLINE UTILITY FUNCTIONS - ALL STATIC INLINE
// ============================================================================

// Get error message from error code
static inline const char* get_error_message(int error_code) {
    switch (error_code) {
        // Connection errors
        case ERR_CONNECTION_FAILED: return "Connection failed";
        case ERR_SOCKET_CREATE_FAILED: return "Socket creation failed";
        case ERR_BIND_FAILED: return "Bind failed";
        case ERR_LISTEN_FAILED: return "Listen failed";
        case ERR_TIMEOUT: return "Connection timeout";
        case ERR_SS_UNAVAILABLE: return "Storage server unavailable";
        case ERR_NM_UNAVAILABLE: return "Name server unavailable";
        
        // Communication errors
        case ERR_SEND_FAILED: return "Send failed";
        case ERR_RECV_FAILED: return "Receive failed";
        case ERR_INVALID_MESSAGE: return "Invalid message";
        case ERR_PROTOCOL_ERROR: return "Protocol error";
        
        // File operation errors
        case ERR_FILE_NOT_FOUND: return "File not found";
        case ERR_FILE_ALREADY_EXISTS: return "File already exists";
        case ERR_FILE_LOCKED: return "File is locked by another user";
        case ERR_FILE_READ_FAILED: return "Failed to read file";
        case ERR_FILE_WRITE_FAILED: return "Failed to write file";
        case ERR_FILE_DELETE_FAILED: return "Failed to delete file";
        case ERR_INVALID_FILENAME: return "Invalid filename";
        
        // Access control errors
        case ERR_ACCESS_DENIED: return "Access denied";
        case ERR_PERMISSION_DENIED: return "Permission denied";
        case ERR_NOT_OWNER: return "Only the owner can perform this operation";
        case ERR_USER_NOT_FOUND: return "User not found";
        case ERR_INVALID_USERNAME: return "Invalid username";
        
        // Operation errors
        case ERR_INVALID_COMMAND: return "Invalid command";
        case ERR_INVALID_PARAMETER: return "Invalid parameter";
        case ERR_SENTENCE_INDEX_OUT_OF_RANGE: return "Sentence index out of range";
        case ERR_WORD_INDEX_OUT_OF_RANGE: return "Word index out of range";
        case ERR_NOTHING_TO_UNDO: return "Nothing to undo";
        
        // Resource errors
        case ERR_OUT_OF_MEMORY: return "Out of memory";
        case ERR_MAX_CLIENTS_REACHED: return "Maximum clients reached";
        case ERR_RESOURCE_BUSY: return "Resource busy";
        
        // System errors
        case ERR_INTERNAL_ERROR: return "Internal error";
        case ERR_NOT_IMPLEMENTED: return "Feature not implemented";
        case ERR_SYSTEM_FAILURE: return "System failure";
        case ERR_INITIALIZATION_FAILED: return "Initialization failed";
        
        default: return "Unknown error";
    }
}

// Check if character is a sentence delimiter
static inline int is_sentence_delimiter(char c) {
    return (c == '.' || c == '!' || c == '?');
}

// Trim whitespace from string
static inline void trim_whitespace(char *str) {
    if (str == NULL) return;
    
    // Trim leading whitespace
    char *start = str;
    while (*start && (*start == ' ' || *start == '\t' || *start == '\n' || *start == '\r')) {
        start++;
    }
    
    // Trim trailing whitespace
    char *end = start + strlen(start) - 1;
    while (end > start && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')) {
        end--;
    }
    *(end + 1) = '\0';
    
    // Move trimmed string to beginning
    if (start != str) {
        memmove(str, start, strlen(start) + 1);
    }
}

// Validate filename
static inline int is_valid_filename(const char *filename) {
    if (filename == NULL || strlen(filename) == 0 || strlen(filename) >= MAX_FILENAME_LENGTH) {
        return 0;
    }
    
    // Check for invalid characters
    const char *invalid_chars = "/\\:*?\"<>|";
    for (const char *p = filename; *p; p++) {
        if (strchr(invalid_chars, *p)) {
            return 0;
        }
    }
    
    return 1;
}

// Validate username
static inline int is_valid_username(const char *username) {
    if (username == NULL || strlen(username) == 0 || strlen(username) >= MAX_USERNAME_LENGTH) {
        return 0;
    }
    
    // Username should only contain alphanumeric characters and underscores
    for (const char *p = username; *p; p++) {
        if (!isalnum(*p) && *p != '_') {
            return 0;
        }
    }
    
    return 1;
}

// Get current timestamp as string
static inline void get_timestamp_string(char *buffer, size_t size) {
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    strftime(buffer, size, "%Y-%m-%d %H:%M:%S", tm_info);
}

// Get sentence count from content
static inline int get_sentence_count(const char *content) {
    if (content == NULL) return 0;
    
    int count = 0;
    for (const char *p = content; *p; p++) {
        if (is_sentence_delimiter(*p)) {
            count++;
        }
    }
    return count;
}

// Get word count from content
static inline int get_word_count(const char *content) {
    if (content == NULL || strlen(content) == 0) return 0;
    
    int count = 0;
    int in_word = 0;
    
    for (const char *p = content; *p; p++) {
        if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
            in_word = 0;
        } else {
            if (!in_word) {
                count++;
                in_word = 1;
            }
        }
    }
    
    return count;
}

// Split string into tokens
static inline int split_string(char *str, const char *delimiter, char **tokens, int max_tokens) {
    if (str == NULL || tokens == NULL) return 0;
    
    int count = 0;
    char *token = strtok(str, delimiter);
    
    while (token != NULL && count < max_tokens) {
        tokens[count++] = token;
        token = strtok(NULL, delimiter);
    }
    
    return count;
}

// Parse message into tokens
static inline int parse_message(const char *message, char **tokens, int max_tokens) {
    if (message == NULL || tokens == NULL) return 0;
    
    char buffer[LARGE_BUFFER_SIZE];
    strncpy(buffer, message, LARGE_BUFFER_SIZE - 1);
    buffer[LARGE_BUFFER_SIZE - 1] = '\0';
    
    return split_string(buffer, PROTOCOL_DELIMITER, tokens, max_tokens);
}

// Check if user is owner of file
static inline int is_owner(const FileMetadata *file, const char *username) {
    if (file == NULL || username == NULL) return 0;
    return strcmp(file->owner, username) == 0;
}

// Check if user has read access to file
static inline int has_read_access(const FileMetadata *file, const char *username) {
    if (file == NULL || username == NULL) return 0;
    
    // Owner always has access
    if (is_owner(file, username)) return 1;
    
    // Check access rights (simplified - you'll need proper implementation)
    return 0;  // Placeholder - implement based on your access control system
}

// Check if user has write access to file
static inline int has_write_access(const FileMetadata *file, const char *username) {
    if (file == NULL || username == NULL) return 0;
    
    // Owner always has write access
    if (is_owner(file, username)) return 1;
    
    // Check access rights (simplified - you'll need proper implementation)
    return 0;  // Placeholder - implement based on your access control system
}

// Sanitize filename by removing invalid characters
static inline void sanitize_filename(char *filename) {
    if (filename == NULL) return;
    
    const char *invalid_chars = "/\\:*?\"<>|";
    for (char *p = filename; *p; p++) {
        if (strchr(invalid_chars, *p)) {
            *p = '_';
        }
    }
}

// Parse timestamp string to time_t
static inline time_t parse_timestamp(const char *timestamp_str) {
    if (timestamp_str == NULL) return 0;
    
    struct tm tm_info;
    memset(&tm_info, 0, sizeof(struct tm));
    
    if (strptime(timestamp_str, "%Y-%m-%d %H:%M:%S", &tm_info) != NULL) {
        return mktime(&tm_info);
    }
    
    return 0;
}

// Log error with error code
static inline void log_error(const char *component, int error_code, const char *details) {
    char timestamp[64];
    get_timestamp_string(timestamp, sizeof(timestamp));
    
    fprintf(stderr, "[%s] [ERROR] [%s] Code %d: %s - %s\n", 
            timestamp, component, error_code, 
            get_error_message(error_code), 
            details ? details : "No additional details");
}


static inline const char* log_level_to_string(LogLevel level) {
    switch (level) {
        case LOG_LEVEL_INFO: return "INFO";
        case LOG_LEVEL_WARNING: return "WARN";
        case LOG_LEVEL_ERROR: return "ERROR";
        case LOG_LEVEL_DEBUG: return "DEBUG";
        case LOG_LEVEL_CRITICAL: return "CRITICAL";
        default: return "UNKNOWN";
    }
}

// Logging function
// Parameters:
//   - log_file: opened FILE* to write logs
//   - level: log level to categorize message
//   - ip: source IP string, can be NULL if not applicable
//   - port: source port, 0 if not applicable
//   - username: user's name, or NULL if not applicable
//   - fmt: printf-style format string for message body
//   - ...: format args
static inline void log_message(FILE *log_file, LogLevel level, 
                              const char *ip, int port, const char *username, 
                              const char *fmt, ...) {
    if (!log_file) return;

    char time_buffer[30];
    struct timeval tv;
    time_t curtime;

    gettimeofday(&tv, NULL);
    curtime = tv.tv_sec;

    strftime(time_buffer, sizeof(time_buffer), "%Y-%m-%d %H:%M:%S", localtime(&curtime));

    // Construct the log prefix with timestamp, level, ip, port, and username
    char prefix[256];
    if (ip && port > 0 && username && username[0] != '\0') {
        snprintf(prefix, sizeof(prefix), "[%s.%06ld] [%s] [IP:%s] [Port:%d] [User:%s]",
                 time_buffer, tv.tv_usec, log_level_to_string(level), ip, port, username);
    } else if (ip && port > 0) {
        snprintf(prefix, sizeof(prefix), "[%s.%06ld] [%s] [IP:%s] [Port:%d]",
                 time_buffer, tv.tv_usec, log_level_to_string(level), ip, port);
    } else if (username && username[0] != '\0') {
        snprintf(prefix, sizeof(prefix), "[%s.%06ld] [%s] [User:%s]",
                 time_buffer, tv.tv_usec, log_level_to_string(level), username);
    } else {
        snprintf(prefix, sizeof(prefix), "[%s.%06ld] [%s]",
                 time_buffer, tv.tv_usec, log_level_to_string(level));
    }

    va_list args;
    va_start(args, fmt);

    // Print to log file
    fprintf(log_file, "%s ", prefix);
    vfprintf(log_file, fmt, args);
    fprintf(log_file, "\n");
    fflush(log_file);

    va_end(args);
}


#endif // COMMON_H
