// ############## LLM Generated Code Begins ##############


#ifndef CLIENT_H
#define CLIENT_H

#include "../../common/common.h"

// Client-specific constants
#define CLIENT_VERSION "1.0.0"
#define PROMPT_FORMAT "%s@%s> "

// Client structure
typedef struct {
    char username[MAX_USERNAME_LENGTH];
    char nm_ip[INET_ADDRSTRLEN];
    int nm_port;
    int client_port;
    int nm_socket;
    int is_connected;
    time_t connected_time;
} Client;

// Function declarations
int client_init(Client *client, const char *nm_ip, int nm_port, const char *username);
void client_cleanup(Client *client);
int send_to_nameserver(Client *client, const char *message, char *response, size_t response_size);
int connect_to_storage_server(const char *ss_ip, int ss_port);

// Command handlers
void handle_view(Client *client, const char *flags);
void handle_read(Client *client, const char *filename);
void handle_create(Client *client, const char *filename);
void handle_write(Client *client, const char *filename, int sentence_num);
void handle_undo(Client *client, const char *filename);
void handle_info(Client *client, const char *filename);
void handle_delete(Client *client, const char *filename);
void handle_stream(Client *client, const char *filename);
void handle_list(Client *client);
void handle_addaccess(Client *client, const char *access_type, const char *filename, const char *target_user);
void handle_remaccess(Client *client, const char *filename, const char *target_user);
void handle_exec(Client *client, const char *filename);


// Utility functions
void parse_command(char *input, char **tokens, int *token_count);
void print_error(const char *message);
void print_success(const char *message);
void print_help(void);
int receive_full_message(int socket_fd, char *buffer, size_t buffer_size);
int send_full_message(int socket_fd, const char *message);

#endif // CLIENT_H

// ############## LLM Generated Code Ends ################
