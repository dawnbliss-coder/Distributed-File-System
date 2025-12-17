#include "../include/nameserver.h"


void handle_session_command(ClientSession *session, NameServerConfig *config, 
                           const char *command) {
    log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
               "Processing command: user='%s', session_id=%d, command='%s'", 
               session->username, session->socket_fd, command);
    
    char cmd_copy[BUFFER_SIZE];
    strncpy(cmd_copy, command, sizeof(cmd_copy) - 1);
    cmd_copy[sizeof(cmd_copy) - 1] = '\0';
    
    char *saveptr;
    char *cmd = strtok_r(cmd_copy, "|", &saveptr);
    
    if (!cmd) {
        log_message(log_file, LOG_LEVEL_WARNING, NULL, 0, NULL, 
                   "Invalid command format: user='%s', command='%s'", 
                   session->username, command);
        send(session->socket_fd, "ERROR|Invalid command\n", 22, 0);
        return;
    }
    
    // ========================================================================
    // QUIT/EXIT - Disconnect
    // ========================================================================
    if (strcmp(cmd, "QUIT") == 0 || strcmp(cmd, "EXIT") == 0) {
        log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
                   "User disconnecting: user='%s', session_id=%d", 
                   session->username, session->socket_fd);
        send(session->socket_fd, "SUCCESS|Goodbye!\n", 17, 0);
        session->is_active = 0;
        return;
    }
    
    // ========================================================================
    // CREATE - Create new file
    // ========================================================================
    else if (strcmp(cmd, "CREATE") == 0) {
        char *filename = strtok_r(NULL, "|", &saveptr);
        
        if (!filename) {
            log_message(log_file, LOG_LEVEL_WARNING, NULL, 0, NULL, 
                       "CREATE: Missing filename - user='%s'", session->username);
            send(session->socket_fd, "ERROR|Missing filename\n", 23, 0);
            return;
        }
        
        log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
                   "CREATE request: user='%s', filename='%s'", 
                   session->username, filename);
        
        // Find available SS
        int ss_id = find_available_ss(config);
        if (ss_id < 0) {
            log_message(log_file, LOG_LEVEL_ERROR, NULL, 0, NULL, 
                       "CREATE failed: No storage server available for file '%s'", filename);
            send(session->socket_fd, "ERROR|No storage server available\n", 35, 0);
            return;
        }
        
        log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
                   "Selected SS#%d for file '%s'", ss_id, filename);
        
        // Get SS session
        SSSession *ss = find_ss_session(config, ss_id);
        if (!ss) {
            log_message(log_file, LOG_LEVEL_ERROR, NULL, 0, NULL, 
                       "CREATE failed: SS#%d session not found", ss_id);
            send(session->socket_fd, "ERROR|SS not available\n", 23, 0);
            return;
        }
        
        log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
                   "Connecting to SS#%d: %s:%d", ss_id, ss->ip, ss->client_port);
        printf("    → Forwarding CREATE to SS#%d\n", ss_id);
        
        // Connect to SS
        int ss_socket = socket(AF_INET, SOCK_STREAM, 0);
        struct sockaddr_in ss_addr;
        memset(&ss_addr, 0, sizeof(ss_addr));
        ss_addr.sin_family = AF_INET;
        ss_addr.sin_port = htons(ss->client_port);
        inet_pton(AF_INET, ss->ip, &ss_addr.sin_addr);
        
        if (connect(ss_socket, (struct sockaddr*)&ss_addr, sizeof(ss_addr)) < 0) {
            log_message(log_file, LOG_LEVEL_ERROR, NULL, 0, NULL, 
                       "CREATE: Failed to connect to SS#%d at %s:%d (errno=%d: %s)", 
                       ss_id, ss->ip, ss->client_port, errno, strerror(errno));
            send(session->socket_fd, "ERROR|Failed to connect to SS\n", 31, 0);
            close(ss_socket);
            return;
        }
        
        log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
                   "Connected to SS#%d, sending CREATE command", ss_id);
        
        // Forward CREATE
        char ss_cmd[BUFFER_SIZE];
        snprintf(ss_cmd, sizeof(ss_cmd), "CREATE|%s|%s\n", filename, session->username);
        send(ss_socket, ss_cmd, strlen(ss_cmd), 0);
        
        // Get response
        char ss_response[BUFFER_SIZE];
        ssize_t bytes = recv(ss_socket, ss_response, sizeof(ss_response) - 1, 0);
        close(ss_socket);
        
        if (bytes > 0) {
            ss_response[bytes] = '\0';
            
            log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
                       "SS#%d response: %s", ss_id, ss_response);
            
            if (strncmp(ss_response, "SUCCESS", 7) == 0) {
                // Add to hash table and ACL (no backup)
                add_file_mapping(&config->file_table, filename, ss_id);
                add_file_access(&config->acl_manager, filename, session->username);
                
                log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
                           "File created successfully: filename='%s', owner='%s', ss_id=%d", 
                           filename, session->username, ss_id);
                
                send(session->socket_fd, "SUCCESS|File created successfully!\n", 36, 0);
                printf("    ✓ File '%s' created on SS#%d\n", filename, ss_id);
            } else {
                log_message(log_file, LOG_LEVEL_WARNING, NULL, 0, NULL, 
                           "CREATE failed on SS#%d: %s", ss_id, ss_response);
                send(session->socket_fd, ss_response, bytes, 0);
            }
        } else {
            log_message(log_file, LOG_LEVEL_ERROR, NULL, 0, NULL, 
                       "CREATE: No response from SS#%d (bytes=%zd)", ss_id, bytes);
            send(session->socket_fd, "ERROR|No response from SS\n", 27, 0);
        }
    }
    
    // ========================================================================
    // VIEW - List files
    // ========================================================================
    else if (strcmp(cmd, "VIEW") == 0) {
        char *flags = strtok_r(NULL, "|", &saveptr);
        
        log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
                   "VIEW request: user='%s', flags='%s'", 
                   session->username, flags ? flags : "(none)");
        
        char response[LARGE_BUFFER_SIZE] = "SUCCESS|\n";
        int file_count = 0;
        int accessible_count = 0;
        
        pthread_mutex_lock(&config->file_table.lock);
        for (int i = 0; i < HASH_TABLE_SIZE; i++) {
            FileMapping *current = config->file_table.buckets[i];
            while (current) {
                file_count++;
                
                // Check access (if not -a flag)
                if (!flags || !strstr(flags, "a")) {
                    // Only show files user has access to
                    if (!check_access(&config->acl_manager, current->filename, 
                                     session->username, ACCESS_READ)) {
                        current = current->next;
                        continue;
                    }
                }
                
                accessible_count++;
                strcat(response, "--> ");
                strcat(response, current->filename);
                strcat(response, "\n");
                current = current->next;
            }
        }
        pthread_mutex_unlock(&config->file_table.lock);
        
        log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
                   "VIEW completed: user='%s', total_files=%d, accessible=%d", 
                   session->username, file_count, accessible_count);
        
        send(session->socket_fd, response, strlen(response), 0);
    }
    
    // ========================================================================
    // READ - Redirect to SS
    // ========================================================================
    else if (strcmp(cmd, "READ") == 0) {
        char *filename = strtok_r(NULL, "|", &saveptr);
        
        if (!filename) {
            log_message(log_file, LOG_LEVEL_WARNING, NULL, 0, NULL, 
                       "READ: Missing filename - user='%s'", session->username);
            send(session->socket_fd, "ERROR|Missing filename\n", 23, 0);
            return;
        }
        
        log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
                   "READ request: user='%s', filename='%s'", 
                   session->username, filename);
        
        // Check access
        if (!check_access(&config->acl_manager, filename, session->username, ACCESS_READ)) {
            log_message(log_file, LOG_LEVEL_WARNING, NULL, 0, NULL, 
                       "READ denied: user='%s' lacks access to file='%s'", 
                       session->username, filename);
            send(session->socket_fd, "ERROR|Access denied\n", 20, 0);
            return;
        }
        
        // Get SS info
        int ss_id = get_file_primary_ss(&config->file_table, filename);
        if (ss_id < 0) {
            log_message(log_file, LOG_LEVEL_WARNING, NULL, 0, NULL, 
                       "READ: File not found '%s'", filename);
            send(session->socket_fd, "ERROR|File not found\n", 21, 0);
            return;
        }
        
        SSSession *ss = find_ss_session(config, ss_id);
        if (!ss) {
            log_message(log_file, LOG_LEVEL_ERROR, NULL, 0, NULL, 
                       "READ: SS#%d not available for file '%s'", ss_id, filename);
            send(session->socket_fd, "ERROR|SS not available\n", 23, 0);
            return;
        }
        
        // Return SS connection info for DIRECT connection
        char response[BUFFER_SIZE];
        snprintf(response, sizeof(response), "REDIRECT|%s|%d\n", ss->ip, ss->client_port);
        send(session->socket_fd, response, strlen(response), 0);
        
        log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
                   "READ redirect: user='%s', file='%s' -> SS#%d (%s:%d)", 
                   session->username, filename, ss_id, ss->ip, ss->client_port);
        
        printf("    → Redirecting READ to SS#%d (%s:%d)\n", ss_id, ss->ip, ss->client_port);
    }
    
    // ========================================================================
    // WRITE - Redirect to SS
    // ========================================================================
    else if (strcmp(cmd, "WRITE") == 0) {
        char *filename = strtok_r(NULL, "|", &saveptr);
        
        if (!filename) {
            log_message(log_file, LOG_LEVEL_WARNING, NULL, 0, NULL, 
                       "WRITE: Missing filename - user='%s'", session->username);
            send(session->socket_fd, "ERROR|Missing filename\n", 23, 0);
            return;
        }
        
        log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
                   "WRITE request: user='%s', filename='%s'", 
                   session->username, filename);
        
        // Check write access
        if (!check_access(&config->acl_manager, filename, session->username, ACCESS_WRITE)) {
            log_message(log_file, LOG_LEVEL_WARNING, NULL, 0, NULL, 
                       "WRITE denied: user='%s' lacks write access to file='%s'", 
                       session->username, filename);
            send(session->socket_fd, "ERROR|Access denied\n", 20, 0);
            return;
        }
        
        // Get SS info
        int ss_id = get_file_primary_ss(&config->file_table, filename);
        if (ss_id < 0) {
            log_message(log_file, LOG_LEVEL_WARNING, NULL, 0, NULL, 
                       "WRITE: File not found '%s'", filename);
            send(session->socket_fd, "ERROR|File not found\n", 21, 0);
            return;
        }
        
        SSSession *ss = find_ss_session(config, ss_id);
        if (!ss) {
            log_message(log_file, LOG_LEVEL_ERROR, NULL, 0, NULL, 
                       "WRITE: SS#%d not available for file '%s'", ss_id, filename);
            send(session->socket_fd, "ERROR|SS not available\n", 23, 0);
            return;
        }
        
        // Return SS connection info
        char response[BUFFER_SIZE];
        snprintf(response, sizeof(response), "REDIRECT|%s|%d\n", ss->ip, ss->client_port);
        send(session->socket_fd, response, strlen(response), 0);
        
        log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
                   "WRITE redirect: user='%s', file='%s' -> SS#%d (%s:%d)", 
                   session->username, filename, ss_id, ss->ip, ss->client_port);
        
        printf("    → Redirecting WRITE to SS#%d\n", ss_id);
    }
    
    // ========================================================================
    // DELETE - Forward to SS
    // ========================================================================
    else if (strcmp(cmd, "DELETE") == 0) {
        char *filename = strtok_r(NULL, "|", &saveptr);
        
        if (!filename) {
            log_message(log_file, LOG_LEVEL_WARNING, NULL, 0, NULL, 
                       "DELETE: Missing filename - user='%s'", session->username);
            send(session->socket_fd, "ERROR|Missing filename\n", 23, 0);
            return;
        }
        
        log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
                   "DELETE request: user='%s', filename='%s'", 
                   session->username, filename);
        
        // Check if owner
        FileAccessControl *acl = get_file_acl(&config->acl_manager, filename);
        if (!acl || strcmp(acl->owner, session->username) != 0) {
            log_message(log_file, LOG_LEVEL_WARNING, NULL, 0, NULL, 
                       "DELETE denied: user='%s' is not owner of file='%s' (owner='%s')", 
                       session->username, filename, acl ? acl->owner : "(no ACL)");
            send(session->socket_fd, "ERROR|Only owner can delete\n", 29, 0);
            return;
        }
        
        // Get SS
        int ss_id = get_file_primary_ss(&config->file_table, filename);
        if (ss_id < 0) {
            log_message(log_file, LOG_LEVEL_WARNING, NULL, 0, NULL, 
                       "DELETE: File not found '%s'", filename);
            send(session->socket_fd, "ERROR|File not found\n", 21, 0);
            return;
        }
        
        SSSession *ss = find_ss_session(config, ss_id);
        if (!ss) {
            log_message(log_file, LOG_LEVEL_ERROR, NULL, 0, NULL, 
                       "DELETE: SS#%d not available", ss_id);
            send(session->socket_fd, "ERROR|SS not available\n", 23, 0);
            return;
        }
        
        log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
                   "Forwarding DELETE to SS#%d: file='%s'", ss_id, filename);
        printf("    → Forwarding DELETE to SS#%d\n", ss_id);
        
        // Connect and delete
        int ss_socket = socket(AF_INET, SOCK_STREAM, 0);
        struct sockaddr_in ss_addr;
        memset(&ss_addr, 0, sizeof(ss_addr));
        ss_addr.sin_family = AF_INET;
        ss_addr.sin_port = htons(ss->client_port);
        inet_pton(AF_INET, ss->ip, &ss_addr.sin_addr);
        
        if (connect(ss_socket, (struct sockaddr*)&ss_addr, sizeof(ss_addr)) < 0) {
            log_message(log_file, LOG_LEVEL_ERROR, NULL, 0, NULL, 
                       "DELETE: Failed to connect to SS#%d (errno=%d: %s)", 
                       ss_id, errno, strerror(errno));
            send(session->socket_fd, "ERROR|Failed to connect to SS\n", 31, 0);
            return;
        }
        
        char ss_cmd[BUFFER_SIZE];
        snprintf(ss_cmd, sizeof(ss_cmd), "DELETE|%s\n", filename);
        send(ss_socket, ss_cmd, strlen(ss_cmd), 0);
        
        char ss_response[BUFFER_SIZE];
        ssize_t bytes = recv(ss_socket, ss_response, sizeof(ss_response) - 1, 0);
        close(ss_socket);
        
        if (bytes > 0) {
            ss_response[bytes] = '\0';
            
            if (strncmp(ss_response, "SUCCESS", 7) == 0) {
                // Remove from hash table
                remove_file_mapping(&config->file_table, filename);
                
                log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
                           "File deleted successfully: filename='%s', owner='%s', ss_id=%d", 
                           filename, session->username, ss_id);
                
                send(session->socket_fd, "SUCCESS|File deleted successfully!\n", 36, 0);
                printf("    ✓ File '%s' deleted from SS#%d\n", filename, ss_id);
            } else {
                log_message(log_file, LOG_LEVEL_WARNING, NULL, 0, NULL, 
                           "DELETE failed on SS#%d: %s", ss_id, ss_response);
                send(session->socket_fd, ss_response, bytes, 0);
            }
        } else {
            log_message(log_file, LOG_LEVEL_ERROR, NULL, 0, NULL, 
                       "DELETE: No response from SS#%d", ss_id);
            send(session->socket_fd, "ERROR|No response from SS\n", 27, 0);
        }
    }
    
    // ========================================================================
    // INFO - Fetch and augment with ACL
    // ========================================================================
    else if (strcmp(cmd, "INFO") == 0) {
        char *filename = strtok_r(NULL, "|", &saveptr);

        if (!filename) {
            log_message(log_file, LOG_LEVEL_WARNING, NULL, 0, NULL, 
                       "INFO: Missing filename - user='%s'", session->username);
            send(session->socket_fd, "ERROR|Missing filename\n", 23, 0);
            return;
        }

        log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
                   "INFO request: user='%s', filename='%s'", 
                   session->username, filename);

        // Get SS info
        int ss_id = get_file_primary_ss(&config->file_table, filename);
        if (ss_id < 0) {
            log_message(log_file, LOG_LEVEL_WARNING, NULL, 0, NULL, 
                       "INFO: File not found '%s'", filename);
            send(session->socket_fd, "ERROR|File not found\n", 21, 0);
            return;
        }

        SSSession *ss = find_ss_session(config, ss_id);
        if (!ss) {
            log_message(log_file, LOG_LEVEL_ERROR, NULL, 0, NULL, 
                       "INFO: SS#%d not available", ss_id);
            send(session->socket_fd, "ERROR|SS not available\n", 23, 0);
            return;
        }

        log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
                   "Fetching INFO from SS#%d for file '%s'", ss_id, filename);

        // Connect to SS, send INFO request
        int ss_socket = socket(AF_INET, SOCK_STREAM, 0);
        struct sockaddr_in ss_addr;
        memset(&ss_addr, 0, sizeof(ss_addr));
        ss_addr.sin_family = AF_INET;
        ss_addr.sin_port = htons(ss->client_port);
        inet_pton(AF_INET, ss->ip, &ss_addr.sin_addr);

        if (connect(ss_socket, (struct sockaddr*)&ss_addr, sizeof(ss_addr)) < 0) {
            log_message(log_file, LOG_LEVEL_ERROR, NULL, 0, NULL, 
                       "INFO: Failed to connect to SS#%d (errno=%d: %s)", 
                       ss_id, errno, strerror(errno));
            send(session->socket_fd, "ERROR|Failed to connect to SS\n", 31, 0);
            close(ss_socket);
            return;
        }

        char ss_cmd[BUFFER_SIZE];
        snprintf(ss_cmd, sizeof(ss_cmd), "INFO|%s\n", filename);
        send(ss_socket, ss_cmd, strlen(ss_cmd), 0);

        char ss_response[LARGE_BUFFER_SIZE];
        ssize_t bytes = recv(ss_socket, ss_response, sizeof(ss_response) - 1, 0);
        close(ss_socket);

        if (bytes <= 0) {
            log_message(log_file, LOG_LEVEL_ERROR, NULL, 0, NULL, 
                       "INFO: No response from SS#%d (bytes=%zd)", ss_id, bytes);
            send(session->socket_fd, "ERROR|Failed to get info\n", 25, 0);
            return;
        }
        ss_response[bytes] = '\0';

        log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
                   "Received INFO from SS#%d, augmenting with ACL", ss_id);

        // Compose final response
        char response[LARGE_BUFFER_SIZE];
        snprintf(response, sizeof(response), "%s\n", ss_response);

        // Attach access rights
        FileAccessControl *acl = get_file_acl(&config->acl_manager, filename);
        if (!acl) {
            log_message(log_file, LOG_LEVEL_WARNING, NULL, 0, NULL, 
                       "No ACL found for file '%s'", filename);
            strcat(response, "ACCESS|No ACL entry for this file\n");
        } else {
            int reader_count = 0, writer_count = 0;
            
            strcat(response, "ACCESS|\n");
            strcat(response, "  Owner(RW): ");
            strcat(response, acl->owner);
            strcat(response, "\n");

            // Readers
            strcat(response, "  Readers(R): ");
            int has_reader = 0;
            for (int i = 0; i < acl->user_count; i++) {
                if (acl->access_levels[i] == ACCESS_READ || 
                    acl->access_levels[i] == ACCESS_READ_WRITE || 
                    acl->access_levels[i] == ACCESS_WRITE) {
                    if (has_reader) strcat(response, ",");
                    strcat(response, acl->users[i]);
                    has_reader = 1;
                    reader_count++;
                }
            }
            if (!has_reader) strcat(response, "(none)");
            strcat(response, "\n");

            // Writers
            strcat(response, "  Writers(W): ");
            int has_writer = 0;
            for (int i = 0; i < acl->user_count; i++) {
                if (acl->access_levels[i] == ACCESS_WRITE || 
                    acl->access_levels[i] == ACCESS_READ_WRITE) {
                    if (has_writer) strcat(response, ",");
                    strcat(response, acl->users[i]);
                    has_writer = 1;
                    writer_count++;
                }
            }
            if (!has_writer) strcat(response, "(none)");
            strcat(response, "\n");
            
            log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
                       "ACL info: file='%s', owner='%s', readers=%d, writers=%d", 
                       filename, acl->owner, reader_count, writer_count);
        }

        send(session->socket_fd, response, strlen(response), 0);
        
        log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
                   "INFO completed: user='%s', file='%s', ss_id=%d", 
                   session->username, filename, ss_id);
    }

    // ========================================================================
    // STREAM - Redirect to SS
    // ========================================================================
    else if (strcmp(cmd, "STREAM") == 0) {
        char *filename = strtok_r(NULL, "|", &saveptr);
        
        if (!filename) {
            log_message(log_file, LOG_LEVEL_WARNING, NULL, 0, NULL, 
                       "STREAM: Missing filename - user='%s'", session->username);
            send(session->socket_fd, "ERROR|Missing filename\n", 23, 0);
            return;
        }
        
        log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
                   "STREAM request: user='%s', filename='%s'", 
                   session->username, filename);
        
        // Check access
        if (!check_access(&config->acl_manager, filename, session->username, ACCESS_READ)) {
            log_message(log_file, LOG_LEVEL_WARNING, NULL, 0, NULL, 
                       "STREAM denied: user='%s' lacks access to file='%s'", 
                       session->username, filename);
            send(session->socket_fd, "ERROR|Access denied\n", 20, 0);
            return;
        }
        
        // Get SS info
        int ss_id = get_file_primary_ss(&config->file_table, filename);
        if (ss_id < 0) {
            log_message(log_file, LOG_LEVEL_WARNING, NULL, 0, NULL, 
                       "STREAM: File not found '%s'", filename);
            send(session->socket_fd, "ERROR|File not found\n", 21, 0);
            return;
        }
        
        SSSession *ss = find_ss_session(config, ss_id);
        if (!ss) {
            log_message(log_file, LOG_LEVEL_ERROR, NULL, 0, NULL, 
                       "STREAM: SS#%d not available", ss_id);
            send(session->socket_fd, "ERROR|SS not available\n", 23, 0);
            return;
        }
        
        // Return SS connection info
        char response[BUFFER_SIZE];
        snprintf(response, sizeof(response), "REDIRECT|%s|%d\n", ss->ip, ss->client_port);
        send(session->socket_fd, response, strlen(response), 0);
        
        log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
                   "STREAM redirect: user='%s', file='%s' -> SS#%d", 
                   session->username, filename, ss_id);
    }
    
    // ========================================================================
    // UNDO - Redirect to SS
    // ========================================================================
    else if (strcmp(cmd, "UNDO") == 0) {
        char *filename = strtok_r(NULL, "|", &saveptr);
        
        if (!filename) {
            log_message(log_file, LOG_LEVEL_WARNING, NULL, 0, NULL, 
                       "UNDO: Missing filename - user='%s'", session->username);
            send(session->socket_fd, "ERROR|Missing filename\n", 23, 0);
            return;
        }
        
        log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
                   "UNDO request: user='%s', filename='%s'", 
                   session->username, filename);
        
        // Check write access
        if (!check_access(&config->acl_manager, filename, session->username, ACCESS_WRITE)) {
            log_message(log_file, LOG_LEVEL_WARNING, NULL, 0, NULL, 
                       "UNDO denied: user='%s' lacks write access to file='%s'", 
                       session->username, filename);
            send(session->socket_fd, "ERROR|Access denied\n", 20, 0);
            return;
        }
        
        // Get SS info
        int ss_id = get_file_primary_ss(&config->file_table, filename);
        if (ss_id < 0) {
            log_message(log_file, LOG_LEVEL_WARNING, NULL, 0, NULL, 
                       "UNDO: File not found '%s'", filename);
            send(session->socket_fd, "ERROR|File not found\n", 21, 0);
            return;
        }
        
        SSSession *ss = find_ss_session(config, ss_id);
        if (!ss) {
            log_message(log_file, LOG_LEVEL_ERROR, NULL, 0, NULL, 
                       "UNDO: SS#%d not available", ss_id);
            send(session->socket_fd, "ERROR|SS not available\n", 23, 0);
            return;
        }
        
        // Return SS connection info
        char response[BUFFER_SIZE];
        snprintf(response, sizeof(response), "REDIRECT|%s|%d\n", ss->ip, ss->client_port);
        send(session->socket_fd, response, strlen(response), 0);
        
        log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
                   "UNDO redirect: user='%s', file='%s' -> SS#%d", 
                   session->username, filename, ss_id);
        
        printf("    → Redirecting UNDO to SS#%d\n", ss_id);
    }
    
    // ========================================================================
    // EXEC - Fetch from SS, execute on NM
    // ========================================================================
    else if (strcmp(cmd, "EXEC") == 0) {
        char *filename = strtok_r(NULL, "|", &saveptr);
        
        if (!filename) {
            log_message(log_file, LOG_LEVEL_WARNING, NULL, 0, NULL, 
                       "EXEC: Missing filename - user='%s'", session->username);
            send(session->socket_fd, "ERROR|Missing filename\n", 23, 0);
            return;
        }
        
        log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
                   "EXEC request: user='%s', filename='%s'", 
                   session->username, filename);
        
        // Check read access
        if (!check_access(&config->acl_manager, filename, session->username, ACCESS_READ)) {
            log_message(log_file, LOG_LEVEL_WARNING, NULL, 0, NULL, 
                       "EXEC denied: user='%s' lacks access to file='%s'", 
                       session->username, filename);
            send(session->socket_fd, "ERROR|Access denied\n", 20, 0);
            return;
        }
        
        // Get SS info
        int ss_id = get_file_primary_ss(&config->file_table, filename);
        if (ss_id < 0) {
            log_message(log_file, LOG_LEVEL_WARNING, NULL, 0, NULL, 
                       "EXEC: File not found '%s'", filename);
            send(session->socket_fd, "ERROR|File not found\n", 21, 0);
            return;
        }
        
        SSSession *ss = find_ss_session(config, ss_id);
        if (!ss) {
            log_message(log_file, LOG_LEVEL_ERROR, NULL, 0, NULL, 
                       "EXEC: SS#%d not available", ss_id);
            send(session->socket_fd, "ERROR|SS not available\n", 23, 0);
            return;
        }
        
        log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
                   "Fetching content from SS#%d for EXEC", ss_id);
        printf("    → Fetching content from SS#%d for EXEC\n", ss_id);
        
        // Connect to SS to get file content
        int ss_socket = socket(AF_INET, SOCK_STREAM, 0);
        struct sockaddr_in ss_addr;
        memset(&ss_addr, 0, sizeof(ss_addr));
        ss_addr.sin_family = AF_INET;
        ss_addr.sin_port = htons(ss->client_port);
        inet_pton(AF_INET, ss->ip, &ss_addr.sin_addr);
        
        if (connect(ss_socket, (struct sockaddr*)&ss_addr, sizeof(ss_addr)) < 0) {
            log_message(log_file, LOG_LEVEL_ERROR, NULL, 0, NULL, 
                       "EXEC: Failed to connect to SS#%d (errno=%d: %s)", 
                       ss_id, errno, strerror(errno));
            send(session->socket_fd, "ERROR|Failed to connect to SS\n", 31, 0);
            close(ss_socket);
            return;
        }
        
        // Request file content
        char ss_cmd[BUFFER_SIZE];
        snprintf(ss_cmd, sizeof(ss_cmd), "CLEANREAD|%s\n", filename);
        send(ss_socket, ss_cmd, strlen(ss_cmd), 0);
        
        // Get response
        char ss_response[LARGE_BUFFER_SIZE];
        ssize_t bytes = recv(ss_socket, ss_response, sizeof(ss_response) - 1, 0);
        close(ss_socket);
        
        if (bytes <= 0 || strncmp(ss_response, "SUCCESS|", 8) != 0) {
            log_message(log_file, LOG_LEVEL_ERROR, NULL, 0, NULL, 
                       "EXEC: Failed to read file from SS#%d (bytes=%zd)", ss_id, bytes);
            send(session->socket_fd, "ERROR|Failed to read file\n", 27, 0);
            return;
        }
        
        // Extract content (skip "SUCCESS|")
        char *content = ss_response + 8;
        
        log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
                   "Executing command: user='%s', file='%s', command='%s'", 
                   session->username, filename, content);
        printf("    → Executing commands from '%s' command: %s\n", filename, content);
        
        // Execute on NM
        char result[LARGE_BUFFER_SIZE] = "SUCCESS|\n";
        FILE *fp = popen(content, "r");
        if (!fp) {
            log_message(log_file, LOG_LEVEL_ERROR, NULL, 0, NULL, 
                       "EXEC: popen failed (errno=%d: %s)", errno, strerror(errno));
            send(session->socket_fd, "ERROR|Execution failed\n", 24, 0);
            return;
        }
        
        char line[BUFFER_SIZE];
        int output_lines = 0;
        while (fgets(line, sizeof(line), fp)) {
            strncat(result, line, sizeof(result) - strlen(result) - 1);
            output_lines++;
        }
        
        int exit_code = pclose(fp);
        
        log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
                   "EXEC completed: user='%s', file='%s', exit_code=%d, output_lines=%d", 
                   session->username, filename, exit_code, output_lines);
        
        if (exit_code == 0) {
            send(session->socket_fd, result, strlen(result), 0);
        } else {
            char error[BUFFER_SIZE];
            snprintf(error, sizeof(error), "ERROR|Command failed with exit code %d\n%s", 
                    exit_code, result + 8);
            log_message(log_file, LOG_LEVEL_WARNING, NULL, 0, NULL, 
                       "EXEC failed: exit_code=%d", exit_code);
            send(session->socket_fd, error, strlen(error), 0);
        }
    }
    
    // ========================================================================
    // LIST - Show connected users
    // ========================================================================
    else if (strcmp(cmd, "LIST") == 0) {
        log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
                   "LIST request: user='%s'", session->username);
        
        char response[LARGE_BUFFER_SIZE] = "SUCCESS|Users:\n";
        
        pthread_mutex_lock(&config->client_session_lock);
        
        ClientSession *current = config->client_sessions;
        int user_count = 0;
        
        while (current) {
            if (current->is_active) {
                strcat(response, "--> ");
                strcat(response, current->username);
                strcat(response, "\n");
                user_count++;
            }
            current = current->next;
        }
        
        pthread_mutex_unlock(&config->client_session_lock);
        
        if (user_count == 0) {
            strcat(response, "(No users connected)\n");
        }
        
        log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
                   "LIST completed: active_users=%d", user_count);
        
        send(session->socket_fd, response, strlen(response), 0);
    }    
    
    // ========================================================================
    // ADDACCESS - Grant access
    // ========================================================================
    else if (strcmp(cmd, "ADDACCESS") == 0) {
        char *access_type = strtok_r(NULL, "|", &saveptr);
        char *filename = strtok_r(NULL, "|", &saveptr);
        char *target_user = strtok_r(NULL, "|", &saveptr);
        
        if (!access_type || !filename || !target_user) {
            log_message(log_file, LOG_LEVEL_WARNING, NULL, 0, NULL, 
                       "ADDACCESS: Missing parameters - user='%s'", session->username);
            send(session->socket_fd, "ERROR|Missing parameters\n", 25, 0);
            return;
        }
        
        log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
                   "ADDACCESS request: user='%s', file='%s', target='%s', type='%s'", 
                   session->username, filename, target_user, access_type);
        
        // Check if owner
        FileAccessControl *acl = get_file_acl(&config->acl_manager, filename);
        if (!acl || strcmp(acl->owner, session->username) != 0) {
            log_message(log_file, LOG_LEVEL_WARNING, NULL, 0, NULL, 
                       "ADDACCESS denied: user='%s' is not owner of '%s'", 
                       session->username, filename);
            send(session->socket_fd, "ERROR|Only owner can grant access\n", 35, 0);
            return;
        }
        
        int access_level = ACCESS_NONE;
        if (strcmp(access_type, "-R") == 0) {
            access_level = ACCESS_READ;
        } else if (strcmp(access_type, "-W") == 0) {
            access_level = ACCESS_WRITE;
        } else {
            log_message(log_file, LOG_LEVEL_WARNING, NULL, 0, NULL, 
                       "ADDACCESS: Invalid access type '%s'", access_type);
            send(session->socket_fd, "ERROR|Invalid access type (use -R or -W)\n", 42, 0);
            return;
        }
        
        int result = grant_access(&config->acl_manager, filename, target_user, access_level);
        
        if (result == ERR_SUCCESS) {
            log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
                       "Access granted: file='%s', target='%s', level=%d, by='%s'", 
                       filename, target_user, access_level, session->username);
            send(session->socket_fd, "SUCCESS|Access granted successfully!\n", 38, 0);
        } else {
            log_message(log_file, LOG_LEVEL_ERROR, NULL, 0, NULL, 
                       "ADDACCESS failed: file='%s', target='%s', error=%d", 
                       filename, target_user, result);
            char error[256];
            snprintf(error, sizeof(error), "ERROR|%s\n", get_error_message(result));
            send(session->socket_fd, error, strlen(error), 0);
        }
    }
    
    // ========================================================================
    // REMACCESS - Revoke access
    // ========================================================================
    else if (strcmp(cmd, "REMACCESS") == 0) {
        char *filename = strtok_r(NULL, "|", &saveptr);
        char *target_user = strtok_r(NULL, "|", &saveptr);
        
        if (!filename || !target_user) {
            log_message(log_file, LOG_LEVEL_WARNING, NULL, 0, NULL, 
                       "REMACCESS: Missing parameters - user='%s'", session->username);
            send(session->socket_fd, "ERROR|Missing parameters\n", 25, 0);
            return;
        }
        
        log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
                   "REMACCESS request: user='%s', file='%s', target='%s'", 
                   session->username, filename, target_user);
        
        // Check if owner
        FileAccessControl *acl = get_file_acl(&config->acl_manager, filename);
        if (!acl || strcmp(acl->owner, session->username) != 0) {
            log_message(log_file, LOG_LEVEL_WARNING, NULL, 0, NULL, 
                       "REMACCESS denied: user='%s' is not owner of '%s'", 
                       session->username, filename);
            send(session->socket_fd, "ERROR|Only owner can revoke access\n", 36, 0);
            return;
        }
        
        int result = revoke_access(&config->acl_manager, filename, target_user);
        
        if (result == ERR_SUCCESS) {
            log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
                       "Access revoked: file='%s', target='%s', by='%s'", 
                       filename, target_user, session->username);
            send(session->socket_fd, "SUCCESS|Access removed successfully!\n", 38, 0);
        } else {
            log_message(log_file, LOG_LEVEL_ERROR, NULL, 0, NULL, 
                       "REMACCESS failed: file='%s', target='%s', error=%d", 
                       filename, target_user, result);
            char error[256];
            snprintf(error, sizeof(error), "ERROR|%s\n", get_error_message(result));
            send(session->socket_fd, error, strlen(error), 0);
        }
    }
    
    // ========================================================================
    // Unknown command
    // ========================================================================
    else {
        log_message(log_file, LOG_LEVEL_WARNING, NULL, 0, NULL, 
                   "Unknown command: user='%s', command='%s'", 
                   session->username, cmd);
        char error[256];
        snprintf(error, sizeof(error), "ERROR|Unknown command: %s\n", cmd);
        send(session->socket_fd, error, strlen(error), 0);
    }
}
