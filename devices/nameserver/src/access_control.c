#include "../include/nameserver.h"

// External log file handle
extern FILE* log_file;

int init_access_control(AccessControlManager *acl_mgr) {
    log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
               "Initializing access control manager");
    
    acl_mgr->acl_count = 0;
    pthread_mutex_init(&acl_mgr->acl_lock, NULL);
    
    log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
               "Access control manager initialized: acl_count=0");
    
    return ERR_SUCCESS;
}

int add_file_access(AccessControlManager *acl_mgr, const char *filename, 
                   const char *owner) {
    log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
               "Adding file access: filename='%s', owner='%s'", filename, owner);
    
    pthread_mutex_lock(&acl_mgr->acl_lock);
    
    int current_count = acl_mgr->acl_count;
    int max_capacity = MAX_FILES_PER_SS * MAX_STORAGE_SERVERS;
    
    log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
               "ACL capacity check: current=%d, max=%d", current_count, max_capacity);
    
    if (current_count >= max_capacity) {
        log_message(log_file, LOG_LEVEL_ERROR, NULL, 0, NULL, 
                   "ACL capacity exceeded: filename='%s', current=%d, max=%d", 
                   filename, current_count, max_capacity);
        pthread_mutex_unlock(&acl_mgr->acl_lock);
        return ERR_MAX_FILES_REACHED;
    }
    
    // Check if already exists
    for (int i = 0; i < current_count; i++) {
        if (strcmp(acl_mgr->acl_list[i].filename, filename) == 0) {
            log_message(log_file, LOG_LEVEL_WARNING, NULL, 0, NULL, 
                       "ACL already exists: filename='%s', existing_owner='%s', requested_owner='%s'", 
                       filename, acl_mgr->acl_list[i].owner, owner);
            pthread_mutex_unlock(&acl_mgr->acl_lock);
            return ERR_FILE_ALREADY_EXISTS;
        }
    }
    
    log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
               "No existing ACL found, creating new entry: filename='%s'", filename);
    
    // Add new ACL
    FileAccessControl *acl = &acl_mgr->acl_list[acl_mgr->acl_count];
    strncpy(acl->filename, filename, MAX_FILENAME_LENGTH - 1);
    acl->filename[MAX_FILENAME_LENGTH - 1] = '\0';
    strncpy(acl->owner, owner, MAX_USERNAME_LENGTH - 1);
    acl->owner[MAX_USERNAME_LENGTH - 1] = '\0';
    
    // Owner gets full access
    strncpy(acl->users[0], owner, MAX_USERNAME_LENGTH - 1);
    acl->users[0][MAX_USERNAME_LENGTH - 1] = '\0';
    acl->access_levels[0] = ACCESS_OWNER;
    acl->user_count = 1;
    
    acl_mgr->acl_count++;
    
    pthread_mutex_unlock(&acl_mgr->acl_lock);
    
    log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
               "ACL created: filename='%s', owner='%s', total_acls=%d", 
               filename, owner, acl_mgr->acl_count);
    
    printf("  ✓ ACL created for '%s' (owner: %s)\n", filename, owner);
    return ERR_SUCCESS;
}

int grant_access(AccessControlManager *acl_mgr, const char *filename, 
                const char *username, int access_level) {
    const char *level_str = (access_level == ACCESS_READ) ? "READ" : 
                           (access_level == ACCESS_WRITE) ? "WRITE" : 
                           (access_level == ACCESS_READ_WRITE) ? "READ_WRITE" : "UNKNOWN";
    
    log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
               "Granting access: filename='%s', username='%s', level=%s(%d)", 
               filename, username, level_str, access_level);
    
    pthread_mutex_lock(&acl_mgr->acl_lock);
    
    FileAccessControl *acl = NULL;
    int acl_index = -1;
    
    for (int i = 0; i < acl_mgr->acl_count; i++) {
        if (strcmp(acl_mgr->acl_list[i].filename, filename) == 0) {
            acl = &acl_mgr->acl_list[i];
            acl_index = i;
            break;
        }
    }
    
    if (!acl) {
        log_message(log_file, LOG_LEVEL_WARNING, NULL, 0, NULL, 
                   "Grant access failed: ACL not found for filename='%s'", filename);
        pthread_mutex_unlock(&acl_mgr->acl_lock);
        return ERR_FILE_NOT_FOUND;
    }
    
    log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
               "ACL found: filename='%s', index=%d, owner='%s', user_count=%d", 
               filename, acl_index, acl->owner, acl->user_count);
    
    // Check if user already has access - update level
    for (int i = 0; i < acl->user_count; i++) {
        if (strcmp(acl->users[i], username) == 0) {
            int old_level = acl->access_levels[i];
            const char *old_level_str = (old_level == ACCESS_READ) ? "READ" : 
                                       (old_level == ACCESS_WRITE) ? "WRITE" : 
                                       (old_level == ACCESS_READ_WRITE) ? "READ_WRITE" : 
                                       (old_level == ACCESS_OWNER) ? "OWNER" : "UNKNOWN";
            
            acl->access_levels[i] = access_level;
            
            log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
                       "Access level updated: filename='%s', username='%s', old=%s(%d), new=%s(%d)", 
                       filename, username, old_level_str, old_level, level_str, access_level);
            
            pthread_mutex_unlock(&acl_mgr->acl_lock);
            return ERR_SUCCESS;
        }
    }
    
    // Add new user
    if (acl->user_count >= MAX_USERS) {
        log_message(log_file, LOG_LEVEL_ERROR, NULL, 0, NULL, 
                   "Grant access failed: Max users reached for filename='%s' (max=%d)", 
                   filename, MAX_USERS);
        pthread_mutex_unlock(&acl_mgr->acl_lock);
        return ERR_MAX_CLIENTS_REACHED;
    }
    
    int user_index = acl->user_count;
    strncpy(acl->users[user_index], username, MAX_USERNAME_LENGTH - 1);
    acl->users[user_index][MAX_USERNAME_LENGTH - 1] = '\0';
    acl->access_levels[user_index] = access_level;
    acl->user_count++;
    
    log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
               "Access granted: filename='%s', username='%s', level=%s(%d), user_count=%d", 
               filename, username, level_str, access_level, acl->user_count);
    
    pthread_mutex_unlock(&acl_mgr->acl_lock);
    return ERR_SUCCESS;
}

int revoke_access(AccessControlManager *acl_mgr, const char *filename, 
                 const char *username) {
    log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
               "Revoking access: filename='%s', username='%s'", filename, username);
    
    pthread_mutex_lock(&acl_mgr->acl_lock);
    
    FileAccessControl *acl = NULL;
    int acl_index = -1;
    
    for (int i = 0; i < acl_mgr->acl_count; i++) {
        if (strcmp(acl_mgr->acl_list[i].filename, filename) == 0) {
            acl = &acl_mgr->acl_list[i];
            acl_index = i;
            break;
        }
    }
    
    if (!acl) {
        log_message(log_file, LOG_LEVEL_WARNING, NULL, 0, NULL, 
                   "Revoke access failed: ACL not found for filename='%s'", filename);
        pthread_mutex_unlock(&acl_mgr->acl_lock);
        return ERR_FILE_NOT_FOUND;
    }
    
    log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
               "ACL found: filename='%s', index=%d, user_count=%d", 
               filename, acl_index, acl->user_count);
    
    // Find and remove user (shift array)
    for (int i = 0; i < acl->user_count; i++) {
        if (strcmp(acl->users[i], username) == 0) {
            int user_level = acl->access_levels[i];
            
            // Don't remove owner
            if (user_level == ACCESS_OWNER) {
                log_message(log_file, LOG_LEVEL_WARNING, NULL, 0, NULL, 
                           "Revoke access denied: Cannot revoke owner access - filename='%s', username='%s'", 
                           filename, username);
                pthread_mutex_unlock(&acl_mgr->acl_lock);
                return ERR_PERMISSION_DENIED;
            }
            
            const char *level_str = (user_level == ACCESS_READ) ? "READ" : 
                                   (user_level == ACCESS_WRITE) ? "WRITE" : 
                                   (user_level == ACCESS_READ_WRITE) ? "READ_WRITE" : "UNKNOWN";
            
            log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
                       "Removing user: filename='%s', username='%s', level=%s(%d), position=%d", 
                       filename, username, level_str, user_level, i);
            
            // Shift remaining users
            int shifted_count = 0;
            for (int j = i; j < acl->user_count - 1; j++) {
                strcpy(acl->users[j], acl->users[j + 1]);
                acl->access_levels[j] = acl->access_levels[j + 1];
                shifted_count++;
            }
            acl->user_count--;
            
            log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL, 
                       "Access revoked: filename='%s', username='%s', shifted=%d users, new_count=%d", 
                       filename, username, shifted_count, acl->user_count);
            
            pthread_mutex_unlock(&acl_mgr->acl_lock);
            return ERR_SUCCESS;
        }
    }
    
    log_message(log_file, LOG_LEVEL_WARNING, NULL, 0, NULL, 
               "Revoke access failed: User not found in ACL - filename='%s', username='%s'", 
               filename, username);
    
    pthread_mutex_unlock(&acl_mgr->acl_lock);
    return ERR_USER_NOT_FOUND;
}

int check_access(AccessControlManager *acl_mgr, const char *filename, 
                const char *username, int required_level) {
    const char *required_str = (required_level == ACCESS_READ) ? "READ" : 
                              (required_level == ACCESS_WRITE) ? "WRITE" : 
                              (required_level == ACCESS_READ_WRITE) ? "READ_WRITE" : "UNKNOWN";
    
    log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
               "Checking access: filename='%s', username='%s', required_level=%s(%d)", 
               filename, username, required_str, required_level);
    
    pthread_mutex_lock(&acl_mgr->acl_lock);
    
    FileAccessControl *acl = NULL;
    int search_count = 0;
    
    for (int i = 0; i < acl_mgr->acl_count; i++) {
        search_count++;
        if (strcmp(acl_mgr->acl_list[i].filename, filename) == 0) {
            acl = &acl_mgr->acl_list[i];
            break;
        }
    }
    
    if (!acl) {
        log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
                   "Access denied: No ACL found - filename='%s', searched=%d entries", 
                   filename, search_count);
        pthread_mutex_unlock(&acl_mgr->acl_lock);
        return 0;  // No ACL = no access
    }
    
    log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
               "ACL found: filename='%s', owner='%s', user_count=%d", 
               filename, acl->owner, acl->user_count);
    
    // Check user access
    for (int i = 0; i < acl->user_count; i++) {
        if (strcmp(acl->users[i], username) == 0) {
            int user_level = acl->access_levels[i];
            const char *user_level_str = (user_level == ACCESS_READ) ? "READ" : 
                                        (user_level == ACCESS_WRITE) ? "WRITE" : 
                                        (user_level == ACCESS_READ_WRITE) ? "READ_WRITE" : 
                                        (user_level == ACCESS_OWNER) ? "OWNER" : "UNKNOWN";
            
            pthread_mutex_unlock(&acl_mgr->acl_lock);
            
            // ACCESS_OWNER (3) >= ACCESS_WRITE (2) >= ACCESS_READ (1)
            int has_access = (user_level >= required_level);
            
            if (has_access) {
                log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
                           "Access granted: filename='%s', username='%s', has=%s(%d), required=%s(%d)", 
                           filename, username, user_level_str, user_level, required_str, required_level);
            } else {
                log_message(log_file, LOG_LEVEL_WARNING, NULL, 0, NULL, 
                           "Access denied: Insufficient permissions - filename='%s', username='%s', has=%s(%d), required=%s(%d)", 
                           filename, username, user_level_str, user_level, required_str, required_level);
            }
            
            return has_access;
        }
    }
    
    log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
               "Access denied: User not in ACL - filename='%s', username='%s'", 
               filename, username);
    
    pthread_mutex_unlock(&acl_mgr->acl_lock);
    return 0;  // User not in ACL
}

FileAccessControl* get_file_acl(AccessControlManager *acl_mgr, const char *filename) {
    log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
               "Getting file ACL: filename='%s'", filename);
    
    pthread_mutex_lock(&acl_mgr->acl_lock);
    
    int search_count = 0;
    
    for (int i = 0; i < acl_mgr->acl_count; i++) {
        search_count++;
        if (strcmp(acl_mgr->acl_list[i].filename, filename) == 0) {
            log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
                       "ACL found: filename='%s', owner='%s', user_count=%d, searched=%d", 
                       filename, acl_mgr->acl_list[i].owner, 
                       acl_mgr->acl_list[i].user_count, search_count);
            pthread_mutex_unlock(&acl_mgr->acl_lock);
            return &acl_mgr->acl_list[i];
        }
    }
    
    log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL, 
               "ACL not found: filename='%s', searched=%d entries", filename, search_count);
    
    pthread_mutex_unlock(&acl_mgr->acl_lock);
    return NULL;
}
