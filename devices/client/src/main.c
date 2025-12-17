// ############## FIXED CLIENT CODE (NO BONUS) ##############

#include "client.h"
#include <signal.h>
#include<sys/time.h>

// Global client instance
Client g_client;

// ============================================================================
// INITIALIZATION AND CLEANUP
// ============================================================================

void signal_handler(int signum) {
    printf("\nReceived signal, shutting down client...\n");
    client_cleanup(&g_client);
    exit(0);
}

int client_init(Client *client, const char *nm_ip, int nm_port, const char *username) {
    struct sockaddr_in nm_addr;
    
    // Validate username using common function
    if (!is_valid_username(username)) {
        print_error(get_error_message(ERR_INVALID_USERNAME));
        return ERR_INVALID_USERNAME;
    }
    
    // Initialize client structure
    strncpy(client->username, username, MAX_USERNAME_LENGTH - 1);
    client->username[MAX_USERNAME_LENGTH - 1] = '\0';
    strncpy(client->nm_ip, nm_ip, INET_ADDRSTRLEN - 1);
    client->nm_ip[INET_ADDRSTRLEN - 1] = '\0';
    client->nm_port = nm_port;
    client->is_connected = 0;
    client->connected_time = time(NULL);
    
    // Create socket
    client->nm_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client->nm_socket < 0) {
        perror("Socket creation failed");
        return ERR_SOCKET_CREATE_FAILED;
    }
    
    // Set socket options
    int opt = 1;
    if (setsockopt(client->nm_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("Setsockopt failed");
        close(client->nm_socket);
        return ERR_CONNECTION_FAILED;
    }
    
    // Set socket timeout
    struct timeval timeout;
    timeout.tv_sec = CONNECTION_TIMEOUT_SEC;
    timeout.tv_usec = 0;
    if (setsockopt(client->nm_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
        perror("Warning: Could not set receive timeout");
    }
    
    if (setsockopt(client->nm_socket, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) < 0) {
        perror("Warning: Could not set send timeout");
    }
    
    // Configure nameserver address
    memset(&nm_addr, 0, sizeof(nm_addr));
    nm_addr.sin_family = AF_INET;
    nm_addr.sin_port = htons(nm_port);
    
    if (inet_pton(AF_INET, nm_ip, &nm_addr.sin_addr) <= 0) {
        print_error(get_error_message(ERR_CONNECTION_FAILED));
        close(client->nm_socket);
        return ERR_CONNECTION_FAILED;
    }
    
    // Connect to nameserver
    printf("Connecting to nameserver at %s:%d...\n", nm_ip, nm_port);
    if (connect(client->nm_socket, (struct sockaddr *)&nm_addr, sizeof(nm_addr)) < 0) {
        perror("Connection to nameserver failed");
        close(client->nm_socket);
        return ERR_CONNECT_FAILED;
    }
    
    client->is_connected = 1;
    
    // Send registration message using MSG_REGISTER_CLIENT
    // Format: REGISTER_CLIENT|username|client_ip|client_port
    char init_msg[BUFFER_SIZE];
    snprintf(init_msg, BUFFER_SIZE, "%s%s%s%s%s%s%d", 
             MSG_REGISTER_CLIENT, PROTOCOL_DELIMITER, username,
             PROTOCOL_DELIMITER, "127.0.0.1",
             PROTOCOL_DELIMITER, 0);
    
    char response[BUFFER_SIZE];
    if (send_to_nameserver(client, init_msg, response, BUFFER_SIZE) < 0) {
        print_error(get_error_message(ERR_INITIALIZATION_FAILED));
        client_cleanup(client);
        return ERR_INITIALIZATION_FAILED;
    }
    
    // Parse response - should be SUCCESS or ERROR or ACK
    if (strncmp(response, MSG_SUCCESS, strlen(MSG_SUCCESS)) == 0) {
        printf("✓ Connected to nameserver as '%s'\n", username);
        printf("✓ Client initialized successfully\n\n");
        return ERR_SUCCESS;
    } else if (strncmp(response, MSG_ERROR, strlen(MSG_ERROR)) == 0) {
        print_error(response + strlen(MSG_ERROR) + 1);
        client_cleanup(client);
        return ERR_INITIALIZATION_FAILED;
    } else if (strncmp(response, MSG_ACK, strlen(MSG_ACK)) == 0) {
        printf("✓ Connected to nameserver as '%s'\n", username);
        printf("✓ Client initialized successfully\n\n");
        return ERR_SUCCESS;
    }
    
    return ERR_SUCCESS;
}

void client_cleanup(Client *client) {
    if (client->is_connected) {
        // Send disconnect message
        char disconnect_msg[BUFFER_SIZE];
        snprintf(disconnect_msg, BUFFER_SIZE, "%s%s%s",
                 MSG_DISCONNECT, PROTOCOL_DELIMITER, client->username);
        send(client->nm_socket, disconnect_msg, strlen(disconnect_msg), 0);
        close(client->nm_socket);
        client->is_connected = 0;
    }
}

// ============================================================================
// COMMUNICATION UTILITIES
// ============================================================================

int send_to_nameserver(Client *client, const char *message, char *response, size_t response_size) {
    if (!client->is_connected) {
        return ERR_CONNECTION_FAILED;
    }
    
    if (send_full_message(client->nm_socket, message) < 0) {
        return ERR_SEND_FAILED;
    }
    
    if (receive_full_message(client->nm_socket, response, response_size) < 0) {
        return ERR_RECV_FAILED;
    }
    
    return ERR_SUCCESS;
}

int connect_to_storage_server(const char *ss_ip, int ss_port) {
    int ss_socket;
    struct sockaddr_in ss_addr;
    
    ss_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (ss_socket < 0) {
        perror("Storage server socket creation failed");
        return -1;
    }
    
    // Set socket timeout
    struct timeval timeout;
    timeout.tv_sec = CONNECTION_TIMEOUT_SEC;
    timeout.tv_usec = 0;
    setsockopt(ss_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(ss_socket, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    
    memset(&ss_addr, 0, sizeof(ss_addr));
    ss_addr.sin_family = AF_INET;
    ss_addr.sin_port = htons(ss_port);
    
    if (inet_pton(AF_INET, ss_ip, &ss_addr.sin_addr) <= 0) {
        perror("Invalid storage server address");
        close(ss_socket);
        return -1;
    }
    
    if (connect(ss_socket, (struct sockaddr *)&ss_addr, sizeof(ss_addr)) < 0) {
        perror("Connection to storage server failed");
        close(ss_socket);
        return -1;
    }
    
    return ss_socket;
}

int send_full_message(int socket_fd, const char *message) {
    size_t total_sent = 0;
    size_t message_len = strlen(message);
    
    while (total_sent < message_len) {
        ssize_t bytes_sent = send(socket_fd, message + total_sent, 
                                   message_len - total_sent, 0);
        if (bytes_sent < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("Send failed");
            return -1;
        }
        total_sent += bytes_sent;
    }
    
    return total_sent;
}

int receive_full_message(int socket_fd, char *buffer, size_t buffer_size) {
    memset(buffer, 0, buffer_size);
    ssize_t bytes_received = recv(socket_fd, buffer, buffer_size - 1, 0);
    
    if (bytes_received < 0) {
        if (errno == EINTR) {
            return receive_full_message(socket_fd, buffer, buffer_size);
        }
        perror("Receive failed");
        return -1;
    }
    
    if (bytes_received == 0) {
        fprintf(stderr, "Connection closed by peer\n");
        return -1;
    }
    
    buffer[bytes_received] = '\0';
    return bytes_received;
}

// ============================================================================
// COMMAND HANDLERS
// ============================================================================

typedef struct {
    char filename[256];
    int words;
    int chars;
    char accessed[20];
    char owner[64];
} FileInfo;

static int parse_info_response(const char *response, FileInfo *info) {
    const char *pos;

    pos = strstr(response, "Filename:");
    if (!pos || sscanf(pos, "Filename: %255[^\n]", info->filename) != 1) return -1;

    pos = strstr(response, "Words:");
    if (!pos || sscanf(pos, "Words: %d", &info->words) != 1) return -1;

    pos = strstr(response, "Characters:");
    if (!pos || sscanf(pos, "Characters: %d", &info->chars) != 1) return -1;

    pos = strstr(response, "Accessed:");
    if (!pos || sscanf(pos, "Accessed: %19[^\n]", info->accessed) != 1) return -1;

    pos = strstr(response, "Owner:");
    if (!pos || sscanf(pos, "Owner: %63[^\n]", info->owner) != 1) return -1;

    // Trim accessed time seconds for table fit (YYYY-MM-DD HH:MM)
    if (strlen(info->accessed) >= 16) {
        info->accessed[16] = '\0';
    }

    return 0;
}

static int get_info_response(Client *client, const char *filename, char *buffer, size_t buffer_size) {
    char request[BUFFER_SIZE];
    snprintf(request, BUFFER_SIZE, "%s%s%s", MSG_INFO, PROTOCOL_DELIMITER, filename);
    if (send_to_nameserver(client, request, buffer, buffer_size) < 0) {
        return -1;
    }
    if (strncmp(buffer, MSG_ERROR, strlen(MSG_ERROR)) == 0) {
        return -1;
    }
    return 0;
}

void handle_view(Client *client, const char *flags) {
    char request[BUFFER_SIZE];
    char response[LARGE_BUFFER_SIZE];
    // printf("Flags: %s\n", flags ? flags : "(none)");

    if (flags != NULL) {
        snprintf(request, BUFFER_SIZE, "%s%s%s",
                 MSG_VIEW,
                 PROTOCOL_DELIMITER, flags);
    } else {
        snprintf(request, BUFFER_SIZE, "%s", MSG_VIEW);
    }

    if (send_to_nameserver(client, request, response, sizeof(response)) < 0) {
        print_error("Failed to retrieve file list");
        return;
    }

    if (strncmp(response, MSG_ERROR, strlen(MSG_ERROR)) == 0) {
        print_error(response + strlen(MSG_ERROR) + 1);
        return;
    }

    if (flags != NULL && strstr(flags, "l") != NULL) {
        // Print table header once
        printf("------------------------------------------------------------\n");
        printf("| %-10s | %-5s | %-5s | %-16s | %-6s |\n", 
               "Filename", "Words", "Chars", "Last Access Time", "Owner");
        printf("|------------|-------|-------|------------------|-------|\n");

        char *line = response;
        if (strncmp(line, "SUCCESS|\n", 9) == 0) {
            line += 9;  // skip prefix
        }

        char *file_line = strtok(line, "\n");
        while (file_line != NULL) {
            if (strncmp(file_line, "--> ", 4) == 0) {
                char *filename = file_line + 4;

                // Prepare a buffer for info response
                char info_response[LARGE_BUFFER_SIZE];
                if (get_info_response(client, filename, info_response, sizeof(info_response)) == 0) {
                    // Parse relevant fields and print table row
                    FileInfo info;
                    if (parse_info_response(info_response, &info) == 0) {
                        printf("| %-10s | %5d | %5d | %-16s | %-6s |\n",
                               info.filename, info.words, info.chars, info.accessed, info.owner);
                    } else {
                        print_error("Failed to parse file info");
                    }
                } else {
                    print_error("Failed to get file info");
                }
            }
            file_line = strtok(NULL, "\n");
        }

        printf("------------------------------------------------------------\n");

    } else {
        printf("%s\n", response);
    }
}



void handle_read(Client *client, const char *filename) {
    char request[BUFFER_SIZE];
    char response[BUFFER_SIZE];
    
    // Validate filename
    if (!is_valid_filename(filename)) {
        print_error(get_error_message(ERR_INVALID_FILENAME));
        return;
    }
    
    // Request format: READ|username|filename
    snprintf(request, BUFFER_SIZE, "%s%s%s",
             MSG_READ, PROTOCOL_DELIMITER, filename);
    
    if (send_to_nameserver(client, request, response, BUFFER_SIZE) < 0) {
        print_error("Failed to send read request");
        return;
    }
    
    if (strncmp(response, MSG_ERROR, strlen(MSG_ERROR)) == 0) {
        print_error(response + strlen(MSG_ERROR) + 1);
        return;
    }
    
    // Parse storage server info: SS_IP|SS_PORT or REDIRECT|SS_IP|SS_PORT
    char *tokens[5];
    int token_count = parse_message(response, tokens, 5);
    
    char ss_ip[INET_ADDRSTRLEN];
    int ss_port;
    
    // Handle REDIRECT response
    if (token_count >= 3 && strcmp(tokens[0], MSG_REDIRECT) == 0) {
        strncpy(ss_ip, tokens[1], INET_ADDRSTRLEN - 1);
        ss_ip[INET_ADDRSTRLEN - 1] = '\0';
        ss_port = atoi(tokens[2]);
    } else if (token_count >= 2) {
        strncpy(ss_ip, tokens[0], INET_ADDRSTRLEN - 1);
        ss_ip[INET_ADDRSTRLEN - 1] = '\0';
        ss_port = atoi(tokens[1]);
    } else {
        print_error("Invalid storage server information");
        return;
    }
    
    // Connect to storage server
    int ss_socket = connect_to_storage_server(ss_ip, ss_port);
    if (ss_socket < 0) {
        print_error("Failed to connect to storage server");
        return;
    }
    
    // Send read request to storage server: READ|filename
    snprintf(request, BUFFER_SIZE, "%s%s%s", MSG_READ, PROTOCOL_DELIMITER, filename);
    if (send_full_message(ss_socket, request) < 0) {
        print_error("Failed to send read request to storage server");
        close(ss_socket);
        return;
    }
    
    // Receive file content
    char content[LARGE_BUFFER_SIZE];
    if (receive_full_message(ss_socket, content, sizeof(content)) < 0) {
        print_error("Failed to receive file content");
        close(ss_socket);
        return;
    }
    
    close(ss_socket);
    
    // Parse response: SUCCESS|content or ERROR|message
    if (strncmp(content, MSG_SUCCESS, strlen(MSG_SUCCESS)) == 0) {
        // Extract content after SUCCESS|
        char *file_content = content + strlen(MSG_SUCCESS);
        if (*file_content == '|') file_content++;
        printf("%s\n", file_content);
    } else if (strncmp(content, MSG_ERROR, strlen(MSG_ERROR)) == 0) {
        print_error(content + strlen(MSG_ERROR) + 1);
    } else {
        // If no prefix, assume it's the content
        printf("%s\n", content);
    }
}

void handle_create(Client *client, const char *filename) {
    char request[BUFFER_SIZE];
    char response[BUFFER_SIZE];
    
    // Validate filename
    if (!is_valid_filename(filename)) {
        print_error(get_error_message(ERR_INVALID_FILENAME));
        return;
    }
    
    // Request format: CREATE|filename
    snprintf(request, BUFFER_SIZE, "%s%s%s",
             MSG_CREATE, PROTOCOL_DELIMITER, filename);
    
    if (send_to_nameserver(client, request, response, BUFFER_SIZE) < 0) {
        print_error("Failed to send create request");
        return;
    }
    
    if (strncmp(response, MSG_SUCCESS, strlen(MSG_SUCCESS)) == 0) {
        print_success("File created successfully!");
    } else if (strncmp(response, MSG_ACK, strlen(MSG_ACK)) == 0) {
        print_success("File created successfully!");
    } else if (strncmp(response, MSG_ERROR, strlen(MSG_ERROR)) == 0) {
        print_error(response + strlen(MSG_ERROR) + 1);
    }
}

void handle_write(Client *client, const char *filename, int sentence_num) {
    char request[BUFFER_SIZE];
    char response[BUFFER_SIZE];
    
    // Validate filename
    if (!is_valid_filename(filename)) {
        print_error(get_error_message(ERR_INVALID_FILENAME));
        return;
    }
    
    if (sentence_num < 0) {
        print_error(get_error_message(ERR_SENTENCE_INDEX_OUT_OF_RANGE));
        return;
    }
    
    // Request lock from nameserver: WRITE|filename|sentence_num
    snprintf(request, BUFFER_SIZE, "%s%s%s%s%d",
             MSG_WRITE, PROTOCOL_DELIMITER, filename, PROTOCOL_DELIMITER, sentence_num);
    
    if (send_to_nameserver(client, request, response, BUFFER_SIZE) < 0) {
        print_error("Failed to send write request");
        return;
    }
    
    if (strncmp(response, MSG_ERROR, strlen(MSG_ERROR)) == 0) {
        print_error(response + strlen(MSG_ERROR) + 1);
        return;
    }
    
    // Parse storage server info
    char *tokens[5];
    int token_count = parse_message(response, tokens, 5);
    
    char ss_ip[INET_ADDRSTRLEN];
    int ss_port;
    
    if (token_count >= 3 && strcmp(tokens[0], MSG_REDIRECT) == 0) {
        strncpy(ss_ip, tokens[1], INET_ADDRSTRLEN - 1);
        ss_ip[INET_ADDRSTRLEN - 1] = '\0';
        ss_port = atoi(tokens[2]);
    } else if (token_count >= 2) {
        strncpy(ss_ip, tokens[0], INET_ADDRSTRLEN - 1);
        ss_ip[INET_ADDRSTRLEN - 1] = '\0';
        ss_port = atoi(tokens[1]);
    } else {
        print_error("Invalid storage server information");
        return;
    }
    
    // Connect to storage server
    int ss_socket = connect_to_storage_server(ss_ip, ss_port);
    if (ss_socket < 0) {
        print_error("Failed to connect to storage server");
        return;
    }
    
    // Send write initialization to storage server: WRITE|filename|sentence_num|username
    snprintf(request, BUFFER_SIZE, "%s%s%s%s%d%s%s",
             MSG_WRITE, PROTOCOL_DELIMITER, filename, 
             PROTOCOL_DELIMITER, sentence_num, PROTOCOL_DELIMITER, client->username);
    
    if (send_full_message(ss_socket, request) < 0) {
        print_error("Failed to send write request to storage server");
        close(ss_socket);
        return;
    }
    
    // Receive acknowledgment
    if (receive_full_message(ss_socket, response, BUFFER_SIZE) < 0) {
        print_error("Failed to receive acknowledgment");
        close(ss_socket);
        return;
    }
    
    if (strncmp(response, MSG_ERROR, strlen(MSG_ERROR)) == 0) {
        print_error(response + strlen(MSG_ERROR) + 1);
        close(ss_socket);
        return;
    }
    
    // Accept write commands from user
    char input[MAX_COMMAND_LENGTH];
    printf("Enter write commands (word_index content). Type 'ETIRW' to finish:\n");
    
    while (1) {
        printf("Client: ");
        fflush(stdout);
        
        if (fgets(input, sizeof(input), stdin) == NULL) {
            print_error("Failed to read input");
            break;
        }
        
        // Remove newline
        input[strcspn(input, "\n")] = 0;
        trim_whitespace(input);
        
        if (strcmp(input, MSG_WRITE_END) == 0) {
            // Send finish signal
            if (send_full_message(ss_socket, MSG_WRITE_END) < 0) {
                print_error("Failed to send finish signal");
                close(ss_socket);
                return;
            }
            
            // Receive final response
            if (receive_full_message(ss_socket, response, BUFFER_SIZE) < 0) {
                print_error("Failed to receive response");
                close(ss_socket);
                return;
            }
            
            if (strncmp(response, MSG_SUCCESS, strlen(MSG_SUCCESS)) == 0) {
                print_success("Write successful!");
            } else {
                print_error(response);
            }
            break;
        }
        
        char *p = strchr(input, ' ');  
        if (p != NULL) *p = '|';                
        if (send_full_message(ss_socket, input) < 0) {
            print_error("Failed to send write command");
            close(ss_socket);
            return;
        }
        
        // Receive acknowledgment for each command
        if (receive_full_message(ss_socket, response, BUFFER_SIZE) < 0) {
            print_error("Failed to receive acknowledgment");
            close(ss_socket);
            return;
        }
        
        // Check for errors
        if (strncmp(response, MSG_ERROR, strlen(MSG_ERROR)) == 0) {
            print_error(response + strlen(MSG_ERROR) + 1);
        }
    }
    
    close(ss_socket);
}

void handle_undo(Client *client, const char *filename) {
    char request[BUFFER_SIZE];
    char response[BUFFER_SIZE];
    
    if (!is_valid_filename(filename)) {
        print_error(get_error_message(ERR_INVALID_FILENAME));
        return;
    }
    
    // Request format: UNDO|filename
    snprintf(request, BUFFER_SIZE, "%s%s%s",
             MSG_UNDO, PROTOCOL_DELIMITER, filename);
    
    if (send_to_nameserver(client, request, response, BUFFER_SIZE) < 0) {
        print_error("Failed to send undo request");
        return;
    }
    
    if (strncmp(response, MSG_ERROR, strlen(MSG_ERROR)) == 0) {
        print_error(response + strlen(MSG_ERROR) + 1);
        return;
    }
    
    // Parse storage server info: SS_IP|SS_PORT or REDIRECT|SS_IP|SS_PORT
    char *tokens[5];
    int token_count = parse_message(response, tokens, 5);
    
    char ss_ip[INET_ADDRSTRLEN];
    int ss_port;
    
    // Handle REDIRECT response
    if (token_count >= 3 && strcmp(tokens[0], MSG_REDIRECT) == 0) {
        strncpy(ss_ip, tokens[1], INET_ADDRSTRLEN - 1);
        ss_ip[INET_ADDRSTRLEN - 1] = '\0';
        ss_port = atoi(tokens[2]);
    } else if (token_count >= 2) {
        strncpy(ss_ip, tokens[0], INET_ADDRSTRLEN - 1);
        ss_ip[INET_ADDRSTRLEN - 1] = '\0';
        ss_port = atoi(tokens[1]);
    } else {
        print_error("Invalid storage server information");
        return;
    }
    
    // Connect to storage server
    int ss_socket = connect_to_storage_server(ss_ip, ss_port);
    if (ss_socket < 0) {
        print_error("Failed to connect to storage server");
        return;
    }
    
    snprintf(request, BUFFER_SIZE, "%s%s%s", MSG_UNDO, PROTOCOL_DELIMITER, filename);
    if (send_full_message(ss_socket, request) < 0) {
        print_error("Failed to send undo request to storage server");
        close(ss_socket);
        return;
    }
    
    if (receive_full_message(ss_socket, response, BUFFER_SIZE) < 0) {
        print_error("Failed to receive acknowledgment");
        close(ss_socket);
        return;
    }
    close(ss_socket);
    
    // Parse response: SUCCESS|content or ERROR|message
    
    
    if (strncmp(response, MSG_SUCCESS, strlen(MSG_SUCCESS)) == 0) {
        print_success("Undo successful!");
    } else if (strncmp(response, MSG_ACK, strlen(MSG_ACK)) == 0) {
        print_success("Undo successful!");
    } else if (strncmp(response, MSG_ERROR, strlen(MSG_ERROR)) == 0) {
        print_error(response + strlen(MSG_ERROR) + 1);
    }
}

void handle_info(Client *client, const char *filename) {
    char request[BUFFER_SIZE];
    char response[LARGE_BUFFER_SIZE];
    
    if (!is_valid_filename(filename)) {
        print_error(get_error_message(ERR_INVALID_FILENAME));
        return;
    }
    
    // Request format: INFO|username|filename
    snprintf(request, BUFFER_SIZE, "%s%s%s",
             MSG_INFO, PROTOCOL_DELIMITER, filename);
    
    if (send_to_nameserver(client, request, response, sizeof(response)) < 0) {
        print_error("Failed to send info request");
        return;
    }
    
    if (strncmp(response, MSG_ERROR, strlen(MSG_ERROR)) == 0) {
        print_error(response + strlen(MSG_ERROR) + 1);
    } else {
        printf("%s\n", response);
    }
}

void handle_delete(Client *client, const char *filename) {
    char request[BUFFER_SIZE];
    char response[BUFFER_SIZE];
    
    if (!is_valid_filename(filename)) {
        print_error(get_error_message(ERR_INVALID_FILENAME));
        return;
    }
    
    // Request format: DELETE|filename
    snprintf(request, BUFFER_SIZE, "%s%s%s",
             MSG_DELETE, PROTOCOL_DELIMITER, filename);
    
    if (send_to_nameserver(client, request, response, BUFFER_SIZE) < 0) {
        print_error("Failed to send delete request");
        return;
    }
    
    if (strncmp(response, MSG_SUCCESS, strlen(MSG_SUCCESS)) == 0) {
        printf("File '%s' deleted successfully!\n", filename);
    } else if (strncmp(response, MSG_ACK, strlen(MSG_ACK)) == 0) {
        printf("File '%s' deleted successfully!\n", filename);
    } else if (strncmp(response, MSG_ERROR, strlen(MSG_ERROR)) == 0) {
        print_error(response + strlen(MSG_ERROR) + 1);
    }
}

void handle_stream(Client *client, const char *filename) {
    char request[BUFFER_SIZE];
    char response[BUFFER_SIZE];
    
    if (!is_valid_filename(filename)) {
        print_error(get_error_message(ERR_INVALID_FILENAME));
        return;
    }
    
    // Request format: STREAM|username|filename
    snprintf(request, BUFFER_SIZE, "%s%s%s",
             MSG_STREAM, PROTOCOL_DELIMITER, filename);
    
    if (send_to_nameserver(client, request, response, BUFFER_SIZE) < 0) {
        print_error("Failed to send stream request");
        return;
    }
    
    if (strncmp(response, MSG_ERROR, strlen(MSG_ERROR)) == 0) {
        print_error(response + strlen(MSG_ERROR) + 1);
        return;
    }
    
    // Parse storage server info
    char *tokens[5];
    int token_count = parse_message(response, tokens, 5);
    
    char ss_ip[INET_ADDRSTRLEN];
    int ss_port;
    
    if (token_count >= 3 && strcmp(tokens[0], MSG_REDIRECT) == 0) {
        strncpy(ss_ip, tokens[1], INET_ADDRSTRLEN - 1);
        ss_ip[INET_ADDRSTRLEN - 1] = '\0';
        ss_port = atoi(tokens[2]);
    } else if (token_count >= 2) {
        strncpy(ss_ip, tokens[0], INET_ADDRSTRLEN - 1);
        ss_ip[INET_ADDRSTRLEN - 1] = '\0';
        ss_port = atoi(tokens[1]);
    } else {
        print_error("Invalid storage server information");
        return;
    }
    
    // Connect to storage server
    int ss_socket = connect_to_storage_server(ss_ip, ss_port);
    if (ss_socket < 0) {
        print_error("Failed to connect to storage server");
        return;
    }
    
    // Send stream request: STREAM|filename
    snprintf(request, BUFFER_SIZE, "%s%s%s%s%s", MSG_STREAM, PROTOCOL_DELIMITER, filename, PROTOCOL_DELIMITER, client -> username);
    if (send_full_message(ss_socket, request) < 0) {
        print_error("Failed to send stream request to storage server");
        close(ss_socket);
        return;
    }
    
    // Receive initial response
    if (receive_full_message(ss_socket, response, BUFFER_SIZE) < 0) {
        print_error("Failed to receive response");
        close(ss_socket);
        return;
    }
    
    // Check if streaming started successfully
    if (strncmp(response, MSG_ERROR, strlen(MSG_ERROR)) == 0) {
        print_error(response + strlen(MSG_ERROR) + 1);
        close(ss_socket);
        return;
    }
    
    // Receive and display words with delay
    char word[MAX_WORD_LENGTH];
    
    int first_word = 1;
    
    while (1) {
        memset(word, 0, sizeof(word));
        if (receive_full_message(ss_socket, word, sizeof(word)) < 0) {
            print_error("\nStorage server disconnected during streaming");
            close(ss_socket);
            return;
        }
        
        // Check for STOP or end markers
        if (strcmp(word, MSG_STOP) == 0 || strncmp(word, "STOP", 4) == 0) {
            printf("\n");
            break;
        }
        
        // Check for WORD| prefix from storage server
        if (strncmp(word, "WORD|", 5) == 0) {
            char *word_content = word + 5;
            word_content[strcspn(word_content, "\n")] = '\0';
            
            if (!first_word) {
                printf(" ");
            } else first_word = 0;
            printf("%s", word_content);
            size_t len = strlen(word_content);
            if (len > 0 && is_sentence_delimiter(word_content[len - 1])) {
                printf("\n");
                first_word = 1;
            }
            fflush(stdout);
        } else if (strncmp(word, MSG_ERROR, strlen(MSG_ERROR)) == 0) {
            printf("\n");
            print_error(word + strlen(MSG_ERROR) + 1);
            break;
        } else {
            // No prefix, treat as word
            if (!first_word) {
                printf(" ");
            }
            printf("%s", word);
            fflush(stdout);
            first_word = 0;
        }
    }
    
    close(ss_socket);
}

void handle_list(Client *client) {
    char request[BUFFER_SIZE];
    char response[LARGE_BUFFER_SIZE];
    
    // Request format: LIST or LIST_USERS
    snprintf(request, BUFFER_SIZE, "%s", MSG_LIST);
    
    if (send_to_nameserver(client, request, response, sizeof(response)) < 0) {
        print_error("Failed to send list request");
        return;
    }
    
    if (strncmp(response, MSG_ERROR, strlen(MSG_ERROR)) == 0) {
        print_error(response + strlen(MSG_ERROR) + 1);
    } else {
        printf("%s\n", response);
    }
}

void handle_addaccess(Client *client, const char *access_type, 
                      const char *filename, const char *target_user) {
    char request[BUFFER_SIZE];
    char response[BUFFER_SIZE];
    
    if (!is_valid_filename(filename)) {
        print_error(get_error_message(ERR_INVALID_FILENAME));
        return;
    }
    
    if (!is_valid_username(target_user)) {
        print_error(get_error_message(ERR_INVALID_USERNAME));
        return;
    }
    
    // Request format: ADDACCESS|username|access_type|filename|target_user
    snprintf(request, BUFFER_SIZE, "%s%s%s%s%s%s%s",
             MSG_ADDACCESS, PROTOCOL_DELIMITER, access_type, PROTOCOL_DELIMITER,
             filename, PROTOCOL_DELIMITER, target_user);
    
    if (send_to_nameserver(client, request, response, BUFFER_SIZE) < 0) {
        print_error("Failed to send add access request");
        return;
    }
    
    if (strncmp(response, MSG_SUCCESS, strlen(MSG_SUCCESS)) == 0) {
        print_success("Access granted successfully!");
    } else if (strncmp(response, MSG_ACK, strlen(MSG_ACK)) == 0) {
        print_success("Access granted successfully!");
    } else if (strncmp(response, MSG_ERROR, strlen(MSG_ERROR)) == 0) {
        print_error(response + strlen(MSG_ERROR) + 1);
    }
}

void handle_remaccess(Client *client, const char *filename, const char *target_user) {
    char request[BUFFER_SIZE];
    char response[BUFFER_SIZE];
    
    if (!is_valid_filename(filename)) {
        print_error(get_error_message(ERR_INVALID_FILENAME));
        return;
    }
    
    if (!is_valid_username(target_user)) {
        print_error(get_error_message(ERR_INVALID_USERNAME));
        return;
    }
    
    // Request format: REMACCESS|filename|target_user
    snprintf(request, BUFFER_SIZE, "%s%s%s%s%s",
             MSG_REMACCESS, PROTOCOL_DELIMITER, filename, PROTOCOL_DELIMITER, target_user);
    
    if (send_to_nameserver(client, request, response, BUFFER_SIZE) < 0) {
        print_error("Failed to send remove access request");
        return;
    }
    
    if (strncmp(response, MSG_SUCCESS, strlen(MSG_SUCCESS)) == 0) {
        print_success("Access removed successfully!");
    } else if (strncmp(response, MSG_ACK, strlen(MSG_ACK)) == 0) {
        print_success("Access removed successfully!");
    } else if (strncmp(response, MSG_ERROR, strlen(MSG_ERROR)) == 0) {
        print_error(response + strlen(MSG_ERROR) + 1);
    }
}

void handle_exec(Client *client, const char *filename) {
    char request[BUFFER_SIZE];
    char response[LARGE_BUFFER_SIZE];
    
    if (!is_valid_filename(filename)) {
        print_error(get_error_message(ERR_INVALID_FILENAME));
        return;
    }
    
    // Request format: EXEC|username|filename
    snprintf(request, BUFFER_SIZE, "%s%s%s",
             MSG_EXEC, PROTOCOL_DELIMITER, filename);
    
    if (send_to_nameserver(client, request, response, sizeof(response)) < 0) {
        print_error("Failed to send exec request");
        return;
    }
    
    if (strncmp(response, MSG_ERROR, strlen(MSG_ERROR)) == 0) {
        print_error(response + strlen(MSG_ERROR) + 1);
    } else {
        printf("%s", response);
    }
}

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

void parse_command(char *input, char **tokens, int *token_count) {
    *token_count = 0;
    char *token = strtok(input, " \t");
    while (token != NULL && *token_count < 10) {
        tokens[(*token_count)++] = token;
        token = strtok(NULL, " \t");
    }
}

void print_error(const char *message) {
    fprintf(stderr, "\033[1;31mERROR:\033[0m %s\n", message);
}

void print_success(const char *message) {
    printf("\033[1;32mSUCCESS:\033[0m %s\n", message);
}

void print_help(void) {
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════╗\n");
    printf("║                       Available Commands                          ║\n");
    printf("╠═══════════════════════════════════════════════════════════════════╣\n");
    printf("║ File Operations:                                                  ║\n");
    printf("║   VIEW [-a] [-l]            List files                           ║\n");
    printf("║   READ <filename>           Read file content                    ║\n");
    printf("║   CREATE <filename>         Create new file                      ║\n");
    printf("║   WRITE <file> <sent#>      Write to file at sentence level     ║\n");
    printf("║   DELETE <filename>         Delete file (owner only)             ║\n");
    printf("║   INFO <filename>           Get file information                 ║\n");
    printf("║   UNDO <filename>           Undo last change                     ║\n");
    printf("║   STREAM <filename>         Stream file content word-by-word     ║\n");
    printf("║                                                                   ║\n");
    printf("║ Access Control:                                                   ║\n");
    printf("║   ADDACCESS -R/-W <file> <user>  Grant read/write access        ║\n");
    printf("║   REMACCESS <file> <user>         Remove access                 ║\n");
    printf("║   LIST                            List all users                ║\n");
    printf("║                                                                   ║\n");
    printf("║ Execution:                                                        ║\n");
    printf("║   EXEC <filename>           Execute file as shell commands       ║\n");
    printf("║                                                                   ║\n");
    printf("║ System:                                                           ║\n");
    printf("║   help                      Show this help message               ║\n");
    printf("║   quit/exit                 Exit client                          ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    printf("Examples:\n");
    printf("  VIEW -al\n");
    printf("  CREATE mydoc.txt\n");
    printf("  WRITE mydoc.txt 0\n");
    printf("    1 Hello world.\n");
    printf("    ETIRW\n");
    printf("  READ mydoc.txt\n");
    printf("  ADDACCESS -W mydoc.txt bob\n");
    printf("\n");
}

// ============================================================================
// MAIN FUNCTION
// ============================================================================

int main(int argc, char *argv[]) {
    char nm_ip[INET_ADDRSTRLEN];
    int nm_port;
    char username[MAX_USERNAME_LENGTH];
    
    signal(SIGINT, signal_handler);
    
    // Display banner
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════╗\n");
    printf("║         LangOS Distributed File System v%s                    ║\n", CLIENT_VERSION);
    printf("║              Network File System Client                           ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    
    // Get nameserver details
    if (argc >= 3) {
        strncpy(nm_ip, argv[1], INET_ADDRSTRLEN - 1);
        nm_ip[INET_ADDRSTRLEN - 1] = '\0';
        nm_port = atoi(argv[2]);
    } else {
        printf("Nameserver Configuration:\n");
        printf("  IP Address: ");
        if (fgets(nm_ip, INET_ADDRSTRLEN, stdin) == NULL) {
            fprintf(stderr, "Failed to read IP address\n");
            return 1;
        }
        nm_ip[strcspn(nm_ip, "\n")] = 0;
        trim_whitespace(nm_ip);
        
        printf("  Port: ");
        if (scanf("%d", &nm_port) != 1) {
            fprintf(stderr, "Invalid port number\n");
            return 1;
        }
        getchar(); // Consume newline
    }
    
    // Get username
    printf("\nUser Authentication:\n");
    printf("  Username: ");
    if (fgets(username, MAX_USERNAME_LENGTH, stdin) == NULL) {
        fprintf(stderr, "Failed to read username\n");
        return 1;
    }
    username[strcspn(username, "\n")] = 0;
    trim_whitespace(username);
    
    // Initialize client
    printf("\nInitializing client...\n");
    int init_result = client_init(&g_client, nm_ip, nm_port, username);
    if (init_result != ERR_SUCCESS) {
        fprintf(stderr, "Failed to initialize client (Error code: %d)\n", init_result);
        return 1;
    }
    
    printf("Type 'help' for available commands, 'quit' to exit\n\n");
    
    // Command loop
    char input[MAX_COMMAND_LENGTH];
    char *tokens[10];
    int token_count;
    
    while (1) {
        printf(PROMPT_FORMAT, username, nm_ip);
        fflush(stdout);
        
        if (fgets(input, sizeof(input), stdin) == NULL) {
            break;
        }
        
        // Remove newline
        input[strcspn(input, "\n")] = 0;
        trim_whitespace(input);
        
        if (strlen(input) == 0) {
            continue;
        }
        
        // Parse command
        parse_command(input, tokens, &token_count);
        if (token_count == 0) {
            continue;
        }
        
        // Process commands
        if (strcmp(tokens[0], "quit") == 0 || strcmp(tokens[0], "exit") == 0) {
            break;
        }
        else if (strcmp(tokens[0], "help") == 0) {
            print_help();
        }
        else if (strcmp(tokens[0], "VIEW") == 0) {
            handle_view(&g_client, token_count > 1 ? tokens[1] : NULL);
        }
        else if (strcmp(tokens[0], "READ") == 0) {
            if (token_count < 2) {
                print_error("Usage: READ <filename>");
            } else {
                handle_read(&g_client, tokens[1]);
            }
        }
        else if (strcmp(tokens[0], "CREATE") == 0) {
            if (token_count < 2) {
                print_error("Usage: CREATE <filename>");
            } else {
                handle_create(&g_client, tokens[1]);
            }
        }
        else if (strcmp(tokens[0], "WRITE") == 0) {
            if (token_count < 3) {
                print_error("Usage: WRITE <filename> <sentence_number>");
            } else {
                int sentence_num = atoi(tokens[2]);
                handle_write(&g_client, tokens[1], sentence_num);
            }
        }
        else if (strcmp(tokens[0], "UNDO") == 0) {
            if (token_count < 2) {
                print_error("Usage: UNDO <filename>");
            } else {
                handle_undo(&g_client, tokens[1]);
            }
        }
        else if (strcmp(tokens[0], "INFO") == 0) {
            if (token_count < 2) {
                print_error("Usage: INFO <filename>");
            } else {
                handle_info(&g_client, tokens[1]);
            }
        }
        else if (strcmp(tokens[0], "DELETE") == 0) {
            if (token_count < 2) {
                print_error("Usage: DELETE <filename>");
            } else {
                handle_delete(&g_client, tokens[1]);
            }
        }
        else if (strcmp(tokens[0], "STREAM") == 0) {
            if (token_count < 2) {
                print_error("Usage: STREAM <filename>");
            } else {
                handle_stream(&g_client, tokens[1]);
            }
        }
        else if (strcmp(tokens[0], "LIST") == 0) {
            handle_list(&g_client);
        }
        else if (strcmp(tokens[0], "ADDACCESS") == 0) {
            if (token_count < 4) {
                print_error("Usage: ADDACCESS -R/-W <filename> <username>");
            } else {
                handle_addaccess(&g_client, tokens[1], tokens[2], tokens[3]);
            }
        }
        else if (strcmp(tokens[0], "REMACCESS") == 0) {
            if (token_count < 3) {
                print_error("Usage: REMACCESS <filename> <username>");
            } else {
                handle_remaccess(&g_client, tokens[1], tokens[2]);
            }
        }
        else if (strcmp(tokens[0], "EXEC") == 0) {
            if (token_count < 2) {
                print_error("Usage: EXEC <filename>");
            } else {
                handle_exec(&g_client, tokens[1]);
            }
        }
        else {
            print_error(get_error_message(ERR_INVALID_COMMAND));
            printf("Type 'help' for available commands.\n");
        }
    }
    
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════╗\n");
    printf("║              Disconnecting from LangOS...                         ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    
    client_cleanup(&g_client);
    return 0;
}

// ############## FIXED CLIENT CODE ENDS ##############
