#include "../include/nameserver.h"


// ============================================================================
// CLIENT ACCEPT THREAD (Persistent Sessions)
// ============================================================================

void* accept_client_connections(void *arg) {
    NameServerConfig *config = (NameServerConfig*)arg;

    log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
               "Client connection acceptor thread started: port=%d, thread_id=%lu", 
               config->client_port, pthread_self());
    
    printf("✓ Client connection acceptor started on port %d\n", config->client_port);

    int connection_count = 0;
    int failed_accepts = 0;
    int failed_inits = 0;

    while (config->is_running) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
                   "Waiting for client connection (total accepted=%d)", connection_count);

        int client_fd = accept(config->client_socket, (struct sockaddr*)&client_addr, &client_len);

        if (client_fd < 0) {
            if (config->is_running) {
                failed_accepts++;
                log_message(log_file, LOG_LEVEL_ERROR, NULL, 0, NULL, 
                           "Client accept failed (errno=%d: %s, total_failures=%d)", 
                           errno, strerror(errno), failed_accepts);
                perror("Client accept failed");
            } else {
                log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
                           "Accept interrupted - server shutting down");
            }
            continue;
        }

        connection_count++;

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        int client_port = ntohs(client_addr.sin_port);

        log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
                   "New client connection accepted: fd=%d, ip=%s:%d, connection_number=%d", 
                   client_fd, client_ip, client_port, connection_count);
        
        printf("\n[NEW CLIENT] Connection from %s:%d\n", client_ip, client_port);

        // Read INIT message to get username
        char buffer[BUFFER_SIZE];
        
        log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
                   "Waiting for INIT message from %s:%d", client_ip, client_port);
        
        ssize_t bytes = recv(client_fd, buffer, sizeof(buffer) - 1, 0);

        if (bytes <= 0) {
            log_message(log_file, LOG_LEVEL_WARNING, NULL, 0, NULL, 
                       "Client disconnected before sending INIT: %s:%d (bytes=%zd)", 
                       client_ip, client_port, bytes);
            close(client_fd);
            failed_inits++;
            continue;
        }

        buffer[bytes] = '\0';

        // Remove newline
        char *newline = strchr(buffer, '\n');
        if (newline) *newline = '\0';

        log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
                   "Received from %s:%d: '%s' (%zd bytes)", 
                   client_ip, client_port, buffer, bytes);
        
        printf("  Received: %s\n", buffer);

        // Parse INIT|username
        char *saveptr;
        char *cmd = strtok_r(buffer, "|", &saveptr);

        if (!cmd || strcmp(cmd, "INIT") != 0) {
            log_message(log_file, LOG_LEVEL_WARNING, NULL, 0, NULL, 
                       "Invalid INIT command from %s:%d: '%s'", 
                       client_ip, client_port, cmd ? cmd : "(null)");
            send(client_fd, "ERROR|First message must be INIT|username\n", 43, 0);
            close(client_fd);
            failed_inits++;
            continue;
        }

        char *username = strtok_r(NULL, "|", &saveptr);
        if (!username) {
            log_message(log_file, LOG_LEVEL_WARNING, NULL, 0, NULL, 
                       "Missing username in INIT from %s:%d", client_ip, client_port);
            send(client_fd, "ERROR|Missing username\n", 23, 0);
            close(client_fd);
            failed_inits++;
            continue;
        }

        log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
                   "Client initialization: username='%s', ip=%s:%d", 
                   username, client_ip, client_port);

        // Create session
        log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
                   "Creating client session for user '%s'", username);
        
        ClientSession *session = create_client_session(client_fd, username, client_ip, client_port);
        if (!session) {
            log_message(log_file, LOG_LEVEL_ERROR, NULL, 0, NULL, 
                       "Failed to create session for user '%s' from %s:%d", 
                       username, client_ip, client_port);
            send(client_fd, "ERROR|Failed to create session\n", 32, 0);
            close(client_fd);
            continue;
        }

        log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
                   "Session created for '%s': session_id=%d", 
                   username, session->socket_fd);

        // Add to session list
        log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
                   "Adding session to session list: user='%s'", username);
        
        int result = add_client_session(config, session);
        if (result != ERR_SUCCESS) {
            log_message(log_file, LOG_LEVEL_WARNING, NULL, 0, NULL, 
                       "Failed to add session - user '%s' already connected (error=%d)", 
                       username, result);
            send(client_fd, "ERROR|User already connected\n", 29, 0);
            close(client_fd);
            free(session);
            continue;
        }

        log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
                   "Session added to list: user='%s', total_sessions=%d", 
                   username, config->client_session_count);

        // ✅ FIX: Use proper thread argument instead of hijacking ->next
        ClientThreadArg *thread_arg = malloc(sizeof(ClientThreadArg));
        if (!thread_arg) {
            log_message(log_file, LOG_LEVEL_ERROR, NULL, 0, NULL, 
                       "Failed to allocate thread argument for user '%s'", username);
            perror("malloc thread_arg failed");
            remove_client_session(config, username);
            continue;
        }

        thread_arg->session = session;
        thread_arg->config = config;

        log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
                   "Creating handler thread for user '%s'", username);

        // Create thread for this session
        if (pthread_create(&session->thread, NULL, handle_client_session, thread_arg) != 0) {
            log_message(log_file, LOG_LEVEL_ERROR, NULL, 0, NULL, 
                       "Failed to create handler thread for user '%s' (errno=%d: %s)", 
                       username, errno, strerror(errno));
            perror("Failed to create client thread");
            remove_client_session(config, username);
            free(thread_arg);
            continue;
        }

        log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
                   "Client session established: user='%s', ip=%s:%d, thread_id=%lu, session_id=%d", 
                   username, client_ip, client_port, session->thread, session->socket_fd);

        pthread_detach(session->thread);
        
        log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
                   "Handler thread detached for user '%s'", username);
    }

    log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
               "Client connection acceptor stopping: total_connections=%d, failed_accepts=%d, failed_inits=%d", 
               connection_count, failed_accepts, failed_inits);

    return NULL;
}
