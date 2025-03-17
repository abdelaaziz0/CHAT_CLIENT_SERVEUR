#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <poll.h>
#include <netdb.h>
#include <limits.h>
#include <sys/stat.h>
#include <errno.h>
#include "msg_struct.h"
#include "common.h"

static char saved_filepath[FILE_PATH_LEN];

// Structure globale pour le transfert de fichiers
typedef struct {
    char filename[FILE_PATH_LEN];
    char sender[NICK_LEN];
    char receiver[NICK_LEN];
    int transfer_socket;
    int listening;
} FileTransfer;


void handle_file_request(int sockfd, const char *sender, const char *filename);
void handle_file_send(const char *nickname, const char *filepath, int sockfd);
void handle_file_response(struct message *msg, const char *payload);
void setup_file_receiver(FileTransfer *transfer);
void send_message(int sockfd, struct message *msg, const char *payload);

void send_message(int sockfd, struct message *msg, const char *payload) {
    if (send(sockfd, msg, sizeof(struct message), 0) == -1) {
        perror("send() message struct");
        return;
    }
    if (msg->pld_len > 0 && payload != NULL) {
        if (send(sockfd, payload, msg->pld_len, 0) == -1) {
            perror("send() payload");
            return;
        }
    }
}

void handle_user_input(int sockfd, char *buffer, char *nickname) {
    struct message msg = {0};
    char payload[MSG_LEN] = {0};
    
    if (strncmp(buffer, "/nick ", 6) == 0) {
        msg.type = NICKNAME_NEW;
        strncpy(msg.infos, buffer + 6, INFOS_LEN - 1);
        msg.pld_len = 0;
        strncpy(msg.nick_sender, nickname, NICK_LEN - 1);
    } 
    else if (strcmp(buffer, "/who") == 0) {
        msg.type = NICKNAME_LIST;
        msg.infos[0] = '\0';
        msg.pld_len = 0;
        strncpy(msg.nick_sender, nickname, NICK_LEN - 1);
    } 
    else if (strncmp(buffer, "/whois ", 7) == 0) {
        msg.type = NICKNAME_INFOS;
        strncpy(msg.infos, buffer + 7, INFOS_LEN - 1);
        msg.pld_len = 0;
        strncpy(msg.nick_sender, nickname, NICK_LEN - 1);
    } 
    else if (strncmp(buffer, "/msgall ", 8) == 0) {
        msg.type = BROADCAST_SEND;
        msg.infos[0] = '\0';
        msg.pld_len = strlen(buffer + 8);
        strncpy(msg.nick_sender, nickname, NICK_LEN - 1);
        strncpy(payload, buffer + 8, MSG_LEN - 1);
    } 
    else if (strncmp(buffer, "/msg ", 5) == 0) {
        msg.type = UNICAST_SEND;
        char *space = strchr(buffer + 5, ' ');
        if (space) {
            int nickname_len = space - (buffer + 5);
            strncpy(msg.infos, buffer + 5, nickname_len);
            msg.infos[nickname_len] = '\0';
            msg.pld_len = strlen(space + 1);
            strncpy(msg.nick_sender, nickname, NICK_LEN - 1);
            strncpy(payload, space + 1, MSG_LEN - 1);
        } else {
            printf("Usage: /msg <nickname> <message>\n");
            return;
        }
    }

    else if (strncmp(buffer, "/create ", 8) == 0) {
        msg.type = MULTICAST_CREATE;
        strncpy(msg.infos, buffer + 8, INFOS_LEN - 1);
        msg.pld_len = 0;
        strncpy(msg.nick_sender, nickname, NICK_LEN - 1);
    }
    else if (strcmp(buffer, "/channel_list") == 0) {
        msg.type = MULTICAST_LIST;
        msg.infos[0] = '\0';
        msg.pld_len = 0;
        strncpy(msg.nick_sender, nickname, NICK_LEN - 1);
    }
    else if (strncmp(buffer, "/join ", 6) == 0) {
        msg.type = MULTICAST_JOIN;
        strncpy(msg.infos, buffer + 6, INFOS_LEN - 1);
        msg.pld_len = 0;
        strncpy(msg.nick_sender, nickname, NICK_LEN - 1);
    }
    else if (strncmp(buffer, "/quit ", 6) == 0) {
        msg.type = MULTICAST_QUIT;
        strncpy(msg.infos, buffer + 6, INFOS_LEN - 1);
        msg.pld_len = 0;
        strncpy(msg.nick_sender, nickname, NICK_LEN - 1);
    }
    else if (strncmp(buffer, "/send ", 6) == 0) {
        char *space = strchr(buffer + 6, ' ');
        if (space) {
            *space = '\0';
            char *nickname = buffer + 6;
            char *filepath = space + 1;
            if (filepath[0] == '"') {
                filepath++;
                char *end_quote = strchr(filepath, '"');
                if (end_quote) *end_quote = '\0';
            }
            
            handle_file_send(nickname, filepath, sockfd);
        } else {
            printf("Usage: /send <nickname> <filepath>\n");
        }
    }
    else {
        msg.type = MULTICAST_SEND;
        msg.infos[0] = '\0';
        msg.pld_len = strlen(buffer);
        strncpy(msg.nick_sender, nickname, NICK_LEN - 1);
        strncpy(payload, buffer, MSG_LEN - 1);
    }
    
    send_message(sockfd, &msg, msg.pld_len > 0 ? payload : NULL);
}

void echo_client(int sockfd) {
    struct pollfd fds[2];
    char buffer[MSG_LEN];
    char nickname[NICK_LEN] = {0};
    
    fds[0].fd = STDIN_FILENO;
    fds[0].events = POLLIN;
    fds[1].fd = sockfd;
    fds[1].events = POLLIN;

    while (1) {
        int poll_ret = poll(fds, 2, -1);
        if (poll_ret < 0) {
            perror("poll()");
            break;
        }

        if (fds[0].revents & POLLIN) {
            if (fgets(buffer, MSG_LEN, stdin) != NULL) {
                size_t len = strlen(buffer);
                if (len > 0 && buffer[len-1] == '\n') {
                    buffer[len-1] = '\0';
                }

                if (strcmp(buffer, "/quit") == 0) {
                    printf("Goodbye!\n");
                    break;
                }

                handle_user_input(sockfd, buffer, nickname);
            }
        }

        if (fds[1].revents & POLLIN) {
            struct message msg = {0};
            char payload[MSG_LEN] = {0};
            
            ssize_t rec = recv(sockfd, &msg, sizeof(struct message), 0);
            if (rec <= 0) {
                if (rec == 0) printf("Server disconnected\n");
                else perror("recv() message struct");
                break;
            }

            if (msg.pld_len > 0) {
                if (recv(sockfd, payload, msg.pld_len, 0) <= 0) {
                    perror("recv() payload");
                    break;
                }
                payload[msg.pld_len] = '\0';
            }

            switch (msg.type) {
                case NICKNAME_NEW:
                    if (msg.infos[0] != '\0') {
                        strncpy(nickname, msg.infos, NICK_LEN - 1);
                        printf("Welcome on the chat %s\n", nickname);
                    } else {
                        printf("Nickname change failed\n");
                    }
                    break;
                    
                case NICKNAME_LIST:
                case NICKNAME_INFOS:
                    printf("%s\n", msg.infos);
                    break;
                    
                case ECHO_SEND:
                    // Message d'erreur du serveur ou message d'information
                    printf("[%s]: %s\n", msg.nick_sender, msg.infos);
                    break;

                case UNICAST_SEND:
                case BROADCAST_SEND:
                case MULTICAST_SEND:
                    if (msg.pld_len > 0) {
                        printf("[%s]: %s\n", msg.nick_sender, payload);
                    } else {
                        printf("[%s]: %s\n", msg.nick_sender, msg.infos);
                    }
                    break;

                case FILE_REQUEST:
                    handle_file_request(sockfd, msg.nick_sender, payload);
                    break;

                case FILE_ACCEPT:
                    handle_file_response(&msg, payload);
                    break;

                case FILE_REJECT:
                    printf("%s refused the file transfer. (%s)\n", msg.nick_sender, msg_type_str[13]);
                    break;

                case FILE_ACK:
                    printf("%s has received the file.\n", msg.nick_sender);
                    break;
                    
                default:
                    if (msg.infos[0] != '\0') {
                        printf("[%s]: %s\n", msg.nick_sender, msg.infos);
                    }
                    break;
            }
        }
    }
}

int handle_connect(const char *server_name, const char *server_port) {
    int sockfd;
    struct sockaddr_in server_addr;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket()");
        return -1;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(atoi(server_port));

    if (strcmp(server_name, "localhost") == 0) {
        server_name = "127.0.0.1";
    }

    if (inet_pton(AF_INET, server_name, &server_addr.sin_addr) <= 0) {
        struct hostent *he = gethostbyname(server_name);
        if (he == NULL) {
            perror("gethostbyname");
            close(sockfd);
            return -1;
        }
        memcpy(&server_addr.sin_addr, he->h_addr_list[0], he->h_length);
    }

    if (connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect()");
        close(sockfd);
        return -1;
    }

    return sockfd;
}

void setup_file_receiver(FileTransfer *transfer) {
    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("socket() for file transfer");
        return;
    }
    
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(FILE_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;
    
    if (bind(server_socket, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind() for file transfer");
        close(server_socket);
        return;
    }
    
    if (listen(server_socket, 1) < 0) {
        perror("listen() for file transfer");
        close(server_socket);
        return;
    }
    
    transfer->transfer_socket = server_socket;
    transfer->listening = 1;
}

void handle_file_request(int sockfd, const char *sender, const char *filename) {
    printf("%s wants you to accept the transfer of the file named \"%s\". Do you accept? [Y/N]\n", 
           sender, filename);
    
    char response;
    scanf(" %c", &response);
    
    struct message msg = {0};
    msg.type = (response == 'Y' || response == 'y') ? FILE_ACCEPT : FILE_REJECT;
    strncpy(msg.infos, sender, NICK_LEN);
    
    if (msg.type == FILE_ACCEPT) {
        FileTransfer transfer = {0};
        strncpy(transfer.filename, filename, FILE_PATH_LEN);
        strncpy(transfer.sender, sender, NICK_LEN);
        setup_file_receiver(&transfer);
        char payload[MSG_LEN];
        snprintf(payload, MSG_LEN, "127.0.0.1:%d", FILE_PORT);
        msg.pld_len = strlen(payload) + 1;
        
        send_message(sockfd, &msg, payload);
        
        struct sockaddr_in sender_addr;
        socklen_t addr_len = sizeof(sender_addr);
        printf("Waiting for file transfer connection...\n");
        
        int file_socket = accept(transfer.transfer_socket, 
                               (struct sockaddr*)&sender_addr, &addr_len);
        if (file_socket < 0) {
            perror("accept() file transfer");
            close(transfer.transfer_socket);
            return;
        }
        system("mkdir -p ./inbox");
        char file_path[FILE_PATH_LEN];
        snprintf(file_path, FILE_PATH_LEN, "./inbox/%s", filename);
        FILE *fp = fopen(file_path, "wb");
        if (!fp) {
            perror("fopen() for received file");
            close(file_socket);
            close(transfer.transfer_socket);
            return;
        }
        
        char buffer[4096];
        ssize_t bytes_received;
        while ((bytes_received = recv(file_socket, buffer, sizeof(buffer), 0)) > 0) {
            fwrite(buffer, 1, bytes_received, fp);
        }
        
        fclose(fp);
        close(file_socket);
        close(transfer.transfer_socket);
        
        msg.type = FILE_ACK;
        msg.pld_len = 0;
        strncpy(msg.infos, filename, INFOS_LEN);
        send_message(sockfd, &msg, NULL);
        
        printf("File saved in ./inbox/%s\n", filename);
    } else {
        // Envoyer le rejet
        msg.pld_len = 0;
        send_message(sockfd, &msg, NULL);
    }
}


void handle_file_send(const char *nickname, const char *filepath, int sockfd) {
    if (!nickname || !filepath || sockfd < 0) {
        printf("Error: Invalid parameters\n");
        return;
    }

    FILE *fp = fopen(filepath, "rb");
    if (!fp) {
        printf("Error: Cannot open file %s\n", filepath);
        return;
    }

    strncpy(saved_filepath, filepath, FILE_PATH_LEN - 1);
    saved_filepath[FILE_PATH_LEN - 1] = '\0';

    const char *filename = strrchr(filepath, '/');
    filename = filename ? filename + 1 : filepath;

    struct message msg = {0};
    msg.type = FILE_REQUEST;
    msg.pld_len = strlen(filename) + 1;
    strncpy(msg.infos, nickname, INFOS_LEN - 1);
    msg.infos[INFOS_LEN - 1] = '\0';
    
    fclose(fp);
    send_message(sockfd, &msg, filename);
    printf("File transfer request sent for \"%s\" to %s\n", filename, nickname);
}

void handle_file_response(struct message *msg, const char *payload) {
    if (!msg || !payload) {
        printf("Error: Invalid response\n");
        return;
    }

    if (msg->type == FILE_ACCEPT) {
        printf("%s accepted file transfer.\n", msg->infos);
        
        // Parser l'adresse et le port
        char addr[16] = {0};
        int port = 0;
        if (sscanf(payload, "%15[^:]:%d", addr, &port) != 2) {
            printf("Error: Invalid address format\n");
            return;
        }
        
        printf("Connecting to %s and sending the file %s...\n", msg->infos, saved_filepath);
        
        // Créer le socket de transfert
        int transfer_socket = socket(AF_INET, SOCK_STREAM, 0);
        if (transfer_socket < 0) {
            perror("socket() for file transfer");
            return;
        }
        
        // Configurer l'adresse de connexion
        struct sockaddr_in receiver_addr = {0};
        receiver_addr.sin_family = AF_INET;
        receiver_addr.sin_port = htons(port);
        if (inet_pton(AF_INET, addr, &receiver_addr.sin_addr) <= 0) {
            printf("Error: Invalid address\n");
            close(transfer_socket);
            return;
        }
        
        // Se connecter au récepteur
        if (connect(transfer_socket, (struct sockaddr*)&receiver_addr,
                   sizeof(receiver_addr)) < 0) {
            perror("connect() for file transfer");
            close(transfer_socket);
            return;
        }
        
        // Ouvrir le fichier à envoyer
        FILE *fp = fopen(saved_filepath, "rb");
        if (!fp) {
            printf("Error: Cannot open file %s for sending\n", saved_filepath);
            close(transfer_socket);
            return;
        }
        
        // Envoyer le fichier
        char buffer[4096];
        size_t bytes_read;
        size_t total_sent = 0;
        
        while ((bytes_read = fread(buffer, 1, sizeof(buffer), fp)) > 0) {
            size_t bytes_to_send = bytes_read;
            size_t bytes_sent = 0;
            
            while (bytes_sent < bytes_to_send) {
                ssize_t ret = send(transfer_socket,
                                 buffer + bytes_sent,
                                 bytes_to_send - bytes_sent,
                                 0);
                if (ret < 0) {
                    perror("send() file data");
                    fclose(fp);
                    close(transfer_socket);
                    return;
                }
                bytes_sent += ret;
            }
            total_sent += bytes_sent;
        }
        
        fclose(fp);
        close(transfer_socket);
        printf("File sent successfully (%zu bytes)\n", total_sent);
    } else {
        printf("%s cancelled file transfer.\n", msg->infos);
    }
}


int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <server_name> <server_port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int sockfd = handle_connect(argv[1], argv[2]);
    if (sockfd == -1) {
        fprintf(stderr, "Connection failed\n");
        exit(EXIT_FAILURE);
    }

    printf("Connecting to server ... done!\n");
    echo_client(sockfd);
    close(sockfd);
    return EXIT_SUCCESS;
}