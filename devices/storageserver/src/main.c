#include "../include/storageserver.h"
#include <signal.h>

StorageServerConfig global_ctx;
FILE* log_file;

// Add to global context
int nm_socket = -1;
pthread_t nm_session_thread;

void signal_handler(int signum) {
    printf("\nReceived signal %d, shutting down...\n", signum);
    log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, "Received signal %d, initiating shutdown", signum);
    global_ctx.is_running = 0;
    if (global_ctx.client_socket > 0) {
        log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, "Closing client socket: %d", global_ctx.client_socket);
        close(global_ctx.client_socket);
    }
    if (nm_socket > 0) {
        log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, "Closing name server socket: %d", nm_socket);
        close(nm_socket);
    }
    log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, "Shutdown complete");
    exit(0);
}

void* maintain_nm_session(void *arg) {
    StorageServerConfig *ctx = (StorageServerConfig*)arg;
    char buffer[BUFFER_SIZE];
    
    log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, "Name server session maintenance thread started");

    while (ctx->is_running) {
        memset(buffer, 0, sizeof(buffer));
        ssize_t bytes = recv(nm_socket, buffer, sizeof(buffer) - 1, 0);

        if (bytes <= 0) {
            log_message(log_file, LOG_LEVEL_ERROR, NULL, 0, NULL, 
                       "Lost connection to name server (bytes=%zd, errno=%d)", bytes, errno);
            printf("Lost connection to Name Server\n");

            sleep(5);
            continue;
        }

        buffer[bytes] = '\0';
        char *nl = strchr(buffer, '\n');
        if (nl) *nl = '\0';

        log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
                   "Received from name server: %s", buffer);

        char *saveptr;
        char *cmd = strtok_r(buffer, "|", &saveptr);

        if (strcmp(cmd, "HEARTBEAT") == 0) {
            send(nm_socket, "HEARTBEAT_ACK\n", 14, 0);
            log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, "Sent heartbeat acknowledgment");
        } else {
            log_message(log_file, LOG_LEVEL_WARNING, NULL, 0, NULL, 
                       "Unknown command from name server: %s", cmd);
        }
    }

    log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, "Name server session maintenance thread stopping");
    return NULL;
}

// ============================================================================
// CLIENT THREAD ARGUMENT STRUCTURE
// ============================================================================
typedef struct {
    int client_fd;
    StorageServerConfig *ctx;
} ClientThreadArg;

// ============================================================================
// CLIENT HANDLER (with persistent connection for multiple commands)
// ============================================================================
void handle_client(int client_fd, StorageServerConfig *ctx) {
    char buffer[BUFFER_SIZE];
    
    log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
               "Client handler started for fd=%d, thread_id=%lu", client_fd, pthread_self());

    // Keep connection open for multiple commands
    while (ctx->is_running) {
        memset(buffer, 0, sizeof(buffer));
        ssize_t bytes = recv(client_fd, buffer, sizeof(buffer) - 1, 0);

        if (bytes <= 0) {
            log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
                       "Client disconnected (fd=%d, bytes=%zd)", client_fd, bytes);
            printf("Client disconnected\n");
            close(client_fd);
            return;
        }

        buffer[bytes] = '\0';

        // Remove newline
        char *newline = strchr(buffer, '\n');
        if (newline) *newline = '\0';

        log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
                   "Received command from fd=%d: %s", client_fd, buffer);

        // Check for QUIT command
        if (strcmp(buffer, "QUIT") == 0 || strcmp(buffer, "EXIT") == 0) {
            log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
                       "Client requested disconnect (fd=%d)", client_fd);
            send(client_fd, "SUCCESS|Goodbye\n", 16, 0);
            close(client_fd);
            return;
        }

        printf("Received: %s\n", buffer);

        // Parse command: CMD|arg1|arg2|...
        char *saveptr;
        char *cmd = strtok_r(buffer, "|", &saveptr);

        if (!cmd) {
            log_message(log_file, LOG_LEVEL_WARNING, NULL, 0, NULL, 
                       "Invalid command format (fd=%d)", client_fd);
            send(client_fd, "ERROR|Invalid command\n", 22, 0);
            continue;
        }

        // CREATE|filename|owner
        if (strcmp(cmd, "CREATE") == 0) {
            char *filename = strtok_r(NULL, "|", &saveptr);
            char *owner = strtok_r(NULL, "|", &saveptr);

            if (!filename || !owner) {
                log_message(log_file, LOG_LEVEL_WARNING, NULL, 0, NULL, 
                           "CREATE: Missing parameters (fd=%d)", client_fd);
                send(client_fd, "ERROR|Missing parameters\n", 25, 0);
            } else {
                log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
                           "CREATE request: filename='%s', owner='%s'", filename, owner);
                
                pthread_mutex_lock(&ctx->storage_lock);
                int result = ss_create_file(ctx->storage_dir, filename, owner);
                pthread_mutex_unlock(&ctx->storage_lock);

                if (result == ERR_SUCCESS) {
                    char response[256];
                    snprintf(response, sizeof(response), "SUCCESS|File '%s' created\n", filename);
                    send(client_fd, response, strlen(response), 0);
                    
                    log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
                               "File created successfully: %s (owner: %s)", filename, owner);
                    printf("Created file: %s (owner: %s)\n", filename, owner);

                    // Notify NM
                    if (nm_socket > 0) {
                        char notify[256];
                        snprintf(notify, sizeof(notify), "FILE_CREATED|%s\n", filename);
                        send(nm_socket, notify, strlen(notify), 0);
                        log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
                                   "Notified name server of file creation: %s", filename);
                    }
                } else {
                    char response[256];
                    snprintf(response, sizeof(response), "ERROR|%s\n", get_error_message(result));
                    send(client_fd, response, strlen(response), 0);
                    log_message(log_file, LOG_LEVEL_ERROR, NULL, 0, NULL, 
                               "CREATE failed: %s (error=%d)", filename, result);
                }
            }
        }

        // READ|filename
        else if (strcmp(cmd, "READ") == 0) {
            char *filename = strtok_r(NULL, "|", &saveptr);
        
            if (!filename) {
                log_message(log_file, LOG_LEVEL_WARNING, NULL, 0, NULL, 
                           "READ: Missing filename (fd=%d)", client_fd);
                send(client_fd, "ERROR|Missing filename\n", 23, 0);
            } else {
                log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
                           "READ request: filename='%s'", filename);
                
                pthread_mutex_lock(&ctx->storage_lock);
        
                // Load as FileContent to access sentences
                FileContent *file = load_file_content(ctx->storage_dir, filename);
        
                if (file) {
                    char response[LARGE_BUFFER_SIZE] = "SUCCESS|\n";
        
                    SentenceNode *current = file->head;
                    int sent_num = 0;
                    while (current) {
                        // Get sentence as string
                        char *sent_str = word_list_to_string(current->word_head, current->delimiter);
                        if (sent_str) {
                            char line[MAX_SENTENCE_LENGTH + 20];
                            // Format: [0] Hello world.
                            snprintf(line, sizeof(line), "[%d] %s\n", sent_num, sent_str);
                            strncat(response, line, LARGE_BUFFER_SIZE - strlen(response) - 1);
                            free(sent_str);
                        }
                        sent_num++;
                        current = current->next;
                    }
        
                    send(client_fd, response, strlen(response), 0);
                    send(client_fd, "STOP\n", 5, 0);
        
                    log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
                               "READ completed: %s (%d sentences)", filename, sent_num);
                    free_file_content(file);
                    printf("Read file: %s (%d sentences)\n", filename, sent_num);
                } else {
                    log_message(log_file, LOG_LEVEL_WARNING, NULL, 0, NULL, 
                               "READ failed: File not found '%s'", filename);
                    send(client_fd, "ERROR|File not found\n", 21, 0);
                }
        
                pthread_mutex_unlock(&ctx->storage_lock);
            }
        }

        else if (strcmp(cmd, "CLEANREAD") == 0) {
            char *filename = strtok_r(NULL, "|", &saveptr);
        
            if (!filename) {
                log_message(log_file, LOG_LEVEL_WARNING, NULL, 0, NULL, 
                           "CLEANREAD: Missing filename (fd=%d)", client_fd);
                send(client_fd, "ERROR|Missing filename\n", 23, 0);
            } else {
                log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
                           "CLEANREAD request: filename='%s'", filename);
                
                pthread_mutex_lock(&ctx->storage_lock);
        
                FileContent *file = load_file_content(ctx->storage_dir, filename);
        
                if (file) {
                    char response[LARGE_BUFFER_SIZE] = "SUCCESS|\n";
        
                    SentenceNode *current = file->head;
                    int sent_num = 0;
                    while (current) {
                        char *sent_str = word_list_to_string(current->word_head, current->delimiter);
                        if (sent_str) {
                            char line[MAX_SENTENCE_LENGTH + 20];
                            snprintf(line, sizeof(line), "%s\n", sent_str);
                            strncat(response, line, LARGE_BUFFER_SIZE - strlen(response) - 1);
                            free(sent_str);
                        }
                        sent_num++;
                        current = current->next;
                    }
        
                    send(client_fd, response, strlen(response), 0);
        
                    log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
                               "CLEANREAD completed: %s (%d sentences)", filename, sent_num);
                    free_file_content(file);
                    printf("Read file: %s (%d sentences)\n", filename, sent_num);
                } else {
                    log_message(log_file, LOG_LEVEL_WARNING, NULL, 0, NULL, 
                               "CLEANREAD failed: File not found '%s'", filename);
                    send(client_fd, "ERROR|File not found\n", 21, 0);
                }
        
                pthread_mutex_unlock(&ctx->storage_lock);
            }
        }

        // WRITE|filename|sentence_num|username
        else if (strcmp(cmd, "WRITE") == 0) {
            char *filename = strtok_r(NULL, "|", &saveptr);
            char *sentence_num_str = strtok_r(NULL, "|", &saveptr);
            char *username_ptr = strtok_r(NULL, "|", &saveptr);
        
            if (!filename || !sentence_num_str || !username_ptr) {
                log_message(log_file, LOG_LEVEL_WARNING, NULL, 0, NULL, 
                           "WRITE: Missing parameters (fd=%d)", client_fd);
                send(client_fd, "ERROR|Missing parameters\n", 25, 0);
                continue;
            }
        
            char username[MAX_USERNAME_LENGTH];
            strncpy(username, username_ptr, MAX_USERNAME_LENGTH - 1);
            username[MAX_USERNAME_LENGTH - 1] = '\0';
        
            char filename_copy[MAX_FILENAME_LENGTH];
            strncpy(filename_copy, filename, MAX_FILENAME_LENGTH - 1);
            filename_copy[MAX_FILENAME_LENGTH - 1] = '\0';
        
            int sentence_num = atoi(sentence_num_str);
        
            if (sentence_num < 0) {
                log_message(log_file, LOG_LEVEL_WARNING, NULL, 0, NULL, 
                           "WRITE: Invalid sentence number %d", sentence_num);
                send(client_fd, "ERROR|Sentence number must be >= 0\n", 36, 0);
                continue;
            }
        
            log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
                       "WRITE request: file='%s', sentence=%d, user='%s'", 
                       filename_copy, sentence_num, username);
            printf("WRITE request: file='%s', sentence=%d, user='%s'\n", 
                   filename_copy, sentence_num, username);
        
            // Try global lock
            if (!global_try_lock_sentence(ctx, filename_copy, sentence_num, username)) {
                log_message(log_file, LOG_LEVEL_WARNING, NULL, 0, NULL, 
                           "WRITE: Sentence already locked - file='%s', sentence=%d", 
                           filename_copy, sentence_num);
                send(client_fd, "ERROR|Sentence locked by another user\n", 39, 0);
                continue;
            }
        
            log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
                       "Lock acquired: file='%s', sentence=%d, user='%s'", 
                       filename_copy, sentence_num, username);
        
            // Load file into buffer
            pthread_mutex_lock(&ctx->storage_lock);
            FileContent *file_buffer = load_file_content(ctx->storage_dir, filename_copy);
            pthread_mutex_unlock(&ctx->storage_lock);
        
            if (!file_buffer) {
                log_message(log_file, LOG_LEVEL_ERROR, NULL, 0, NULL, 
                           "WRITE: File load failed '%s'", filename_copy);
                global_unlock_sentence(ctx, filename_copy, sentence_num, username);
                send(client_fd, "ERROR|File not found\n", 21, 0);
                continue;
            }

            // Validate sentence number
            // Allow: sentence_num == 0 for empty file
            // Allow: sentence_num < sentence_count for existing sentences
            // Allow: sentence_num == sentence_count ONLY if last sentence has delimiter
            int allow_append = 0;
            if (file_buffer->sentence_count > 0 && sentence_num == file_buffer->sentence_count) {
                // Check if last sentence ends with delimiter
                SentenceNode *last = file_buffer->head;
                while (last && last->next) last = last->next;
                if (last && is_sentence_delimiter(last->delimiter)) {
                    allow_append = 1;
                    log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL,
                                "Allowing append to new sentence %d (last sentence has delimiter '%c')",
                                sentence_num, last->delimiter);
                }
            }

            // Error conditions:
            // 1. Empty file and asking for sentence > 0
            // 2. Non-empty file and sentence_num > sentence_count
            // 3. sentence_num == sentence_count but last sentence has no delimiter
            if ((file_buffer->sentence_count == 0 && sentence_num > 0) ||
                (file_buffer->sentence_count > 0 && sentence_num > file_buffer->sentence_count) ||
                (file_buffer->sentence_count > 0 && sentence_num == file_buffer->sentence_count && !allow_append)) {

                log_message(log_file, LOG_LEVEL_WARNING, NULL, 0, NULL,
                            "Sentence validation failed: requested=%d, count=%d, allow_append=%d",
                            sentence_num, file_buffer->sentence_count, allow_append);

                global_unlock_sentence(ctx, filename_copy, sentence_num, username);
                free_file_content(file_buffer);

                char error_msg[128];
                snprintf(error_msg, sizeof(error_msg),
                        "ERROR|Sentence %d does not exist. File has %d sentence(s). %s\n",
                        sentence_num, file_buffer->sentence_count,
                        (file_buffer->sentence_count > 0 && sentence_num == file_buffer->sentence_count) ?
                        "Last sentence must end with delimiter (. ! ?) to create new sentence." : "");
                send(client_fd, error_msg, strlen(error_msg), 0);
                continue;
            }
            
            // Create first sentence node if file is empty and user requests sentence_num==0
            if (file_buffer->sentence_count == 0 && sentence_num == 0) {
                SentenceNode *node = malloc(sizeof(SentenceNode));
                node->word_head = NULL;
                node->word_tail = NULL;
                node->word_count = 0;
                node->delimiter = '\0';
                node->next = NULL;
                node->is_locked = 0;
                node->locked_by[0] = '\0';
                pthread_mutex_init(&node->sentence_lock, NULL);
                file_buffer->head = node;
                file_buffer->sentence_count = 1;
            }
            // Allow creation of new blank sentence at the end if previous ends with delimiter
            else if (allow_append && sentence_num == file_buffer->sentence_count) {
                SentenceNode *node = malloc(sizeof(SentenceNode));
                node->word_head = NULL;
                node->word_tail = NULL;
                node->word_count = 0;
                node->delimiter = '\0';
                node->next = NULL;
                node->is_locked = 0;
                node->locked_by[0] = '\0';
                pthread_mutex_init(&node->sentence_lock, NULL);
                // Append to list
                SentenceNode *last = file_buffer->head;
                while (last && last->next) last = last->next;
                if (last) last->next = node;
                file_buffer->sentence_count++;
            }
        
            // Ensure sentence exists
            lock_sentence(file_buffer, sentence_num, username);
        
            char response[128];
            snprintf(response, sizeof(response), 
                    "SUCCESS|Sentence %d locked for '%s'. Send word updates (word_index|content), then ETIRW\n", 
                    sentence_num, username);
            send(client_fd, response, strlen(response), 0);
            log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
                       "Write session started: sentence %d locked for user '%s'", 
                       sentence_num, username);
            printf("  Locked sentence %d for user '%s'\n", sentence_num, username);
        
            int current_sentence = sentence_num;
            int write_active = 1;
            int word_update_count = 0;
        
            while (write_active) {
                memset(buffer, 0, sizeof(buffer));
                bytes = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
        
                if (bytes <= 0) {
                    log_message(log_file, LOG_LEVEL_WARNING, NULL, 0, NULL, 
                               "Client disconnected during WRITE session (fd=%d)", client_fd);
                    printf("  Client disconnected during WRITE\n");
                    global_unlock_sentence(ctx, filename_copy, sentence_num, username);
                    free_file_content(file_buffer);
                    close(client_fd);
                    return;
                }
        
                buffer[bytes] = '\0';
                newline = strchr(buffer, '\n');
                if (newline) *newline = '\0';
                newline = strchr(buffer, '\r');
                if (newline) *newline = '\0';
        
                log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
                           "WRITE session received: '%s'", buffer);
                printf("  Received: '%s'\n", buffer);
        
                if (strcmp(buffer, "ETIRW") == 0) {
                    log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
                               "WRITE session completing: file='%s', updates=%d", 
                               filename_copy, word_update_count);
                    
                    // Save buffer to disk
                    pthread_mutex_lock(&ctx->storage_lock);
                    int save_result = save_file_content(ctx->storage_dir, file_buffer);
        
                    if (save_result == ERR_SUCCESS) {
                        log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
                                   "File saved successfully: %s", filename_copy);
                        
                        FileMetadata metadata;
                        if (load_metadata(ctx->storage_dir, filename_copy, &metadata) == ERR_SUCCESS) {
                            metadata.modified_time = time(NULL);
                            update_file_stats(ctx->storage_dir, &metadata);
                            save_metadata(ctx->storage_dir, &metadata);
                            log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
                                       "Metadata updated for: %s", filename_copy);
                        }
                    } else {
                        log_message(log_file, LOG_LEVEL_ERROR, NULL, 0, NULL, 
                                   "File save failed: %s (error=%d)", filename_copy, save_result);
                    }
                    pthread_mutex_unlock(&ctx->storage_lock);
        
                    free_file_content(file_buffer);
                    global_unlock_sentence(ctx, filename_copy, sentence_num, username);
        
                    send(client_fd, "SUCCESS|Write complete\n", 23, 0);
                    log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
                               "WRITE session completed successfully");
                    printf("  Write session completed\n");
        
                    if (nm_socket > 0) {
                        char notify[256];
                        snprintf(notify, sizeof(notify), "FILE_UPDATED|%s\n", filename_copy);
                        send(nm_socket, notify, strlen(notify), 0);
                        log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
                                   "Notified name server of file update: %s", filename_copy);
                    }
        
                    write_active = 0;
                }
                else {
                    // Parse word update
                    char buffer_copy[BUFFER_SIZE];
                    strncpy(buffer_copy, buffer, sizeof(buffer_copy) - 1);
                    buffer_copy[sizeof(buffer_copy) - 1] = '\0';
        
                    char *saveptr2;
                    char *word_index_str = strtok_r(buffer_copy, "|", &saveptr2);
                    char *content = strtok_r(NULL, "", &saveptr2);
        
                    if (!word_index_str || !content) {
                        log_message(log_file, LOG_LEVEL_WARNING, NULL, 0, NULL, 
                                   "WRITE: Invalid word update format");
                        send(client_fd, "ERROR|Invalid format. Use: word_index|content\n", 47, 0);
                        continue;
                    }
        
                    int word_index = atoi(word_index_str);
        
                    log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
                               "Word update: sentence=%d, word_index=%d, content='%s'", 
                               current_sentence, word_index, content);
                    printf("  Inserting at word %d: '%s' (sentence %d)\n", 
                           word_index, content, current_sentence);
        
                    int new_sentence_num = current_sentence;
                    int mod_result = modify_sentence_multiword(file_buffer, current_sentence, 
                                                              word_index, content, username, &new_sentence_num);
        
                    if (mod_result == ERR_SUCCESS) {
                        word_update_count++;
                        send(client_fd, "SUCCESS|Word updated\n", 21, 0);
                        log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
                                   "Word updated successfully (now in sentence %d)", new_sentence_num);
                        printf("  ✓ Updated (now in sentence %d)\n", new_sentence_num);
        
                        // Check if sentence changed (delimiter detected)
                        if (new_sentence_num > current_sentence) {
                            current_sentence = new_sentence_num;
                            log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
                                       "Auto-switched to sentence %d", current_sentence);
                            printf("  → Auto-switched to sentence %d\n", current_sentence);
        
                            char info_msg[128];
                            snprintf(info_msg, sizeof(info_msg), 
                                    "INFO|Sentence ended. Now editing sentence %d\n", current_sentence);
                            send(client_fd, info_msg, strlen(info_msg), 0);
                        }
                    } else {
                        char error[256];
                        snprintf(error, sizeof(error), "ERROR|%s\n", get_error_message(mod_result));
                        send(client_fd, error, strlen(error), 0);
                        log_message(log_file, LOG_LEVEL_ERROR, NULL, 0, NULL, 
                                   "Word update failed: %s", get_error_message(mod_result));
                        printf("  ✗ Failed: %s\n", get_error_message(mod_result));
                    }
                }
            }
        }

        // UNDO|filename
        else if (strcmp(cmd, "UNDO") == 0) {
            char *filename = strtok_r(NULL, "|", &saveptr);

            if (!filename) {
                log_message(log_file, LOG_LEVEL_WARNING, NULL, 0, NULL, 
                           "UNDO: Missing filename (fd=%d)", client_fd);
                send(client_fd, "ERROR|Missing filename\n", 23, 0);
            } else {
                log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
                           "UNDO request: filename='%s'", filename);
                
                pthread_mutex_lock(&ctx->storage_lock);

                char *file_path = get_file_path(ctx->storage_dir, filename);
                char backup_path[MAX_PATH_LENGTH];
                snprintf(backup_path, sizeof(backup_path), "%s.backup", file_path);

                if (access(backup_path, F_OK) != 0) {
                    log_message(log_file, LOG_LEVEL_WARNING, NULL, 0, NULL, 
                               "UNDO: No backup available for '%s'", filename);
                    free(file_path);
                    pthread_mutex_unlock(&ctx->storage_lock);
                    send(client_fd, "ERROR|No backup available\n", 27, 0);
                } else {
                    char cmd_buf[BUFFER_SIZE];
                    snprintf(cmd_buf, sizeof(cmd_buf), "cp %s %s", backup_path, file_path);
                    int sys_result = system(cmd_buf);

                    log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
                               "UNDO executed: file='%s', result=%d", filename, sys_result);
                    free(file_path);
                    pthread_mutex_unlock(&ctx->storage_lock);

                    send(client_fd, "SUCCESS|Undo successful\n", 24, 0);
                    printf("Undone changes for: %s\n", filename);
                }
            }
        }

        // DELETE|filename
        else if (strcmp(cmd, "DELETE") == 0) {
            char *filename = strtok_r(NULL, "|", &saveptr);

            if (!filename) {
                log_message(log_file, LOG_LEVEL_WARNING, NULL, 0, NULL, 
                           "DELETE: Missing filename (fd=%d)", client_fd);
                send(client_fd, "ERROR|Missing filename\n", 23, 0);
            } else {
                log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
                           "DELETE request: filename='%s'", filename);
                
                pthread_mutex_lock(&ctx->storage_lock);
                int result = ss_delete_file(ctx->storage_dir, filename);
                pthread_mutex_unlock(&ctx->storage_lock);

                if (result == ERR_SUCCESS) {
                    char response[256];
                    snprintf(response, sizeof(response), "SUCCESS|File '%s' deleted\n", filename);
                    send(client_fd, response, strlen(response), 0);
                    log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
                               "File deleted successfully: %s", filename);
                    printf("Deleted file: %s\n", filename);

                    if (nm_socket > 0) {
                        char notify[256];
                        snprintf(notify, sizeof(notify), "FILE_DELETED|%s\n", filename);
                        send(nm_socket, notify, strlen(notify), 0);
                        log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
                                   "Notified name server of file deletion: %s", filename);
                    }
                } else {
                    char response[256];
                    snprintf(response, sizeof(response), "ERROR|%s\n", get_error_message(result));
                    send(client_fd, response, strlen(response), 0);
                    log_message(log_file, LOG_LEVEL_ERROR, NULL, 0, NULL, 
                               "DELETE failed: %s (error=%d)", filename, result);
                }
            }
        }

        // LIST
        else if (strcmp(cmd, "LIST") == 0) {
            log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, "LIST request received");
            
            pthread_mutex_lock(&ctx->storage_lock);
            char files[MAX_FILES_PER_SS][MAX_FILENAME_LENGTH];
            int count = list_files(ctx->storage_dir, files, MAX_FILES_PER_SS);
            pthread_mutex_unlock(&ctx->storage_lock);

            char response[LARGE_BUFFER_SIZE] = "SUCCESS|Files:\n";
            for (int i = 0; i < count; i++) {
                strcat(response, files[i]);
                strcat(response, "\n");
            }
            send(client_fd, response, strlen(response), 0);
            log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
                       "LIST completed: %d files", count);
            printf("Listed %d files\n", count);
        }

        // INFO|filename
        else if (strcmp(cmd, "INFO") == 0) {
            char *filename = strtok_r(NULL, "|", &saveptr);

            if (!filename) {
                log_message(log_file, LOG_LEVEL_WARNING, NULL, 0, NULL, 
                           "INFO: Missing filename (fd=%d)", client_fd);
                send(client_fd, "ERROR|Missing filename\n", 23, 0);
            } else {
                log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
                           "INFO request: filename='%s'", filename);
                
                pthread_mutex_lock(&ctx->storage_lock);
                FileMetadata metadata;
                int result = load_metadata(ctx->storage_dir, filename, &metadata);
                pthread_mutex_unlock(&ctx->storage_lock);

                if (result == ERR_SUCCESS) {
                    char response[BUFFER_SIZE];
                    char created[64], modified[64], accessed[64];

                    strftime(created, sizeof(created), "%Y-%m-%d %H:%M:%S", localtime(&metadata.created_time));
                    strftime(modified, sizeof(modified), "%Y-%m-%d %H:%M:%S", localtime(&metadata.modified_time));
                    strftime(accessed, sizeof(accessed), "%Y-%m-%d %H:%M:%S", localtime(&metadata.accessed_time));

                    snprintf(response, sizeof(response),
                            "SUCCESS|\n"
                            "Filename: %s\n"
                            "Owner: %s\n"
                            "Size: %zu bytes\n"
                            "Words: %d\n"
                            "Characters: %d\n"
                            "Sentences: %d\n"
                            "Created: %s\n"
                            "Modified: %s\n"
                            "Accessed: %s\n",
                            metadata.filename, metadata.owner, metadata.size,
                            metadata.word_count, metadata.char_count, metadata.sentence_count,
                            created, modified, accessed);

                    send(client_fd, response, strlen(response), 0);
                    log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
                               "INFO completed: %s (size=%zu, words=%d)", 
                               filename, metadata.size, metadata.word_count);
                    printf("Info for file: %s\n", filename);
                } else {
                    char response[256];
                    snprintf(response, sizeof(response), "ERROR|%s\n", get_error_message(result));
                    send(client_fd, response, strlen(response), 0);
                    log_message(log_file, LOG_LEVEL_ERROR, NULL, 0, NULL, 
                               "INFO failed: %s (error=%d)", filename, result);
                }
            }
        }

        // STREAM|filename
        else if (strcmp(cmd, "STREAM") == 0) {
            char *filename = strtok_r(NULL, "|", &saveptr);

            if (!filename) {
                log_message(log_file, LOG_LEVEL_WARNING, NULL, 0, NULL, 
                           "STREAM: Missing filename (fd=%d)", client_fd);
                send(client_fd, "ERROR|Missing filename\n", 23, 0);
            } else {
                log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
                           "STREAM request: filename='%s'", filename);
                
                pthread_mutex_lock(&ctx->storage_lock);
                char content[LARGE_BUFFER_SIZE];
                int result = ss_read_file(ctx->storage_dir, filename, content, sizeof(content));
                pthread_mutex_unlock(&ctx->storage_lock);

                if (result != ERR_SUCCESS) {
                    char response[256];
                    snprintf(response, sizeof(response), "ERROR|%s\n", get_error_message(result));
                    send(client_fd, response, strlen(response), 0);
                    log_message(log_file, LOG_LEVEL_ERROR, NULL, 0, NULL, 
                               "STREAM failed: %s (error=%d)", filename, result);
                } else {
                    send(client_fd, "SUCCESS|Starting stream\n", 24, 0);
                    usleep(50000);

                    int word_count = 0;
                    char *token = strtok(content, " \t\n\r");
                    while (token) {
                        char word_msg[BUFFER_SIZE];
                        snprintf(word_msg, sizeof(word_msg), "WORD|%s\n", token);
                        send(client_fd, word_msg, strlen(word_msg), 0);
                        word_count++;

                        usleep(STREAM_DELAY_MS * 1000);

                        token = strtok(NULL, " \t\n\r");
                    }

                    send(client_fd, "STOP\n", 5, 0);
                    log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
                               "STREAM completed: %s (%d words)", filename, word_count);
                    printf("Streamed file: %s\n", filename);
                }
            }
        }

        else {
            log_message(log_file, LOG_LEVEL_WARNING, NULL, 0, NULL, 
                       "Unknown command: %s (fd=%d)", cmd, client_fd);
            send(client_fd, "ERROR|Unknown command\n", 22, 0);
        }
    }

    log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
               "Client handler ending (fd=%d)", client_fd);
    close(client_fd);
}

// ============================================================================
// CLIENT THREAD WRAPPER
// ============================================================================
void* client_thread_handler(void *arg) {
    ClientThreadArg *client_arg = (ClientThreadArg*)arg;
    log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
               "Client thread spawned: fd=%d, thread_id=%lu", 
               client_arg->client_fd, pthread_self());
    
    handle_client(client_arg->client_fd, client_arg->ctx);
    
    log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
               "Client thread terminating: thread_id=%lu", pthread_self());
    free(client_arg);
    return NULL;
}

// ============================================================================
// MAIN FUNCTION
// ============================================================================
int main(int argc, char *argv[]) {
    log_file = fopen(LOG_FILE, "w");
    
    if (!log_file) {
        fprintf(stderr, "Failed to open log file: %s\n", LOG_FILE);
        return 1;
    }

    log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, "Storage Server initializing");

    if (argc < 3) {
        fprintf(stderr, "Usage: %s <storage_dir> <client_port> [nm_ip] [nm_port]\n", argv[0]);
        fprintf(stderr, "Example: %s ./storage_data 8001 127.0.0.1 9000\n", argv[0]);
        log_message(log_file, LOG_LEVEL_ERROR, NULL, 0, NULL, 
                   "Insufficient arguments (argc=%d)", argc);
        return 1;
    }

    const char *storage_dir = argv[1];
    int client_port = atoi(argv[2]);

    log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
               "Configuration: storage_dir='%s', client_port=%d", storage_dir, client_port);

    // Initialize context
    memset(&global_ctx, 0, sizeof(StorageServerConfig));
    global_ctx.id = 1;
    strncpy(global_ctx.storage_dir, storage_dir, MAX_PATH_LENGTH - 1);
    global_ctx.client_port = client_port;
    global_ctx.is_running = 1;
    pthread_mutex_init(&global_ctx.storage_lock, NULL);

    global_ctx.global_locks = NULL;
    pthread_mutex_init(&global_ctx.lock_table_mutex, NULL);

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, "Signal handlers registered");

    if (create_storage_directory(storage_dir) != ERR_SUCCESS) {
        fprintf(stderr, "Failed to create storage directory\n");
        log_message(log_file, LOG_LEVEL_CRITICAL, NULL, 0, NULL, 
                   "Failed to create storage directory: %s", storage_dir);
        return 1;
    }

    log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
               "Storage directory verified: %s", storage_dir);

    printf("Storage Server Starting...\n");
    printf("Storage Directory: %s\n", storage_dir);
    printf("Client Port: %d\n", client_port);

    // Connect to Name Server if provided
    if (argc >= 5) {
        const char *nm_ip = argv[3];
        int nm_port_arg = atoi(argv[4]);

        log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
                   "Attempting to connect to name server: %s:%d", nm_ip, nm_port_arg);
        printf("Connecting to Name Server at %s:%d...\n", nm_ip, nm_port_arg);

        nm_socket = socket(AF_INET, SOCK_STREAM, 0);
        struct sockaddr_in nm_addr;
        memset(&nm_addr, 0, sizeof(nm_addr));
        nm_addr.sin_family = AF_INET;
        nm_addr.sin_port = htons(nm_port_arg);
        inet_pton(AF_INET, nm_ip, &nm_addr.sin_addr);

        if (connect(nm_socket, (struct sockaddr*)&nm_addr, sizeof(nm_addr)) == 0) {
            log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
                       "Connected to name server successfully");
            printf("✓ Connected to Name Server\n");

            // Scan existing files in storage directory
            char existing_files[MAX_FILES_PER_SS][MAX_FILENAME_LENGTH];
            int file_count = list_files(storage_dir, existing_files, MAX_FILES_PER_SS);

            log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
                       "Found %d existing files in storage", file_count);

            // Build file list for registration
            char file_list[BUFFER_SIZE] = "";
            for (int i = 0; i < file_count; i++) {
                if (i > 0) strcat(file_list, ",");
                strcat(file_list, existing_files[i]);
            }

            // Send REGISTER with existing files
            char reg_msg[BUFFER_SIZE];
            snprintf(reg_msg, sizeof(reg_msg), "REGISTER|127.0.0.1|%d|%d|%s\n", 
                    client_port, client_port, file_list);
            send(nm_socket, reg_msg, strlen(reg_msg), 0);

            log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
                       "Sent REGISTER message with %d files", file_count);

            if (file_count > 0) {
                printf("  → Registered %d existing files\n", file_count);
            }

            // Read response
            char response[BUFFER_SIZE];
            ssize_t bytes = recv(nm_socket, response, sizeof(response) - 1, 0);
            if (bytes > 0) {
                response[bytes] = '\0';
                log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
                           "Name server response: %s", response);
                printf("NM Response: %s", response);
            }

            pthread_create(&nm_session_thread, NULL, maintain_nm_session, &global_ctx);
            pthread_detach(nm_session_thread);
            log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
                       "Name server session thread started");
        } else {
            log_message(log_file, LOG_LEVEL_WARNING, NULL, 0, NULL, 
                       "Could not connect to name server (errno=%d), running standalone", errno);
            printf("⚠ Could not connect to Name Server (running standalone)\n");
            nm_socket = -1;
        }
    } else {
        log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
                   "No name server specified, running standalone");
    }

    // Create server socket
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        log_message(log_file, LOG_LEVEL_CRITICAL, NULL, 0, NULL, 
                   "Failed to create server socket (errno=%d)", errno);
        return 1;
    }

    log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
               "Server socket created: fd=%d", server_fd);

    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        log_message(log_file, LOG_LEVEL_ERROR, NULL, 0, NULL, 
                   "setsockopt failed (errno=%d)", errno);
        close(server_fd);
        return 1;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(client_port);

    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        log_message(log_file, LOG_LEVEL_CRITICAL, NULL, 0, NULL, 
                   "Failed to bind to port %d (errno=%d)", client_port, errno);
        close(server_fd);
        return 1;
    }

    log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
               "Socket bound successfully to port %d", client_port);

    if (listen(server_fd, 10) < 0) {
        perror("listen");
        log_message(log_file, LOG_LEVEL_CRITICAL, NULL, 0, NULL, 
                   "listen() failed (errno=%d)", errno);
        close(server_fd);
        return 1;
    }

    global_ctx.client_socket = server_fd;

    log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
               "Server listening on port %d, ready for connections", client_port);
    printf("Server listening on port %d\n", client_port);
    printf("Ready for connections (use Ctrl+C to stop)\n\n");

    // ACCEPT LOOP - CREATE THREAD FOR EACH CLIENT
    int client_count = 0;
    while (global_ctx.is_running) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);

        if (client_fd < 0) {
            if (global_ctx.is_running) {
                log_message(log_file, LOG_LEVEL_ERROR, NULL, 0, NULL, 
                           "accept() failed (errno=%d)", errno);
                perror("accept");
            }
            continue;
        }

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        
        client_count++;
        log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
                   "Client connection #%d accepted from %s:%d (fd=%d)", 
                   client_count, client_ip, ntohs(client_addr.sin_port), client_fd);
        printf("Client connected from %s:%d\n", client_ip, ntohs(client_addr.sin_port));

        // Create thread for this client
        ClientThreadArg *client_arg = malloc(sizeof(ClientThreadArg));
        if (!client_arg) {
            perror("malloc");
            log_message(log_file, LOG_LEVEL_ERROR, NULL, 0, NULL, 
                       "Failed to allocate client thread argument");
            close(client_fd);
            continue;
        }

        client_arg->client_fd = client_fd;
        client_arg->ctx = &global_ctx;

        pthread_t client_thread;
        if (pthread_create(&client_thread, NULL, client_thread_handler, client_arg) != 0) {
            perror("pthread_create");
            log_message(log_file, LOG_LEVEL_ERROR, NULL, 0, NULL, 
                       "Failed to create client thread (errno=%d)", errno);
            close(client_fd);
            free(client_arg);
            continue;
        }

        pthread_detach(client_thread);
    }

    log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
               "Server shutting down, cleaning up resources");
    pthread_mutex_destroy(&global_ctx.storage_lock);
    pthread_mutex_destroy(&global_ctx.lock_table_mutex);
    close(server_fd);
    
    log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
               "Storage Server stopped cleanly");
    fclose(log_file);
    printf("Server stopped\n");

    return 0;
}
