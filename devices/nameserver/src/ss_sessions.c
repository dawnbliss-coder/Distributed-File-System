#include "../include/nameserver.h"

// Create new SS session
SSSession* create_ss_session(int socket_fd, int ss_id, const char *ip, 
                             int nm_port, int client_port) {
    SSSession *session = malloc(sizeof(SSSession));
    if (!session) return NULL;
    
    session->ss_id = ss_id;
    session->socket_fd = socket_fd;
    strncpy(session->ip, ip, INET_ADDRSTRLEN - 1);
    session->ip[INET_ADDRSTRLEN - 1] = '\0';
    session->nm_port = nm_port;
    session->client_port = client_port;
    session->is_active = 1;
    session->last_heartbeat = time(NULL);
    session->next = NULL;
    
    return session;
}

// Add SS session to linked list
int add_ss_session(NameServerConfig *config, SSSession *session) {
    pthread_mutex_lock(&config->ss_session_lock);
    
    // Add to front of list
    session->next = config->ss_sessions;
    config->ss_sessions = session;
    config->ss_session_count++;
    
    pthread_mutex_unlock(&config->ss_session_lock);
    
    printf("✓ SS#%d session added: %s:%d (client_port=%d) [Total SS: %d]\n", 
           session->ss_id, session->ip, session->nm_port, 
           session->client_port, config->ss_session_count);
    
    return ERR_SUCCESS;
}

// Remove SS session
int remove_ss_session(NameServerConfig *config, int ss_id) {
    pthread_mutex_lock(&config->ss_session_lock);
    
    SSSession *current = config->ss_sessions;
    SSSession *prev = NULL;
    
    while (current) {
        if (current->ss_id == ss_id) {
            // Remove from list
            if (prev) {
                prev->next = current->next;
            } else {
                config->ss_sessions = current->next;
            }
            
            current->is_active = 0;
            close(current->socket_fd);
            config->ss_session_count--;
            
            printf("✗ SS#%d session removed (Total: %d)\n", 
                   ss_id, config->ss_session_count);
            
            free(current);
            pthread_mutex_unlock(&config->ss_session_lock);
            return ERR_SUCCESS;
        }
        prev = current;
        current = current->next;
    }
    
    pthread_mutex_unlock(&config->ss_session_lock);
    return ERR_SS_NOT_REGISTERED;
}

// Find SS session by ID
SSSession* find_ss_session(NameServerConfig *config, int ss_id) {
    pthread_mutex_lock(&config->ss_session_lock);
    
    SSSession *current = config->ss_sessions;
    while (current) {
        if (current->ss_id == ss_id && current->is_active) {
            pthread_mutex_unlock(&config->ss_session_lock);
            return current;
        }
        current = current->next;
    }
    
    pthread_mutex_unlock(&config->ss_session_lock);
    return NULL;
}

// Handle SS failure
void handle_ss_failure(NameServerConfig *config, int failed_ss_id) {
    printf("\n⚠ SS#%d FAILED - Removing from system...\n", failed_ss_id);
    
    // Remove from session list
    remove_ss_session(config, failed_ss_id);
    
    // Remove all file mappings for this SS
    printf("  → Removing file mappings for SS#%d\n", failed_ss_id);
    
    pthread_mutex_lock(&config->file_table.lock);
    for (int i = 0; i < HASH_TABLE_SIZE; i++) {
        FileMapping *current = config->file_table.buckets[i];
        FileMapping *prev = NULL;
        
        while (current) {
            if (current->primary_ss_id == failed_ss_id) {
                printf("    ✗ File '%s' lost\n", current->filename);
                FileMapping *to_delete = current;
                
                if (prev) {
                    prev->next = current->next;
                } else {
                    config->file_table.buckets[i] = current->next;
                }
                
                current = current->next;
                free(to_delete);
            } else {
                prev = current;
                current = current->next;
            }
        }
    }
    pthread_mutex_unlock(&config->file_table.lock);
    
    printf("  ✓ Cleanup complete for SS#%d\n", failed_ss_id);
}

// Monitor SS heartbeats
void* monitor_ss_heartbeats(void *arg) {
    NameServerConfig *config = (NameServerConfig*)arg;
    
    printf("✓ Heartbeat monitor started\n");
    
    while (config->is_running) {
        sleep(5); // Check every 5 seconds
        
        pthread_mutex_lock(&config->ss_session_lock);
        
        SSSession *current = config->ss_sessions;
        while (current) {
            time_t now = time(NULL);
            time_t elapsed = now - current->last_heartbeat;
            
            // If no heartbeat for 15 seconds, consider failed
            if (elapsed > 15 && current->is_active) {
                int failed_id = current->ss_id;
                pthread_mutex_unlock(&config->ss_session_lock);
                
                handle_ss_failure(config, failed_id);
                
                pthread_mutex_lock(&config->ss_session_lock);
                // Restart iteration after modification
                current = config->ss_sessions;
                continue;
            }
            
            current = current->next;
        }
        
        pthread_mutex_unlock(&config->ss_session_lock);
    }
    
    return NULL;
}