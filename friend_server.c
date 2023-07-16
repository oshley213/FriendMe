#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>    /* Internet domain header */
#include <arpa/inet.h>     /* only needed on my mac */
#include <sys/select.h>

#include "friends.h"

#ifndef PORT
  #define PORT 53653
#endif

#define INPUT_BUFFER_SIZE 256
#define INPUT_ARG_MAX_NUM 12
#define DELIM " \r\n"

/* 
 * Print a formatted error message to stderr.
 */
void error(char *msg) {
    fprintf(stderr, "Error: %s\n", msg);
}

/* 
 * Read and process commands
 */
char *process_args(int cmd_argc, char **cmd_argv, User **user_list_ptr, User *curr_user) {
    User *user_list = *user_list_ptr;

    if (cmd_argc <= 0) {
        return strdup("No command entered.\n");
    }
    if (strcmp(cmd_argv[0], "quit") == 0 && cmd_argc == 1) {
        return strdup("Closing connection.\n");
    }
    if (strcmp(cmd_argv[0], "list_users") == 0 && cmd_argc == 1) {
        char *buf = list_users(user_list);
        return buf ? buf : strdup("Error listing users.\n");
    }
    if (strcmp(cmd_argv[0], "make_friends") == 0 && cmd_argc == 2) {
        switch (make_friends(cmd_argv[1], curr_user->name, user_list)) {
            case 1:
                return strdup("you guys are already friends\n");
            case 2:
                return strdup("you have reached the max number of friends\n");
            case 3:
                return strdup("you must enter different user\n");
            case 4:
                return strdup("user you entered does not exist\n");
            default:
                return strdup("friends connection made successfully\n");
        }
    }
    if (strcmp(cmd_argv[0], "post") == 0 && cmd_argc >= 3) {
        int space_needed = 0;
        for (int i = 2; i < cmd_argc; i++) {
            space_needed += strlen(cmd_argv[i]) + 1;
        }

        char *contents = malloc(space_needed);
        if (contents == NULL) {
            error("malloc");
            exit(1);
        }

        strcpy(contents, cmd_argv[2]);
        for (int i = 3; i < cmd_argc; i++) {
            strcat(contents, " ");
            strcat(contents, cmd_argv[i]);
        }

        User *author = curr_user;
        User *target = find_user(cmd_argv[1], user_list);
        switch (make_post(author, target, contents)) {
            case 1:
                return strdup("you guys are not friends\n");
            case 2:
                return strdup("user you entered does not exist\n");
            default:
                return strdup("post uploaded successfully\n");
        } 
    }
    if (strcmp(cmd_argv[0], "profile") == 0 && cmd_argc == 2) {
        User *user = find_user(cmd_argv[1], user_list);
        if (!user) {
            return strdup("user not found\n");
        }

        char *profile_buf = print_user(user);
        if (!profile_buf) {
            return strdup("Error printing user profile.\n");
        }
        return profile_buf;
    }
    return strdup("Incorrect syntax\n");
}

/*
 * Tokenize the string stored in cmd.
 * Return the number of tokens, and store the tokens in cmd_argv.
 */
int tokenize(char *cmd, char **cmd_argv) {
    int cmd_argc = 0;

    char *next_token = strtok(cmd, DELIM);    
    while (next_token != NULL) {
        if (cmd_argc >= INPUT_ARG_MAX_NUM - 1) {
            error("Too many arguments!");
            cmd_argc = 0;
            break;
        }
        cmd_argv[cmd_argc] = next_token;
        cmd_argc++;
        next_token = strtok(NULL, DELIM);
    }
    return cmd_argc;
}

// Step 3 code:
typedef struct client {
    int socket_fd;
    User *user;
    char *buffer;
    int buffer_len;
    int disconnected;
    struct client *next;
} Client;

void create_client(Client **clients, int client_socket_fd, User *user) {
    Client *new_client = malloc(sizeof(Client));
    new_client->socket_fd = client_socket_fd;
    new_client->user = user;
    new_client->disconnected = 0;
    new_client->buffer = NULL;
    new_client->buffer_len = 0;
    new_client->next = *clients;
    *clients = new_client;
}

void remove_client(Client **client_list_ptr, Client *client_to_remove) {
    // Find the client to remove
    Client *prev_client = NULL;
    Client *current_client = *client_list_ptr;
    while (current_client != NULL && current_client != client_to_remove) {
        prev_client = current_client;
        current_client = current_client->next;
    }

    // If the client was found, remove it from the list
    if (current_client != NULL) {
        if (prev_client != NULL) {
            // Client is not the first node in the list
            prev_client->next = current_client->next;
        } else {
            // Client is the first node in the list
            *client_list_ptr = current_client->next;
        }
        free(current_client->buffer);
        free(current_client);
    }
}

int main() {
    int sockfd, client_fd, max_fd;
    struct sockaddr_in serv_addr, cli_addr;
    socklen_t cli_len;
    fd_set read_fds, active_fds;
    Client *client_list = NULL;
    User *user_list = NULL;

    // Initialize server address
    bzero((char *) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(PORT);

    // Create socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        error("opening socket");
        exit(1);
    }

    int on = 1;  
    int status = setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (const char *) &on, sizeof(on));  
    if (status == -1) {  
        error("setsockopt -- REUSEADDR");  
    }  

    // Bind socket
    if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
        error("binding");
        exit(1);
    }
    
    // Listen for incoming connections
    if (listen(sockfd, 5) < 0) {
        error("listening");
        exit(1);
    }

    // Initialize file descriptor sets
    FD_ZERO(&active_fds);
    FD_SET(sockfd, &active_fds);
    max_fd = sockfd;

    printf("Server started: %d\n", PORT);
    while (1) {
        read_fds = active_fds;
        // Wait for activity on the fds
        if (select(max_fd + 1, &read_fds, NULL, NULL, NULL) < 0) {
            error("select");
            exit(1);
        }
        // Check if there is activity on the server socket
        if (FD_ISSET(sockfd, &read_fds)) {
            // Accept new connection
            cli_len = sizeof(cli_addr);
            client_fd = accept(sockfd, (struct sockaddr *) &cli_addr, &cli_len);
            if (client_fd < 0) {
                error("accept");
                exit(1);
            }

            // Prompt client to enter name
            char *name = calloc(MAX_NAME, sizeof(char));
            if (!name) {
                error("allocating memory");
                exit(1);
            }
            char *message = "What is your user name?\n";
            int bytes_sent = send(client_fd, message, strlen(message), 0);
            if (bytes_sent < 0) {
                error("sending message to client");
                free(name);
                exit(1);
            } else if (bytes_sent < strlen(message)) {
                printf("Warning: message truncated while sending to client\n");
            }
            fflush(stdout);

            int bytes_read = read(client_fd, name, MAX_NAME - 1);
            if (bytes_read < 0) {
                error("reading from socket");
                free(name);
                exit(1);
            }
            // Create user and client by name
            name = strtok(name, DELIM);
            create_user(name, &user_list);
            User *new_user = find_user(name, user_list);
            create_client(&client_list, client_fd, new_user);

            // Add client socket to fd set
            FD_SET(client_fd, &active_fds);
            if (client_fd > max_fd) {
                max_fd = client_fd;
            }
            printf("Client connected: %s\n", inet_ntoa(cli_addr.sin_addr));
            char *welcome_msg = "Welcome.\nGo ahead and enter user commands>\n";
            send(client_fd, welcome_msg, strlen(welcome_msg), 0);
            free(name);
        }
        Client *current_client = client_list;
        while (current_client != NULL) {
            if (current_client->disconnected) {
                // Skip disconnected clients
                current_client = current_client->next;
                continue;
            }
            if (FD_ISSET(current_client->socket_fd, &read_fds)) {
                if (current_client->disconnected) {
                    // Reconnect a disconnected client
                    current_client->disconnected = 0;

                    // Add client socket to fd set
                    FD_SET(current_client->socket_fd, &active_fds);
                    if (current_client->socket_fd > max_fd) {
                        max_fd = current_client->socket_fd;
                    }
                }
                // Read message from client
                char buffer[INPUT_BUFFER_SIZE];
                memset(buffer, 0, sizeof(buffer));
                int bytes_read = read(current_client->socket_fd, buffer, INPUT_BUFFER_SIZE - 1);
                if (bytes_read < 0) {
                    error("reading from socket");
                    continue; // Continue to next loop iteration
                }
                if (bytes_read == 0) {
                    // Client has disconnected
                    printf("Client %s disconnected\n", current_client->user->name);

                    // Remove client socket from fd set
                    FD_CLR(current_client->socket_fd, &active_fds);
                    close(current_client->socket_fd);
                    current_client->disconnected = 1;

                    // Remove client from client list
                    remove_client(&client_list, current_client);

                    // Set current_client to the next client in the list
                    struct client *next_client = current_client->next;
                    current_client = next_client;

                    continue; // Continue to next iteration 
                }
                // Process args from client
                char *cmd_argv[INPUT_ARG_MAX_NUM];
                int cmd_argc = tokenize(buffer, cmd_argv);
                if (cmd_argc > 0) {
                    char *response = process_args(cmd_argc, cmd_argv, &user_list, current_client->user);
                    
                    if (strcmp(response, "Closing connection.\n") == 0){
                        // Received quit command, close connection
                        printf("Client %s disconnected\n", current_client->user->name);
                        FD_CLR(current_client->socket_fd, &active_fds);
                        close(current_client->socket_fd);
                        current_client->disconnected = 1;

                        // Remove client
                        remove_client(&client_list, current_client);

                        // Set current_client to the next client
                        struct client *next_client = current_client->next;
                        current_client = next_client;

                        continue; // Continue to next iteration
                    } 
                    send(current_client->socket_fd, response, strlen(response), 0);
                    free(response);
                }
            } 
            current_client = current_client->next;
        }
    }
    close(sockfd);
    return 0;
}

// Step 2 code: 
// int main() {
//     char *cli_name = malloc(MAX_NAME * sizeof(char));
//     User *user_list = NULL;
    
//     // Create socket
//     int listen_soc = socket(AF_INET, SOCK_STREAM, 0);
//     if (listen_soc < 0) {
//             perror("server: socket");
//             exit(1);
//         }

//     // Initialize server address
//     struct sockaddr_in server;
//     server.sin_family = AF_INET;
//     server.sin_port = htons(PORT);
//     memset(&server.sin_zero, 0, 8);
//     server.sin_addr.s_addr = INADDR_ANY;

//     int on = 1;  
//     int status = setsockopt(listen_soc, SOL_SOCKET, SO_REUSEADDR, (const char *) &on, sizeof(on));  
//     if (status == -1) {  
//         perror("setsockopt -- REUSEADDR");  
//     }  

//     // Bind socket to an address
//     if (bind(listen_soc, (struct sockaddr *) &server, sizeof(struct sockaddr_in)) == -1) {
//         perror("server: bind");
//         close(listen_soc);
//         exit(1);
//     }
    
//     // Set up a queue in the kernel to hold pending connections.
//     // Listen for connections with the listen() system call. 
//     if (listen(listen_soc, 5) < 0) {
//         // listen failed 
//         perror("listen");
//         exit(1);
//     }

//     printf("Server started: %d\n", PORT);

//     while (1) {
//         // Accept a connection with the accept() system call. This call typically blocks until a client connects with the server.
//         struct sockaddr_in client_addr;
//         unsigned int client_len = sizeof(struct sockaddr_in);
//         //   client_addr.sin_family = AF_INET;

//         int client_socket = accept(listen_soc, (struct sockaddr *)&client_addr, &client_len);
//         if (client_socket == -1) {
//             perror("accept");
//             continue;
//         }
//         printf("Client connected: %s\n", inet_ntoa(client_addr.sin_addr));
        
//         // Getting client name 
//         memset(cli_name, 0, MAX_NAME + 1);
//         char message[] = "What is your user name?\n";
//         send(client_socket, message, strlen(message), 0);
//         if (read(client_socket, cli_name, MAX_NAME + 1) == -1) {
//             perror("read");
//             close(client_socket);
//             continue;
//         }
	
// 	    cli_name = strtok(cli_name, DELIM);

//         // Adding client to user list
//         if (create_user(cli_name, &user_list) != 0) {
//             error("create_user");
//             close(client_socket);
//             continue;
//         }
//         printf("%s added to user list:\n\n", cli_name);
// 	    char *welcome_msg = "Welcome.\nGo ahead and enter user commands>\n";
//         send(client_socket, welcome_msg, strlen(welcome_msg), 0);
//         while (1) {
//             // Read input from the client
//             // char *command_msg = "> ";
//             // send(client_socket, command_msg, strlen(command_msg), 0);
//             char input_buffer[INPUT_BUFFER_SIZE] = {'\0'};
//             int num_bytes_read = read(client_socket, input_buffer, INPUT_BUFFER_SIZE);
//             if (num_bytes_read < 0) {
//                 perror("read");
//                 break;
//             } else if (num_bytes_read == 0) {
//                 // End of file, client has disconnected
//                 printf("Client disconnected.\n");
//                 break;
//             }

//             // Parse the input into command arguments
//             char *args[INPUT_ARG_MAX_NUM];
//             int num_args = tokenize(input_buffer, args);
//             // if (num_args == 0) {
//             //     continue;
//             // }
            
//             // Process the command arguments and send the result to the client
//             if (num_args > 0) {
//                 char *result = process_args(num_args, args, &user_list);
//                 if (strcmp(result, "Closing connection.\n") == 0){
//                     // Received quit command, close connection
//                     close(client_socket);
//                     break;
//                 } else {
//                     // Convert the result code to a string and send it to the client
//                     send(client_socket, result, strlen(result), 0);
//                 }
//             }
//             // Clear the input buffer and arguments array
//             memset(input_buffer, 0, sizeof(input_buffer));
//             memset(args, 0, sizeof(args));
//         }
//         close(client_socket);
//         break;
//     }
//     close(listen_soc);
//     return 0;
// }
