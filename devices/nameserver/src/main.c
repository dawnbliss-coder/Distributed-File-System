#include "../include/nameserver.h"
#include <signal.h>

NameServerConfig global_config;
FILE* log_file;

void signal_handler(int signum) {
    printf("\nReceived signal %d, shutting down Name Server...\n", signum);
    
    log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
               "Signal %d received, initiating graceful shutdown", signum);
    
    global_config.is_running = 0;
    
    log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
               "Calling cleanup_nameserver");
    cleanup_nameserver(&global_config);
    
    if (global_config.nm_socket > 0) {
        log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
                   "Closing NM socket: fd=%d", global_config.nm_socket);
        close(global_config.nm_socket);
    }
    
    if (global_config.client_socket > 0) {
        log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
                   "Closing client socket: fd=%d", global_config.client_socket);
        close(global_config.client_socket);
    }
    
    log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
               "Name Server shutdown complete");
    
    if (log_file) {
        fclose(log_file);
    }
    
    exit(0);
}

int main(int argc, char *argv[]) {
    int nm_port, client_port;

    // Initialize log file first
    log_file = fopen(LOG_FILE, "w");
    if (!log_file) {
        fprintf(stderr, "WARNING: Failed to open log file: %s\n", LOG_FILE);
        // Continue anyway, logging will be disabled
    } else {
        log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
                   "Name Server starting - log file initialized");
    }
    
    if (argc < 3) {
        log_message(log_file, LOG_LEVEL_ERROR, NULL, 0, NULL, 
                   "Insufficient arguments: argc=%d (expected 3)", argc);
        fprintf(stderr, "Usage: %s <nm_port> <client_port>\n", argv[0]);
        fprintf(stderr, "Example: %s 9000 9001\n", argv[0]);
        if (log_file) fclose(log_file);
        return 1;
    }
    
    nm_port = atoi(argv[1]);
    client_port = atoi(argv[2]);
    
    log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
               "Configuration: nm_port=%d, client_port=%d", nm_port, client_port);
    
    // Validate port numbers
    if (nm_port <= 0 || nm_port > 65535 || client_port <= 0 || client_port > 65535) {
        log_message(log_file, LOG_LEVEL_ERROR, NULL, 0, NULL, 
                   "Invalid port numbers: nm_port=%d, client_port=%d", 
                   nm_port, client_port);
        fprintf(stderr, "Error: Port numbers must be between 1 and 65535\n");
        if (log_file) fclose(log_file);
        return 1;
    }
    
    if (nm_port == client_port) {
        log_message(log_file, LOG_LEVEL_ERROR, NULL, 0, NULL, 
                   "Port conflict: nm_port and client_port are both %d", nm_port);
        fprintf(stderr, "Error: NM port and client port must be different\n");
        if (log_file) fclose(log_file);
        return 1;
    }
    
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════╗\n");
    printf("║         LangOS Distributed File System - Name Server             ║\n");
    printf("║              with Fault Tolerance & Auto-Backup                  ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
               "Signal handlers registered (SIGINT, SIGTERM)");
    
    printf("Initializing Name Server...\n");
    log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
               "Initializing name server subsystems");
    
    int init_result = init_nameserver(&global_config, nm_port, client_port);
    if (init_result != ERR_SUCCESS) {
        log_message(log_file, LOG_LEVEL_CRITICAL, NULL, 0, NULL, 
                   "Name server initialization failed: error=%d", init_result);
        fprintf(stderr, "Failed to initialize name server\n");
        if (log_file) fclose(log_file);
        return 1;
    }
    
    log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
               "Name server initialized successfully");
    
    printf("Name Server initialized successfully\n");
    printf("  SS Port: %d\n", nm_port);
    printf("  Client Port: %d\n", client_port);
    printf("\nName Server is ready. Waiting for connections...\n\n");
    
    // Create thread for SS connections
    log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
               "Creating storage server accept thread");
    
    if (pthread_create(&global_config.nm_accept_thread, NULL, 
                      accept_storage_server_connections, &global_config) != 0) {
        log_message(log_file, LOG_LEVEL_CRITICAL, NULL, 0, NULL, 
                   "Failed to create storage server accept thread (errno=%d)", errno);
        fprintf(stderr, "Failed to create SS thread\n");
        cleanup_nameserver(&global_config);
        if (log_file) fclose(log_file);
        return 1;
    }
    
    log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
               "Storage server accept thread created: thread_id=%lu", 
               global_config.nm_accept_thread);
    
    // Create thread for client connections
    log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
               "Creating client accept thread");
    
    if (pthread_create(&global_config.client_accept_thread, NULL, 
                      accept_client_connections, &global_config) != 0) {
        log_message(log_file, LOG_LEVEL_CRITICAL, NULL, 0, NULL, 
                   "Failed to create client accept thread (errno=%d)", errno);
        fprintf(stderr, "Failed to create client thread\n");
        cleanup_nameserver(&global_config);
        if (log_file) fclose(log_file);
        return 1;
    }
    
    log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
               "Client accept thread created: thread_id=%lu", 
               global_config.client_accept_thread);
    
    // Create heartbeat monitor thread
    log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
               "Creating heartbeat monitor thread");
    
    if (pthread_create(&global_config.heartbeat_thread, NULL, 
                      monitor_ss_heartbeats, &global_config) != 0) {
        log_message(log_file, LOG_LEVEL_CRITICAL, NULL, 0, NULL, 
                   "Failed to create heartbeat monitor thread (errno=%d)", errno);
        fprintf(stderr, "Failed to create heartbeat thread\n");
        cleanup_nameserver(&global_config);
        if (log_file) fclose(log_file);
        return 1;
    }
    
    log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
               "Heartbeat monitor thread created: thread_id=%lu", 
               global_config.heartbeat_thread);
    
    log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
               "All threads started successfully - name server operational");
    
    // Wait for threads
    log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
               "Main thread waiting for worker threads to complete");
    
    void *ss_thread_result = NULL;
    void *client_thread_result = NULL;
    void *heartbeat_thread_result = NULL;
    
    int ss_join = pthread_join(global_config.nm_accept_thread, &ss_thread_result);
    if (ss_join != 0) {
        log_message(log_file, LOG_LEVEL_ERROR, NULL, 0, NULL, 
                   "SS accept thread join failed: error=%d", ss_join);
    } else {
        log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
                   "SS accept thread joined successfully");
    }
    
    int client_join = pthread_join(global_config.client_accept_thread, &client_thread_result);
    if (client_join != 0) {
        log_message(log_file, LOG_LEVEL_ERROR, NULL, 0, NULL, 
                   "Client accept thread join failed: error=%d", client_join);
    } else {
        log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
                   "Client accept thread joined successfully");
    }
    
    int heartbeat_join = pthread_join(global_config.heartbeat_thread, &heartbeat_thread_result);
    if (heartbeat_join != 0) {
        log_message(log_file, LOG_LEVEL_ERROR, NULL, 0, NULL, 
                   "Heartbeat monitor thread join failed: error=%d", heartbeat_join);
    } else {
        log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
                   "Heartbeat monitor thread joined successfully");
    }
    
    log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
               "All worker threads terminated, performing cleanup");
    
    cleanup_nameserver(&global_config);
    
    log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
               "Name Server shutdown sequence completed successfully");
    
    printf("\nName Server shutdown complete\n");
    
    if (log_file) {
        fclose(log_file);
    }
    
    return 0;
}
