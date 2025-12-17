#include "../include/nameserver.h"

// External log file handle
extern FILE* log_file;

// Global variable for round-robin
static int last_assigned_ss = -1;

// Find available SS for new file creation (ROUND ROBIN)
int find_available_ss(NameServerConfig *config) {
    log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
               "Finding available SS for file creation (round-robin)");
    
    pthread_mutex_lock(&config->ss_session_lock);

    int total_sessions = config->ss_session_count;
    
    log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
               "SS session count: total=%d", total_sessions);
    
    printf("  [DEBUG] Total SS sessions: %d\n", total_sessions);

    if (total_sessions == 0) {
        log_message(log_file, LOG_LEVEL_WARNING, NULL, 0, NULL, 
                   "No storage servers available (session_count=0)");
        pthread_mutex_unlock(&config->ss_session_lock);
        return -1;
    }

    // Build array of active SS IDs
    int active_ss[MAX_STORAGE_SERVERS];
    int active_count = 0;
    int inactive_count = 0;

    log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
               "Building active SS list from session chain");

    SSSession *current = config->ss_sessions;
    int session_index = 0;
    
    while (current) {
        session_index++;
        
        log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
                   "Examining SS session [%d]: ss_id=%d, is_active=%d, ip=%s:%d", 
                   session_index, current->ss_id, current->is_active, 
                   current->ip, current->client_port);
        
        printf("  [DEBUG] Checking SS#%d: is_active=%d\n", current->ss_id, current->is_active);
        
        if (current->is_active) {
            if (active_count < MAX_STORAGE_SERVERS) {
                active_ss[active_count] = current->ss_id;
                active_count++;
                
                log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
                           "Added to active list: ss_id=%d, position=%d, ip=%s", 
                           current->ss_id, active_count - 1, current->ip);
                
                printf("  [DEBUG] Added SS#%d to active list (count=%d)\n", 
                       current->ss_id, active_count);
            } else {
                log_message(log_file, LOG_LEVEL_WARNING, NULL, 0, NULL, 
                           "Active SS array full: ss_id=%d skipped (max=%d)", 
                           current->ss_id, MAX_STORAGE_SERVERS);
            }
        } else {
            inactive_count++;
            log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
                       "Skipping inactive SS: ss_id=%d", current->ss_id);
        }
        
        current = current->next;
    }

    log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
               "SS availability scan complete: total_sessions=%d, active=%d, inactive=%d", 
               total_sessions, active_count, inactive_count);
    
    printf("  [DEBUG] Total active SS: %d\n", active_count);

    if (active_count == 0) {
        log_message(log_file, LOG_LEVEL_WARNING, NULL, 0, NULL, 
                   "No active storage servers available (active_count=0, total_sessions=%d)", 
                   total_sessions);
        pthread_mutex_unlock(&config->ss_session_lock);
        return -1;
    }

    // Round robin selection
    int previous_index = last_assigned_ss;
    last_assigned_ss = (last_assigned_ss + 1) % active_count;
    int selected_ss_id = active_ss[last_assigned_ss];

    pthread_mutex_unlock(&config->ss_session_lock);

    log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
               "Round-robin selection: ss_id=%d, index=%d (previous_index=%d), active_pool_size=%d", 
               selected_ss_id, last_assigned_ss, previous_index, active_count);
    
    printf("  → Round-robin selected SS#%d (out of %d active)\n", selected_ss_id, active_count);
    
    // Log the complete active SS pool for debugging load balancing
    if (active_count > 1) {
        char active_list[256] = "";
        for (int i = 0; i < active_count; i++) {
            char buf[16];
            snprintf(buf, sizeof(buf), "%d%s", active_ss[i], (i < active_count - 1) ? "," : "");
            strncat(active_list, buf, sizeof(active_list) - strlen(active_list) - 1);
        }
        log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
                   "Active SS pool: [%s]", active_list);
    }
    
    return selected_ss_id;
}
