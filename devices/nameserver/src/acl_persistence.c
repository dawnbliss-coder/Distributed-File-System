#include "../include/nameserver.h"

#define ACL_CACHE_FILE ".ns_acl_cache.dat"

// Save ACL to disk
int save_acl_cache(AccessControlManager *acl_mgr) {
    FILE *fp = fopen(ACL_CACHE_FILE, "w");
    if (!fp) {
        perror("Failed to save ACL cache");
        return -1;
    }

    pthread_mutex_lock(&acl_mgr->acl_lock);

    int saved_count = 0;

    // Iterate through ACL list
    for (int i = 0; i < acl_mgr->acl_count; i++) {
        FileAccessControl *acl = &acl_mgr->acl_list[i];

        // Format: filename|owner|user1:access1,user2:access2,...
        fprintf(fp, "%s|%s|", acl->filename, acl->owner);

        // Write access list for users
        for (int j = 0; j < acl->user_count; j++) {
            fprintf(fp, "%s:%d", acl->users[j], acl->access_levels[j]);
            if (j < acl->user_count - 1) {
                fprintf(fp, ",");
            }
        }
        fprintf(fp, "\n");
        saved_count++;
    }

    pthread_mutex_unlock(&acl_mgr->acl_lock);
    fclose(fp);

    printf("  → Saved %d ACL entries to cache\n", saved_count);
    return 0;
}

// Load ACL from disk
int load_acl_cache(AccessControlManager *acl_mgr) {
    FILE *fp = fopen(ACL_CACHE_FILE, "r");
    if (!fp) {
        printf("  → No ACL cache found (first run or clean start)\n");
        return 0;  // Not an error - just no cache
    }

    char line[BUFFER_SIZE];
    int restored = 0;

    while (fgets(line, sizeof(line), fp)) {
        // Remove newline
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';

        // Skip empty lines
        if (strlen(line) == 0) continue;

        // Parse: filename|owner|user1:access1,user2:access2
        char *saveptr;
        char *filename = strtok_r(line, "|", &saveptr);
        char *owner = strtok_r(NULL, "|", &saveptr);
        char *access_list = strtok_r(NULL, "|", &saveptr);

        if (!filename || !owner) continue;

        // Add file with owner (this creates the ACL entry)
        add_file_access(acl_mgr, filename, owner);

        // Parse and add other users' access
        if (access_list && strlen(access_list) > 0) {
            char access_copy[BUFFER_SIZE];
            strncpy(access_copy, access_list, sizeof(access_copy) - 1);
            access_copy[sizeof(access_copy) - 1] = '\0';

            char *user_access = strtok(access_copy, ",");
            while (user_access) {
                char *colon = strchr(user_access, ':');
                if (colon) {
                    *colon = '\0';
                    char *username = user_access;
                    int access_level = atoi(colon + 1);

                    // Grant access to this user
                    grant_access(acl_mgr, filename, username, access_level);
                }
                user_access = strtok(NULL, ",");
            }
        }

        restored++;
    }

    fclose(fp);
    printf("  → Restored %d ACL entries from cache\n", restored);
    return restored;
}