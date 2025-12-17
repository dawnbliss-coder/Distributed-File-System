#include "../include/nameserver.h"

// External log file handle
extern FILE* log_file;

// Create a new client session
ClientSession* create_client_session(int socket_fd, const char *username, 
                                     const char *ip, int port) {
    log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
               "Creating client session: username='%s', socket_fd=%d, ip=%s:%d", 
               username, socket_fd, ip, port);
    
    ClientSession *session = malloc(sizeof(ClientSession));
    if (!session) {
        log_message(log_file, LOG_LEVEL_ERROR, NULL, 0, NULL, 
                   "Failed to allocate client session: username='%s' (out of memory)", 
                   username);
        return NULL;
    }

    session->socket_fd = socket_fd;
    strncpy(session->username, username, MAX_USERNAME_LENGTH - 1);
    session->username[MAX_USERNAME_LENGTH - 1] = '\0';
    strncpy(session->ip, ip, INET_ADDRSTRLEN - 1);
    session->ip[INET_ADDRSTRLEN - 1] = '\0';
    session->port = port;
    session->is_active = 1;
    session->connected_time = time(NULL);
    session->next = NULL;

    log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
               "Client session created: username='%s', ip=%s:%d, connected_time=%ld", 
               username, ip, port, session->connected_time);

    return session;
}

// Add session to linked list
int add_client_session(NameServerConfig *config, ClientSession *session) {
    log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
               "Adding client session: username='%s'", session->username);
    
    pthread_mutex_lock(&config->client_session_lock);

    // Check if username already connected
    ClientSession *current = config->client_sessions;
    int duplicate_check_count = 0;
    
    while (current) {
        duplicate_check_count++;
        if (strcmp(current->username, session->username) == 0 && current->is_active) {
            log_message(log_file, LOG_LEVEL_WARNING, NULL, 0, NULL, 
                       "Duplicate login attempt: username='%s', ip=%s:%d, existing_ip=%s:%d", 
                       session->username, session->ip, session->port, 
                       current->ip, current->port);
            pthread_mutex_unlock(&config->client_session_lock);
            return ERR_ALREADY_HAS_ACCESS; // User already logged in
        }
        current = current->next;
    }

    log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
               "No duplicate found: username='%s', checked=%d sessions", 
               session->username, duplicate_check_count);

    // Add to front of list
    session->next = config->client_sessions;
    config->client_sessions = session;
    config->client_session_count++;
    
    int total_count = config->client_session_count;

    pthread_mutex_unlock(&config->client_session_lock);

    log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
               "Client session added: username='%s', ip=%s:%d, total_clients=%d", 
               session->username, session->ip, session->port, total_count);

    printf("✓ Client session added: %s from %s:%d (Total: %d)\n", 
           session->username, session->ip, session->port, total_count);

    return ERR_SUCCESS;
}

// Remove session from linked list
int remove_client_session(NameServerConfig *config, const char *username) {
    log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
               "Removing client session: username='%s'", username);
    
    pthread_mutex_lock(&config->client_session_lock);

    ClientSession *current = config->client_sessions;
    ClientSession *prev = NULL;
    int search_count = 0;

    while (current) {
        search_count++;
        if (strcmp(current->username, username) == 0) {
            // Log session details before removal
            time_t session_duration = time(NULL) - current->connected_time;
            
            log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
                       "Client session found for removal: username='%s', ip=%s:%d, socket_fd=%d, duration=%ld seconds", 
                       username, current->ip, current->port, current->socket_fd, session_duration);
            
            // Remove from list
            if (prev) {
                prev->next = current->next;
            } else {
                config->client_sessions = current->next;
            }

            current->is_active = 0;
            
            log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
                       "Closing client socket: username='%s', fd=%d", username, current->socket_fd);
            close(current->socket_fd);
            
            config->client_session_count--;
            int remaining_count = config->client_session_count;

            log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
                       "Client session removed: username='%s', duration=%ld seconds, remaining_clients=%d", 
                       username, session_duration, remaining_count);

            printf("✓ Client session removed: %s (Total: %d)\n", username, remaining_count);

            free(current);
            pthread_mutex_unlock(&config->client_session_lock);
            return ERR_SUCCESS;
        }
        prev = current;
        current = current->next;
    }

    log_message(log_file, LOG_LEVEL_WARNING, NULL, 0, NULL, 
               "Client session not found for removal: username='%s', searched=%d sessions", 
               username, search_count);

    pthread_mutex_unlock(&config->client_session_lock);
    return ERR_USER_NOT_FOUND;
}

// Find session by username
ClientSession* find_client_session(NameServerConfig *config, const char *username) {
    log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
               "Searching for client session: username='%s'", username);
    
    pthread_mutex_lock(&config->client_session_lock);

    ClientSession *current = config->client_sessions;
    int search_count = 0;
    
    while (current) {
        search_count++;
        if (strcmp(current->username, username) == 0 && current->is_active) {
            log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
                       "Client session found: username='%s', ip=%s:%d, searched=%d entries", 
                       username, current->ip, current->port, search_count);
            pthread_mutex_unlock(&config->client_session_lock);
            return current;
        }
        current = current->next;
    }

    log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
               "Client session not found: username='%s', searched=%d entries", 
               username, search_count);

    pthread_mutex_unlock(&config->client_session_lock);
    return NULL;
}

// Cleanup all sessions on shutdown
void cleanup_all_sessions(NameServerConfig *config) {
    log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
               "Cleaning up all client sessions");
    
    pthread_mutex_lock(&config->client_session_lock);

    int cleaned_count = 0;
    long total_duration = 0;
    
    ClientSession *current = config->client_sessions;
    while (current) {
        ClientSession *next = current->next;
        
        time_t session_duration = time(NULL) - current->connected_time;
        total_duration += session_duration;
        
        log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
                   "Closing client session: username='%s', ip=%s:%d, socket_fd=%d, duration=%ld seconds", 
                   current->username, current->ip, current->port, 
                   current->socket_fd, session_duration);
        
        current->is_active = 0;
        close(current->socket_fd);
        free(current);
        cleaned_count++;
        
        current = next;
    }

    config->client_sessions = NULL;
    config->client_session_count = 0;

    pthread_mutex_unlock(&config->client_session_lock);
    
    long avg_duration = cleaned_count > 0 ? total_duration / cleaned_count : 0;
    
    log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
               "Client session cleanup complete: sessions_closed=%d, avg_duration=%ld seconds", 
               cleaned_count, avg_duration);
}

// ✅ FIX: Per-client session handler (runs in separate thread)
void* handle_client_session(void *arg) {
    // Extract from proper struct
    ClientThreadArg *thread_arg = (ClientThreadArg*)arg;
    ClientSession *session = thread_arg->session;
    NameServerConfig *config = thread_arg->config;
    free(thread_arg);  // Clean up immediately

    log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
               "Client session thread started: username='%s', ip=%s:%d, thread_id=%lu", 
               session->username, session->ip, session->port, pthread_self());

    char buffer[BUFFER_SIZE];

    printf("  → Session thread started for '%s'\n", session->username);

    // Send welcome message
    char welcome[256];
    snprintf(welcome, sizeof(welcome), 
             "SUCCESS|Welcome %s! Connected to LangOS Name Server.\n", 
             session->username);
    send(session->socket_fd, welcome, strlen(welcome), 0);
    
    log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
               "Welcome message sent: username='%s'", session->username);

    int command_count = 0;
    int empty_messages = 0;

    // Command loop - THIS IS THE KEY DIFFERENCE
    while (session->is_active) {
        memset(buffer, 0, sizeof(buffer));
        
        log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
                   "Waiting for command from '%s' (commands_processed=%d)", 
                   session->username, command_count);
        
        ssize_t bytes = recv(session->socket_fd, buffer, sizeof(buffer) - 1, 0);

        if (bytes <= 0) {
            // Client disconnected
            log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
                       "Client disconnected: username='%s', bytes=%zd, commands_processed=%d", 
                       session->username, bytes, command_count);
            printf("  ✗ Client '%s' disconnected\n", session->username);
            break;
        }

        buffer[bytes] = '\0';

        // Remove newline
        char *newline = strchr(buffer, '\n');
        if (newline) *newline = '\0';
        newline = strchr(buffer, '\r');
        if (newline) *newline = '\0';

        if (strlen(buffer) == 0) {
            empty_messages++;
            log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
                       "Empty message received from '%s' (empty_count=%d)", 
                       session->username, empty_messages);
            continue;
        }

        command_count++;
        
        log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
                   "Command received: username='%s', command='%s', count=%d", 
                   session->username, buffer, command_count);

        printf("  [%s] Command: %s\n", session->username, buffer);

        // Handle command
        handle_session_command(session, config, buffer);
        
        // Check if session was terminated by command (e.g., QUIT)
        if (!session->is_active) {
            log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
                       "Session marked inactive by command: username='%s'", 
                       session->username);
        }
    }

    time_t session_duration = time(NULL) - session->connected_time;
    
    log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
               "Client session thread ending: username='%s', duration=%ld seconds, commands=%d, empty_messages=%d", 
               session->username, session_duration, command_count, empty_messages);

    // Cleanup
    log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
               "Removing client session: username='%s'", session->username);
    
    remove_client_session(config, session->username);

    return NULL;
}
