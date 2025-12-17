#ifndef NAMESERVER_H
#define NAMESERVER_H
#include<sys/time.h>

#include "../../common/common.h"

#define LOG_FILE ".nslogs"
extern FILE* log_file;


// ============================================================================
// CLIENT SESSION STRUCTURES
// ============================================================================

typedef struct ClientSession {
    int socket_fd;
    char username[MAX_USERNAME_LENGTH];
    char ip[INET_ADDRSTRLEN];
    int port;
    int is_active;
    time_t connected_time;
    pthread_t thread;
    struct ClientSession *next;
} ClientSession;

// ============================================================================
// STORAGE SERVER SESSION STRUCTURES
// ============================================================================

typedef struct SSSession {
    int ss_id;
    int socket_fd;
    char ip[INET_ADDRSTRLEN];
    int nm_port;
    int client_port;
    int is_active;
    time_t last_heartbeat;
    pthread_t thread;
    struct SSSession *next;
} SSSession;

// ============================================================================
// FILE MAPPING STRUCTURES
// ============================================================================

typedef struct FileMapping {
    char filename[MAX_FILENAME_LENGTH];
    int primary_ss_id;
    struct FileMapping *next;
} FileMapping;

#define HASH_TABLE_SIZE 1009

typedef struct {
    FileMapping *buckets[HASH_TABLE_SIZE];
    pthread_mutex_t lock;
} FileHashTable;

// ============================================================================
// ACCESS CONTROL STRUCTURES
// ============================================================================

typedef struct {
    char filename[MAX_FILENAME_LENGTH];
    char owner[MAX_USERNAME_LENGTH];
    char users[MAX_USERS][MAX_USERNAME_LENGTH];
    int access_levels[MAX_USERS];
    int user_count;
} FileAccessControl;

typedef struct {
    FileAccessControl acl_list[MAX_FILES_PER_SS * MAX_STORAGE_SERVERS];
    int acl_count;
    pthread_mutex_t acl_lock;
} AccessControlManager;

// ============================================================================
// NAME SERVER CONFIGURATION
// ============================================================================

typedef struct {
    int nm_port;
    int client_port;
    int is_running;
    
    SSSession *ss_sessions;
    int ss_session_count;
    pthread_mutex_t ss_session_lock;
    
    ClientSession *client_sessions;
    int client_session_count;
    pthread_mutex_t client_session_lock;
    
    FileHashTable file_table;
    AccessControlManager acl_manager;
    
    int nm_socket;
    int client_socket;
    
    pthread_t nm_accept_thread;
    pthread_t client_accept_thread;
    pthread_t heartbeat_thread;
} NameServerConfig;

typedef struct {
    ClientSession *session;
    NameServerConfig *config;
} ClientThreadArg;

// ============================================================================
// SS SESSION MANAGEMENT
// ============================================================================

SSSession* create_ss_session(int socket_fd, int ss_id, const char *ip, 
                             int nm_port, int client_port);
int add_ss_session(NameServerConfig *config, SSSession *session);
int remove_ss_session(NameServerConfig *config, int ss_id);
SSSession* find_ss_session(NameServerConfig *config, int ss_id);
int find_available_ss(NameServerConfig *config);
void handle_ss_failure(NameServerConfig *config, int failed_ss_id);
void* monitor_ss_heartbeats(void *arg);
void* handle_ss_session(void *arg);
void handle_ss_session_command(SSSession *session, NameServerConfig *config, const char *command);

// ============================================================================
// CLIENT SESSION MANAGEMENT
// ============================================================================

ClientSession* create_client_session(int socket_fd, const char *username, 
                                     const char *ip, int port);
int add_client_session(NameServerConfig *config, ClientSession *session);
int remove_client_session(NameServerConfig *config, const char *username);
ClientSession* find_client_session(NameServerConfig *config, const char *username);
void cleanup_all_sessions(NameServerConfig *config);
void* handle_client_session(void *arg);
void handle_session_command(ClientSession *session, NameServerConfig *config, 
                           const char *command);

// ============================================================================
// INITIALIZATION
// ============================================================================

int init_nameserver(NameServerConfig *config, int nm_port, int client_port);
void cleanup_nameserver(NameServerConfig *config);

// ============================================================================
// HASH TABLE OPERATIONS
// ============================================================================

unsigned int hash_filename(const char *filename);
int add_file_mapping(FileHashTable *table, const char *filename, int primary_ss_id);
int get_file_primary_ss(FileHashTable *table, const char *filename);
int remove_file_mapping(FileHashTable *table, const char *filename);
void init_hash_table(FileHashTable *table);
void cleanup_hash_table(FileHashTable *table);

// ============================================================================
// ACCESS CONTROL
// ============================================================================

int init_access_control(AccessControlManager *acl_mgr);
int add_file_access(AccessControlManager *acl_mgr, const char *filename, 
                   const char *owner);
int grant_access(AccessControlManager *acl_mgr, const char *filename, 
                const char *username, int access_level);
int revoke_access(AccessControlManager *acl_mgr, const char *filename, 
                 const char *username);
int check_access(AccessControlManager *acl_mgr, const char *filename, 
                const char *username, int required_level);
FileAccessControl* get_file_acl(AccessControlManager *acl_mgr, const char *filename);

// ============================================================================
// NETWORK THREADS
// ============================================================================

void* accept_storage_server_connections(void *arg);
void* accept_client_connections(void *arg);

// ACL persistence
int save_acl_cache(AccessControlManager *acl_mgr);
int load_acl_cache(AccessControlManager *acl_mgr);


#endif // NAMESERVER_H