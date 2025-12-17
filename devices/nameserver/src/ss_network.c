#include "../include/nameserver.h"

// External log file handle
extern FILE* log_file;

// ============================================================================
// THREAD ARGUMENT STRUCTURE (PROPER WAY)
// ============================================================================
typedef struct {
    SSSession *session;
    NameServerConfig *config;
} SSThreadArg;

// ============================================================================
// ACCEPT STORAGE SERVER CONNECTIONS
// ============================================================================
void* accept_storage_server_connections(void *arg) {
    NameServerConfig *config = (NameServerConfig*)arg;

    log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
               "Storage server listener started: port=%d, thread_id=%lu", 
               config->nm_port, pthread_self());
    
    printf("✓ Storage Server listener started on port %d\n", config->nm_port);

    int next_ss_id = 0;
    int connection_count = 0;
    int registration_failures = 0;

    while (config->is_running) {
        struct sockaddr_in ss_addr;
        socklen_t ss_len = sizeof(ss_addr);

        log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
                   "Waiting for storage server connection");

        int ss_fd = accept(config->nm_socket, (struct sockaddr*)&ss_addr, &ss_len);

        if (ss_fd < 0) {
            if (config->is_running) {
                log_message(log_file, LOG_LEVEL_ERROR, NULL, 0, NULL, 
                           "SS accept failed (errno=%d: %s)", errno, strerror(errno));
                perror("SS accept failed");
            } else {
                log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
                           "Accept interrupted - server shutting down");
            }
            continue;
        }

        connection_count++;

        char ss_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &ss_addr.sin_addr, ss_ip, sizeof(ss_ip));
        int ss_port = ntohs(ss_addr.sin_port);

        log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
                   "Storage server connection accepted: fd=%d, ip=%s:%d, connection_number=%d", 
                   ss_fd, ss_ip, ss_port, connection_count);
        
        printf("\n[NEW SS] Connection from %s:%d\n", ss_ip, ss_port);

        // Read REGISTER message
        char buffer[BUFFER_SIZE];
        
        log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
                   "Waiting for REGISTER message from %s:%d", ss_ip, ss_port);
        
        ssize_t bytes = recv(ss_fd, buffer, sizeof(buffer) - 1, 0);

        if (bytes <= 0) {
            log_message(log_file, LOG_LEVEL_WARNING, NULL, 0, NULL, 
                       "SS disconnected before REGISTER: %s:%d (bytes=%zd)", 
                       ss_ip, ss_port, bytes);
            close(ss_fd);
            registration_failures++;
            continue;
        }

        buffer[bytes] = '\0';
        char *newline = strchr(buffer, '\n');
        if (newline) *newline = '\0';

        log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
                   "Received from %s:%d: '%s' (%zd bytes)", 
                   ss_ip, ss_port, buffer, bytes);
        
        printf("  Registration: %s\n", buffer);

        // Parse REGISTER|IP|NM_PORT|CLIENT_PORT|file1,file2,...
        char *saveptr;
        char *cmd = strtok_r(buffer, "|", &saveptr);

        if (!cmd || strcmp(cmd, "REGISTER") != 0) {
            log_message(log_file, LOG_LEVEL_WARNING, NULL, 0, NULL, 
                       "Invalid REGISTER command from %s:%d: '%s'", 
                       ss_ip, ss_port, cmd ? cmd : "(null)");
            send(ss_fd, "ERROR|First message must be REGISTER\n", 37, 0);
            close(ss_fd);
            registration_failures++;
            continue;
        }

        char *ip = strtok_r(NULL, "|", &saveptr);
        char *nm_port_str = strtok_r(NULL, "|", &saveptr);
        char *client_port_str = strtok_r(NULL, "|", &saveptr);
        char *files_str = strtok_r(NULL, "|", &saveptr);

        if (!ip || !nm_port_str || !client_port_str) {
            log_message(log_file, LOG_LEVEL_WARNING, NULL, 0, NULL, 
                       "Missing parameters in REGISTER from %s:%d", ss_ip, ss_port);
            send(ss_fd, "ERROR|Missing parameters\n", 25, 0);
            close(ss_fd);
            registration_failures++;
            continue;
        }

        int nm_port = atoi(nm_port_str);
        int client_port = atoi(client_port_str);

        log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
                   "REGISTER parameters: ip=%s, nm_port=%d, client_port=%d, files=%s", 
                   ip, nm_port, client_port, files_str ? files_str : "(none)");

        // Assign SS ID
        int ss_id = next_ss_id++;

        log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
                   "Assigning SS ID: ss_id=%d, ip=%s, nm_port=%d, client_port=%d", 
                   ss_id, ip, nm_port, client_port);

        // Create SS session
        SSSession *session = create_ss_session(ss_fd, ss_id, ip, nm_port, client_port);
        if (!session) {
            log_message(log_file, LOG_LEVEL_ERROR, NULL, 0, NULL, 
                       "Failed to create SS session: ss_id=%d, ip=%s", ss_id, ip);
            send(ss_fd, "ERROR|Failed to create session\n", 32, 0);
            close(ss_fd);
            continue;
        }

        log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
                   "SS session created: ss_id=%d", ss_id);

        // Add to session list
        add_ss_session(config, session);
        
        log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
                   "SS session added to list: ss_id=%d, total_ss=%d", 
                   ss_id, config->ss_session_count);

        // Parse and register files
        int file_count = 0;
        if (files_str && strlen(files_str) > 0) {
            log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
                       "Registering files for SS#%d: files=%s", ss_id, files_str);
            
            char *file = strtok(files_str, ",");
            while (file) {
                // Add file mapping
                add_file_mapping(&config->file_table, file, ss_id);
                file_count++;
                
                log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
                           "File registered: filename='%s', ss_id=%d", file, ss_id);
                printf("    → File '%s' registered\n", file);
                
                file = strtok(NULL, ",");
            }
        }

        log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
                   "Files registered for SS#%d: count=%d", ss_id, file_count);

        // Send success response
        char response[256];
        snprintf(response, sizeof(response), "SUCCESS|SS_ID=%d\n", ss_id);
        send(ss_fd, response, strlen(response), 0);

        log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
                   "SS registration successful: ss_id=%d, ip=%s:%d, files=%d", 
                   ss_id, ip, client_port, file_count);
        
        printf("  → SS#%d registered as PRIMARY\n", ss_id);

        // ✅ FIX: Use proper thread argument struct
        SSThreadArg *thread_arg = malloc(sizeof(SSThreadArg));
        if (!thread_arg) {
            log_message(log_file, LOG_LEVEL_ERROR, NULL, 0, NULL, 
                       "Failed to allocate thread argument for SS#%d", ss_id);
            perror("malloc thread_arg failed");
            remove_ss_session(config, ss_id);
            continue;
        }

        thread_arg->session = session;
        thread_arg->config = config;

        log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
                   "Creating handler thread for SS#%d", ss_id);

        // Create thread for this SS session
        if (pthread_create(&session->thread, NULL, handle_ss_session, thread_arg) != 0) {
            log_message(log_file, LOG_LEVEL_ERROR, NULL, 0, NULL, 
                       "Failed to create handler thread for SS#%d (errno=%d: %s)", 
                       ss_id, errno, strerror(errno));
            perror("Failed to create SS thread");
            remove_ss_session(config, ss_id);
            free(thread_arg);
            continue;
        }

        log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
                   "SS session thread created: ss_id=%d, thread_id=%lu", 
                   ss_id, session->thread);

        pthread_detach(session->thread);
        
        log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
                   "Handler thread detached for SS#%d", ss_id);
    }

    log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
               "Storage server listener stopping: total_connections=%d, registration_failures=%d", 
               connection_count, registration_failures);

    return NULL;
}

// ============================================================================
// PER-SS SESSION THREAD (HANDLES HEARTBEATS AND COMMANDS)
// ============================================================================
void* handle_ss_session(void *arg) {
    // ✅ FIX: Extract from proper struct
    SSThreadArg *thread_arg = (SSThreadArg*)arg;
    SSSession *session = thread_arg->session;
    NameServerConfig *config = thread_arg->config;
    free(thread_arg);  // Clean up immediately

    log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
               "SS session thread started: ss_id=%d, ip=%s:%d, thread_id=%lu", 
               session->ss_id, session->ip, session->client_port, pthread_self());

    char buffer[BUFFER_SIZE];

    printf("  → SS#%d session thread started\n", session->ss_id);

    int command_count = 0;
    int heartbeat_count = 0;
    int timeout_count = 0;

    // Session loop - PERSISTENT CONNECTION
    while (session->is_active) {
        memset(buffer, 0, sizeof(buffer));

        // Set timeout for recv (5 seconds)
        struct timeval tv;
        tv.tv_sec = 5;
        tv.tv_usec = 0;
        setsockopt(session->socket_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
                   "Waiting for message from SS#%d (timeout=5s)", session->ss_id);

        ssize_t bytes = recv(session->socket_fd, buffer, sizeof(buffer) - 1, 0);

        if (bytes < 0) {
            // Timeout or error - check if still alive
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Timeout - send heartbeat request
                timeout_count++;
                
                log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
                           "Receive timeout from SS#%d, sending heartbeat (timeout_count=%d)", 
                           session->ss_id, timeout_count);
                
                send(session->socket_fd, "HEARTBEAT\n", 10, 0);
                
                
                continue;
            } else {
                log_message(log_file, LOG_LEVEL_ERROR, NULL, 0, NULL, 
                           "SS#%d connection error (errno=%d: %s)", 
                           session->ss_id, errno, strerror(errno));
                printf("  ✗ SS#%d connection error\n", session->ss_id);
                break;
            }
        }

        if (bytes == 0) {
            log_message(log_file, LOG_LEVEL_WARNING, NULL, 0, NULL, 
                       "SS#%d disconnected gracefully", session->ss_id);
            printf("  ✗ SS#%d disconnected\n", session->ss_id);
            break;
        }

        buffer[bytes] = '\0';
        char *newline = strchr(buffer, '\n');
        if (newline) *newline = '\0';

        if (strlen(buffer) == 0) {
            log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
                       "Empty message received from SS#%d", session->ss_id);
            continue;
        }

        command_count++;
        
        log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
                   "Received from SS#%d: '%s' (%zd bytes, command_count=%d)", 
                   session->ss_id, buffer, bytes, command_count);

        // Handle command
        handle_ss_session_command(session, config, buffer);
        
        // Check if it was a heartbeat
        if (strncmp(buffer, "HEARTBEAT_ACK", 13) == 0) {
            heartbeat_count++;
        }
    }

    log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
               "SS session thread ending: ss_id=%d, commands=%d, heartbeats=%d, timeouts=%d", 
               session->ss_id, command_count, heartbeat_count, timeout_count);

    // SS failed
    log_message(log_file, LOG_LEVEL_WARNING, NULL, 0, NULL, 
               "Handling SS#%d failure", session->ss_id);
    
    handle_ss_failure(config, session->ss_id);

    return NULL;
}

// ============================================================================
// HANDLE SS SESSION COMMANDS
// ============================================================================
void handle_ss_session_command(SSSession *session, NameServerConfig *config, 
                               const char *command) {
    log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
               "Processing SS command: ss_id=%d, command='%s'", 
               session->ss_id, command);
    
    char cmd_copy[BUFFER_SIZE];
    strncpy(cmd_copy, command, sizeof(cmd_copy) - 1);
    cmd_copy[sizeof(cmd_copy) - 1] = '\0';

    char *saveptr;
    char *cmd = strtok_r(cmd_copy, "|", &saveptr);

    if (!cmd) {
        log_message(log_file, LOG_LEVEL_WARNING, NULL, 0, NULL, 
                   "Invalid command format from SS#%d", session->ss_id);
        return;
    }

    // HEARTBEAT_ACK - SS responding to heartbeat
    if (strcmp(cmd, "HEARTBEAT_ACK") == 0) {
        pthread_mutex_lock(&config->ss_session_lock);
        time_t old_heartbeat = session->last_heartbeat;
        session->last_heartbeat = time(NULL);
        time_t response_time = session->last_heartbeat - old_heartbeat;
        pthread_mutex_unlock(&config->ss_session_lock);
        
        log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
                   "Heartbeat acknowledged: ss_id=%d, response_time=%ld seconds", 
                   session->ss_id, response_time);
    }

    // FILE_CREATED|filename - SS notifying of new file
    else if (strcmp(cmd, "FILE_CREATED") == 0) {
        char *filename = strtok_r(NULL, "|", &saveptr);
        if (filename) {
            log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
                       "File creation notification: filename='%s', ss_id=%d", 
                       filename, session->ss_id);
            
            add_file_mapping(&config->file_table, filename, session->ss_id);
            
            log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
                       "File mapping added: filename='%s', ss_id=%d", 
                       filename, session->ss_id);
            
            printf("    → File '%s' created on SS#%d\n", filename, session->ss_id);
        } else {
            log_message(log_file, LOG_LEVEL_WARNING, NULL, 0, NULL, 
                       "FILE_CREATED missing filename from SS#%d", session->ss_id);
        }
    }

    // FILE_DELETED|filename - SS notifying of file deletion
    else if (strcmp(cmd, "FILE_DELETED") == 0) {
        char *filename = strtok_r(NULL, "|", &saveptr);
        if (filename) {
            log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
                       "File deletion notification: filename='%s', ss_id=%d", 
                       filename, session->ss_id);
            
            remove_file_mapping(&config->file_table, filename);
            
            log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
                       "File mapping removed: filename='%s', ss_id=%d", 
                       filename, session->ss_id);
            
            printf("    → File '%s' deleted from SS#%d\n", filename, session->ss_id);
        } else {
            log_message(log_file, LOG_LEVEL_WARNING, NULL, 0, NULL, 
                       "FILE_DELETED missing filename from SS#%d", session->ss_id);
        }
    }

    // FILE_UPDATED|filename - SS notifying of file modification
    else if (strcmp(cmd, "FILE_UPDATED") == 0) {
        char *filename = strtok_r(NULL, "|", &saveptr);
        if (filename) {
            log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
                       "File update notification: filename='%s', ss_id=%d", 
                       filename, session->ss_id);
            
            printf("    → File '%s' updated on SS#%d\n", filename, session->ss_id);
        } else {
            log_message(log_file, LOG_LEVEL_WARNING, NULL, 0, NULL, 
                       "FILE_UPDATED missing filename from SS#%d", session->ss_id);
        }
    }
    
    // Unknown command
    else {
        log_message(log_file, LOG_LEVEL_WARNING, NULL, 0, NULL, 
                   "Unknown command from SS#%d: '%s'", session->ss_id, cmd);
    }
}
