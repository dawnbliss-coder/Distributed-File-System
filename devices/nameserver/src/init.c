#include "../include/nameserver.h"


int init_nameserver(NameServerConfig *config, int nm_port, int client_port) {
    log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
               "Initializing name server: nm_port=%d, client_port=%d", 
               nm_port, client_port);
    
    memset(config, 0, sizeof(NameServerConfig));
    
    config->nm_port = nm_port;
    config->client_port = client_port;
    config->is_running = 1;
    
    log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
               "Configuration structure initialized");
    
    // Initialize SS sessions
    config->ss_sessions = NULL;
    config->ss_session_count = 0;
    pthread_mutex_init(&config->ss_session_lock, NULL);
    
    log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
               "Storage server session structures initialized");
    
    // Initialize client sessions
    config->client_sessions = NULL;
    config->client_session_count = 0;
    pthread_mutex_init(&config->client_session_lock, NULL);
    
    log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
               "Client session structures initialized");
    
    // Initialize hash table
    log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
               "Initializing file hash table");
    init_hash_table(&config->file_table);
    log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
               "File hash table initialized");
    
    // Initialize ACL manager
    log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
               "Initializing access control manager");
    init_access_control(&config->acl_manager);
    log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
               "Access control manager initialized");

    printf("Loading ACL cache...\n");
    log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
               "Loading ACL cache from persistent storage");
    load_acl_cache(&config->acl_manager);
    log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
               "ACL cache loaded successfully");
    
    // Create socket for storage servers
    log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
               "Creating storage server socket");
    
    config->nm_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (config->nm_socket < 0) {
        log_message(log_file, LOG_LEVEL_CRITICAL, NULL, 0, NULL, 
                   "Storage server socket creation failed (errno=%d: %s)", 
                   errno, strerror(errno));
        perror("SS socket creation failed");
        return ERR_SOCKET_CREATE_FAILED;
    }
    
    log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
               "Storage server socket created: fd=%d", config->nm_socket);
    
    int opt = 1;
    if (setsockopt(config->nm_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        log_message(log_file, LOG_LEVEL_WARNING, NULL, 0, NULL, 
                   "Failed to set SO_REUSEADDR on SS socket (errno=%d)", errno);
    } else {
        log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
                   "SO_REUSEADDR enabled on SS socket");
    }
    
    struct sockaddr_in nm_addr;
    memset(&nm_addr, 0, sizeof(nm_addr));
    nm_addr.sin_family = AF_INET;
    nm_addr.sin_addr.s_addr = INADDR_ANY;
    nm_addr.sin_port = htons(nm_port);
    
    log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
               "Binding SS socket to port %d (INADDR_ANY)", nm_port);
    
    if (bind(config->nm_socket, (struct sockaddr*)&nm_addr, sizeof(nm_addr)) < 0) {
        log_message(log_file, LOG_LEVEL_CRITICAL, NULL, 0, NULL, 
                   "Storage server socket bind failed on port %d (errno=%d: %s)", 
                   nm_port, errno, strerror(errno));
        perror("SS socket bind failed");
        close(config->nm_socket);
        return ERR_BIND_FAILED;
    }
    
    log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
               "Storage server socket bound to port %d", nm_port);
    
    if (listen(config->nm_socket, 10) < 0) {
        log_message(log_file, LOG_LEVEL_CRITICAL, NULL, 0, NULL, 
                   "Storage server socket listen failed (errno=%d: %s)", 
                   errno, strerror(errno));
        perror("SS socket listen failed");
        close(config->nm_socket);
        return ERR_LISTEN_FAILED;
    }
    
    log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
               "Storage server socket listening (backlog=10)");
    
    // Create socket for clients
    log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
               "Creating client socket");
    
    config->client_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (config->client_socket < 0) {
        log_message(log_file, LOG_LEVEL_CRITICAL, NULL, 0, NULL, 
                   "Client socket creation failed (errno=%d: %s)", 
                   errno, strerror(errno));
        perror("Client socket creation failed");
        close(config->nm_socket);
        return ERR_SOCKET_CREATE_FAILED;
    }
    
    log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
               "Client socket created: fd=%d", config->client_socket);
    
    if (setsockopt(config->client_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        log_message(log_file, LOG_LEVEL_WARNING, NULL, 0, NULL, 
                   "Failed to set SO_REUSEADDR on client socket (errno=%d)", errno);
    } else {
        log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
                   "SO_REUSEADDR enabled on client socket");
    }
    
    struct sockaddr_in client_addr;
    memset(&client_addr, 0, sizeof(client_addr));
    client_addr.sin_family = AF_INET;
    client_addr.sin_addr.s_addr = INADDR_ANY;
    client_addr.sin_port = htons(client_port);
    
    log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
               "Binding client socket to port %d (INADDR_ANY)", client_port);
    
    if (bind(config->client_socket, (struct sockaddr*)&client_addr, sizeof(client_addr)) < 0) {
        log_message(log_file, LOG_LEVEL_CRITICAL, NULL, 0, NULL, 
                   "Client socket bind failed on port %d (errno=%d: %s)", 
                   client_port, errno, strerror(errno));
        perror("Client socket bind failed");
        close(config->nm_socket);
        close(config->client_socket);
        return ERR_BIND_FAILED;
    }
    
    log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
               "Client socket bound to port %d", client_port);
    
    if (listen(config->client_socket, 10) < 0) {
        log_message(log_file, LOG_LEVEL_CRITICAL, NULL, 0, NULL, 
                   "Client socket listen failed (errno=%d: %s)", 
                   errno, strerror(errno));
        perror("Client socket listen failed");
        close(config->nm_socket);
        close(config->client_socket);
        return ERR_LISTEN_FAILED;
    }
    
    log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
               "Client socket listening (backlog=10)");
    
    log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
               "Name server initialization completed successfully - SS port=%d, Client port=%d", 
               nm_port, client_port);
    
    return ERR_SUCCESS;
}

void cleanup_nameserver(NameServerConfig *config) {
    log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
               "Starting name server cleanup");
    
    config->is_running = 0;
    log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
               "Set is_running flag to 0");
    
    printf("Saving ACL cache...\n");
    log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
               "Saving ACL cache to persistent storage");
    save_acl_cache(&config->acl_manager);
    log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
               "ACL cache saved successfully");
    
    if (config->nm_socket > 0) {
        log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
                   "Closing storage server socket: fd=%d", config->nm_socket);
        close(config->nm_socket);
    }
    
    if (config->client_socket > 0) {
        log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
                   "Closing client socket: fd=%d", config->client_socket);
        close(config->client_socket);
    }
    
    // Cleanup SS sessions
    log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
               "Cleaning up storage server sessions");
    
    pthread_mutex_lock(&config->ss_session_lock);
    
    int ss_cleaned = 0;
    SSSession *ss_current = config->ss_sessions;
    while (ss_current) {
        SSSession *next = ss_current->next;
        
        log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
                   "Closing SS session: ss_id=%d, socket=%d, ip=%s", 
                   ss_current->ss_id, ss_current->socket_fd, ss_current->ip);
        
        close(ss_current->socket_fd);
        free(ss_current);
        ss_cleaned++;
        
        ss_current = next;
    }
    
    pthread_mutex_unlock(&config->ss_session_lock);
    
    log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
               "Storage server sessions cleaned up: %d sessions closed", ss_cleaned);
    
    // Cleanup client sessions
    log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
               "Cleaning up client sessions");
    
    int client_sessions_before = config->client_session_count;
    cleanup_all_sessions(config);
    
    log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
               "Client sessions cleaned up: %d sessions closed", client_sessions_before);
    
    // Cleanup hash table
    log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
               "Cleaning up file hash table");
    cleanup_hash_table(&config->file_table);
    log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
               "File hash table cleaned up");
    
    // Destroy mutexes
    log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
               "Destroying mutex locks");
    
    pthread_mutex_destroy(&config->ss_session_lock);
    log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
               "SS session lock destroyed");
    
    pthread_mutex_destroy(&config->client_session_lock);
    log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
               "Client session lock destroyed");
    
    pthread_mutex_destroy(&config->acl_manager.acl_lock);
    log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
               "ACL manager lock destroyed");
    
    log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
               "Name server cleanup completed successfully (SS sessions=%d, Client sessions=%d)", 
               ss_cleaned, client_sessions_before);
}
