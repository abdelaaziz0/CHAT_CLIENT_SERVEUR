#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <poll.h>
#include <time.h>
#include <ctype.h>
#include "msg_struct.h"
#include "common.h"

#define MAX_CLIENTS 100
#define MAX_CHANNELS 100
#define CHANNEL_NAME_LEN 32
#define POLL_TIMEOUT -1

typedef struct {
    int fd;
    char nickname[NICK_LEN];
    struct sockaddr_in addr;
    time_t connection_time;
    int has_nickname;
    char current_channel[CHANNEL_NAME_LEN];  // Nom du canal actuel
} Client;

typedef struct {
    Client clients[MAX_CLIENTS];
    int count;
} ClientManager;

typedef struct {
    char name[CHANNEL_NAME_LEN];
    Client *users[MAX_CLIENTS];
    int user_count;
} Channel;

typedef struct {
    Channel channels[MAX_CHANNELS];
    int count;
} ChannelManager;

ClientManager client_manager = {0};
ChannelManager channel_manager = {0};


void safe_strcpy(char *dest, const char *src, size_t size);
void send_message(int fd, struct message *msg, const char *payload);
Client *find_client_by_fd(int fd);
Client *find_client_by_nickname(const char *nickname);
void handle_nickname_new(Client *client, struct message *msg);
void handle_nickname_list(Client *client);  // Ajout de cette déclaration
void handle_nickname_infos(Client *client, struct message *msg);
void handle_broadcast(Client *sender, struct message *msg, const char *payload);
void handle_unicast(Client *sender, struct message *msg, const char *payload);
void handle_channel_message(Client *client, const char *payload);
void handle_channel_create(Client *client, const char *channel_name);
void handle_channel_list(Client *client);
void handle_channel_join(Client *client, const char *channel_name);
void handle_channel_quit(Client *client, const char *channel_name);
void remove_from_current_channel(Client *client);
void notify_channel(Channel *channel, const char *message, Client *exclude);
Channel *find_channel_by_name(const char *name);

// Utilitaires
void safe_strcpy(char *dest, const char *src, size_t size) {
    strncpy(dest, src, size - 1);
    dest[size - 1] = '\0';
}

void send_message(int fd, struct message *msg, const char *payload) {
    if (send(fd, msg, sizeof(struct message), 0) == -1) {
        perror("send() message struct");
        return;
    }
    
    if (msg->pld_len > 0 && payload != NULL) {
        if (send(fd, payload, msg->pld_len, 0) == -1) {
            perror("send() payload");
        }
    }
}

// Gestion des clients
Client *find_client_by_fd(int fd) {
    for (int i = 0; i < client_manager.count; i++) {
        if (client_manager.clients[i].fd == fd) {
            return &client_manager.clients[i];
        }
    }
    return NULL;
}

Client *find_client_by_nickname(const char *nickname) {
    for (int i = 0; i < client_manager.count; i++) {
        if (client_manager.clients[i].has_nickname &&
            strcmp(client_manager.clients[i].nickname, nickname) == 0) {
            return &client_manager.clients[i];
        }
    }
    return NULL;
}

// Validation des noms
int is_nickname_valid(const char *nickname) {
    size_t len = strlen(nickname);
    if (len >= NICK_LEN || len == 0) return 0;
    
    for (size_t i = 0; i < len; i++) {
        if (!isalnum(nickname[i])) return 0;
    }
    return 1;
}

int is_channel_name_valid(const char *name) {
    size_t len = strlen(name);
    if (len >= CHANNEL_NAME_LEN || len == 0) return 0;
    
    for (size_t i = 0; i < len; i++) {
        if (!isalnum(name[i])) return 0;
    }
    return 1;
}

// Gestion des salons
Channel *find_channel_by_name(const char *name) {
    for (int i = 0; i < channel_manager.count; i++) {
        if (strcmp(channel_manager.channels[i].name, name) == 0) {
            return &channel_manager.channels[i];
        }
    }
    return NULL;
}

void notify_channel(Channel *channel, const char *message, Client *exclude) {
    struct message notify = {0};
    notify.type = ECHO_SEND;
    safe_strcpy(notify.nick_sender, "Server", NICK_LEN);
    safe_strcpy(notify.infos, message, INFOS_LEN);

    for (int i = 0; i < channel->user_count; i++) {
        if (channel->users[i] != exclude) {
            send_message(channel->users[i]->fd, &notify, NULL);
        }
    }
}

void remove_from_current_channel(Client *client) {
    if (client->current_channel[0] == '\0') return;
    
    Channel *channel = find_channel_by_name(client->current_channel);
    if (!channel) return;

    printf("Removing user %s from channel %s (current users: %d)\n", 
           client->nickname, channel->name, channel->user_count);

    char notify_msg[INFOS_LEN];
    snprintf(notify_msg, INFOS_LEN, "INFO> %.20s has quit %.20s",
             client->nickname, channel->name);
    
    for (int i = 0; i < channel->user_count; i++) {
        if (channel->users[i] == client) {
            notify_channel(channel, notify_msg, client);
            
            // Déplacer le dernier utilisateur à cette position
            channel->user_count--;
            if (i < channel->user_count) {
                channel->users[i] = channel->users[channel->user_count];
            }
            channel->users[channel->user_count] = NULL;

            printf("After removal: Channel %s now has %d users\n", 
                   channel->name, channel->user_count);

            // Si c'était le dernier utilisateur
            if (channel->user_count == 0) {
                struct message destroy = {0};
                destroy.type = ECHO_SEND;
                safe_strcpy(destroy.nick_sender, "Server", NICK_LEN);
                snprintf(destroy.infos, INFOS_LEN, 
                        "INFO> You were the last user in this channel, %s has been destroyed", 
                        channel->name);
                send_message(client->fd, &destroy, NULL);

                // Supprimer le canal
                int idx = channel - channel_manager.channels;
                printf("Removing empty channel %s at index %d\n", channel->name, idx);
                channel_manager.count--;
                if (idx < channel_manager.count) {
                    channel_manager.channels[idx] = channel_manager.channels[channel_manager.count];
                }
            }
            
            client->current_channel[0] = '\0';
            break;
        }
    }
}
// Handlers pour les différents types de messages
void handle_channel_create(Client *client, const char *channel_name) {
    struct message response = {0};
    response.type = ECHO_SEND;
    safe_strcpy(response.nick_sender, "Server", NICK_LEN);

    // Vérifications habituelles
    if (!is_channel_name_valid(channel_name)) {
        safe_strcpy(response.infos, "Invalid channel name format", INFOS_LEN);
        send_message(client->fd, &response, NULL);
        return;
    }

    if (find_channel_by_name(channel_name)) {
        safe_strcpy(response.infos, "Channel already exists", INFOS_LEN);
        send_message(client->fd, &response, NULL);
        return;
    }

    if (channel_manager.count >= MAX_CHANNELS) {
        safe_strcpy(response.infos, "Maximum number of channels reached", INFOS_LEN);
        send_message(client->fd, &response, NULL);
        return;
    }

    // Créer le nouveau canal d'abord et l'initialiser
    Channel *new_channel = &channel_manager.channels[channel_manager.count];
    memset(new_channel, 0, sizeof(Channel));
    safe_strcpy(new_channel->name, channel_name, CHANNEL_NAME_LEN);
    channel_manager.count++;  // Incrémenter le compteur tout de suite

    // Sauvegarder l'ancien nom de canal
    char old_channel[CHANNEL_NAME_LEN];
    strncpy(old_channel, client->current_channel, CHANNEL_NAME_LEN);

    // Mettre à jour le client avec son nouveau canal AVANT de quitter l'ancien
    safe_strcpy(client->current_channel, channel_name, CHANNEL_NAME_LEN);
    
    // Ajouter le client au nouveau canal
    new_channel->users[0] = client;
    new_channel->user_count = 1;

    // Notifier de la création
    snprintf(response.infos, INFOS_LEN, "You have created channel %s", channel_name);
    send_message(client->fd, &response, NULL);

    // Maintenant seulement, traiter l'ancien canal si nécessaire
    if (old_channel[0] != '\0') {
        Channel *old = find_channel_by_name(old_channel);
        if (old) {
            // Notifier les autres utilisateurs du départ
            char notify_msg[INFOS_LEN];
            snprintf(notify_msg, INFOS_LEN, "INFO> %.20s has quit %.20s",
                     client->nickname, old_channel);
            notify_channel(old, notify_msg, client);

            // Retirer le client de l'ancien canal
            for (int i = 0; i < old->user_count; i++) {
                if (old->users[i] == client) {
                    old->user_count--;
                    if (i < old->user_count) {
                        old->users[i] = old->users[old->user_count];
                    }
                    old->users[old->user_count] = NULL;
                    break;
                }
            }

            // Si c'était le dernier utilisateur, supprimer le canal
            if (old->user_count == 0) {
                struct message destroy = {0};
                destroy.type = ECHO_SEND;
                safe_strcpy(destroy.nick_sender, "Server", NICK_LEN);
                snprintf(destroy.infos, INFOS_LEN, 
                        "INFO> You were the last user in this channel, %s has been destroyed", 
                        old_channel);
                send_message(client->fd, &destroy, NULL);

                int idx = old - channel_manager.channels;
                if (idx < channel_manager.count - 1) {
                    channel_manager.channels[idx] = channel_manager.channels[channel_manager.count - 1];
                }
                channel_manager.count--;
            }
        }
    }

    // Notifier que l'utilisateur a rejoint le nouveau canal
    snprintf(response.infos, INFOS_LEN, "You have joined %s", channel_name);
    send_message(client->fd, &response, NULL);
}
void handle_channel_list(Client *client) {
    struct message response = {0};
    response.type = ECHO_SEND;
    safe_strcpy(response.nick_sender, "Server", NICK_LEN);

    char list[INFOS_LEN] = "Available channels:\n";
    size_t remaining = INFOS_LEN - strlen(list);

    for (int i = 0; i < channel_manager.count; i++) {
        Channel *channel = &channel_manager.channels[i];
        
        printf("Channel: %s, Users: %d\n", channel->name, channel->user_count);

        // Vérifier et ajouter le canal à la liste
        int len = snprintf(NULL, 0, "- %s (%d users)\n", 
                         channel->name, channel->user_count);
        if (len < remaining) {
            snprintf(list + strlen(list), remaining,
                    "- %s (%d users)\n",
                    channel->name, channel->user_count);
            remaining -= len;
        }
    }

    safe_strcpy(response.infos, list, INFOS_LEN);
    send_message(client->fd, &response, NULL);
}

// Ajouter cette fonction avec les autres fonctions de gestion des pseudos
void handle_nickname_list(Client *client) {
    struct message response = {0};
    response.type = NICKNAME_LIST;
    safe_strcpy(response.nick_sender, "Server", NICK_LEN);
    
    char list[INFOS_LEN] = "Online users:\n";
    size_t remaining = INFOS_LEN - strlen(list);
    
    for (int i = 0; i < client_manager.count && remaining > 0; i++) {
        if (client_manager.clients[i].has_nickname) {
            int len = snprintf(NULL, 0, "- %s\n", client_manager.clients[i].nickname);
            if (len < remaining) {
                snprintf(list + strlen(list), remaining, "- %s\n", 
                        client_manager.clients[i].nickname);
                remaining -= len;
            }
        }
    }
    
    safe_strcpy(response.infos, list, INFOS_LEN);
    send_message(client->fd, &response, NULL);
}
void handle_channel_join(Client *client, const char *channel_name) {
    struct message response = {0};
    response.type = ECHO_SEND;
    safe_strcpy(response.nick_sender, "Server", NICK_LEN);

    Channel *channel = find_channel_by_name(channel_name);
    if (!channel) {
        safe_strcpy(response.infos, "Channel does not exist", INFOS_LEN);
        send_message(client->fd, &response, NULL);
        return;
    }

    if (channel->user_count >= MAX_CLIENTS) {
        safe_strcpy(response.infos, "Channel is full", INFOS_LEN);
        send_message(client->fd, &response, NULL);
        return;
    }

    // Si le client est déjà dans un autre salon, le faire quitter
    if (client->current_channel[0] != '\0') {
        remove_from_current_channel(client);
    }

    // Ajouter l'utilisateur au canal
    safe_strcpy(client->current_channel, channel_name, CHANNEL_NAME_LEN);
    channel->users[channel->user_count] = client;
    channel->user_count++;

    // Debug: afficher le nombre d'utilisateurs
    printf("Channel %s now has %d users\n", channel->name, channel->user_count);

    snprintf(response.infos, INFOS_LEN, "INFO> You have joined %s", channel_name);
    send_message(client->fd, &response, NULL);

    // Notifier les autres utilisateurs
    char notify_msg[INFOS_LEN];
    snprintf(notify_msg, INFOS_LEN, "INFO> %.20s has joined %.20s",
             client->nickname, channel_name);
    notify_channel(channel, notify_msg, client);
}

void handle_channel_quit(Client *client, const char *channel_name) {
    if (strcmp(client->current_channel, channel_name) != 0) {
        struct message response = {0};
        response.type = ECHO_SEND;
        safe_strcpy(response.nick_sender, "Server", NICK_LEN);
        safe_strcpy(response.infos, "You are not in this channel", INFOS_LEN);
        send_message(client->fd, &response, NULL);
        return;
    }
    remove_from_current_channel(client);

    
}
void handle_channel_message(Client *client, const char *payload) {
    if (client->current_channel[0] == '\0') {
        struct message response = {0};
        response.type = ECHO_SEND;
        safe_strcpy(response.nick_sender, "Server", NICK_LEN);
        safe_strcpy(response.infos, "You are not in any channel", INFOS_LEN);
        send_message(client->fd, &response, NULL);
        return;
    }

    Channel *channel = find_channel_by_name(client->current_channel);
    if (!channel) return;

    struct message msg = {0};
    msg.type = MULTICAST_SEND;
    safe_strcpy(msg.nick_sender, client->nickname, NICK_LEN);
    msg.pld_len = strlen(payload);
    safe_strcpy(msg.infos, channel->name, INFOS_LEN);

    for (int i = 0; i < channel->user_count; i++) {
        if (channel->users[i] != client) {
            printf("Sending to user: %s\n", channel->users[i]->nickname);
            send_message(channel->users[i]->fd, &msg, payload);
        }
    }
}

void handle_nickname_new(Client *client, struct message *msg) {
    struct message response = {0};
    response.type = NICKNAME_NEW;
    safe_strcpy(response.nick_sender, "Server", NICK_LEN);
    
    if (!is_nickname_valid(msg->infos)) {
        safe_strcpy(response.infos, "Invalid nickname format", INFOS_LEN);
        send_message(client->fd, &response, NULL);
        return;
    }
    
    if (find_client_by_nickname(msg->infos)) {
        safe_strcpy(response.infos, "Nickname already taken", INFOS_LEN);
        send_message(client->fd, &response, NULL);
        return;
    }
    
    safe_strcpy(client->nickname, msg->infos, NICK_LEN);
    client->has_nickname = 1;
    safe_strcpy(response.infos, client->nickname, INFOS_LEN);
    printf("User %s registered\n", client->nickname);
    send_message(client->fd, &response, NULL);
}
void handle_nickname_infos(Client *client, struct message *msg) {
    struct message response = {0};
    response.type = NICKNAME_INFOS;
    safe_strcpy(response.nick_sender, "Server", NICK_LEN);
    
    Client *target = find_client_by_nickname(msg->infos);
    if (target == NULL) {
        snprintf(response.infos, INFOS_LEN, "User %.50s not found", msg->infos);
    } else {
        char time_str[32];
        strftime(time_str, sizeof(time_str), "%Y/%m/%d@%H:%M",
                localtime(&target->connection_time));
        
        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(target->addr.sin_addr), ip_str, INET_ADDRSTRLEN);
        
        snprintf(response.infos, INFOS_LEN, "%.20s connected since %.20s with IP %.15s port %d",
                target->nickname, time_str, ip_str, ntohs(target->addr.sin_port));
    }
    
    send_message(client->fd, &response, NULL);
}

void handle_broadcast(Client *sender, struct message *msg, const char *payload) {
    struct message broadcast = *msg;
    safe_strcpy(broadcast.nick_sender, sender->nickname, NICK_LEN);
    
    for (int i = 0; i < client_manager.count; i++) {
        if (client_manager.clients[i].fd != sender->fd && 
            client_manager.clients[i].has_nickname) {
            send_message(client_manager.clients[i].fd, &broadcast, payload);
        }
    }
}

void handle_unicast(Client *sender, struct message *msg, const char *payload) {
    if (msg->type == FILE_REQUEST || msg->type == FILE_ACCEPT || 
        msg->type == FILE_REJECT || msg->type == FILE_ACK) {
        Client *target = find_client_by_nickname(msg->infos);
        
        if (msg->type == FILE_REQUEST && !target) {
            struct message response = {0};
            response.type = ECHO_SEND;
            safe_strcpy(response.nick_sender, "Server", NICK_LEN);
            snprintf(response.infos, INFOS_LEN, "User %.100s does not exist", msg->infos);
            send_message(sender->fd, &response, NULL);
            return;
        }

        if (target) {
            struct message forward = *msg;
            if (msg->type == FILE_ACCEPT || msg->type == FILE_REJECT) {
                safe_strcpy(forward.infos, sender->nickname, NICK_LEN);
            } else {
                safe_strcpy(forward.nick_sender, sender->nickname, NICK_LEN);
            }
            send_message(target->fd, &forward, payload);
            return;
        }
        return;
    }

    Client *target = find_client_by_nickname(msg->infos);
    if (!target) {
        struct message response = {0};
        response.type = ECHO_SEND;
        safe_strcpy(response.nick_sender, "Server", NICK_LEN);
        snprintf(response.infos, INFOS_LEN, "User %.100s does not exist", msg->infos);
        send_message(sender->fd, &response, NULL);
        return;
    }

    struct message forward = *msg;
    safe_strcpy(forward.nick_sender, sender->nickname, NICK_LEN);
    send_message(target->fd, &forward, payload);
}
void handle_client_message(int fd, struct message *msg, const char *payload) {
    Client *client = find_client_by_fd(fd);
    if (!client) return;
    
    if (!client->has_nickname && msg->type != NICKNAME_NEW) {
        struct message response = {0};
        response.type = ECHO_SEND;
        safe_strcpy(response.nick_sender, "Server", NICK_LEN);
        safe_strcpy(response.infos, "Please set your nickname using /nick <pseudo>", INFOS_LEN);
        send_message(fd, &response, NULL);
        return;
    }
    
    switch (msg->type) {
        case NICKNAME_NEW:
            handle_nickname_new(client, msg);
            break;
            
        case NICKNAME_LIST:
            handle_nickname_list(client);
            break;
            
        case NICKNAME_INFOS:
            handle_nickname_infos(client, msg);
            break;
            
        case BROADCAST_SEND:
            handle_broadcast(client, msg, payload);
            break;
            
        case UNICAST_SEND:
            handle_unicast(client, msg, payload);
            break;
            
        case MULTICAST_CREATE:
            handle_channel_create(client, msg->infos);
            break;
            
        case MULTICAST_LIST:
            handle_channel_list(client);
            break;
            
        case MULTICAST_JOIN:
            handle_channel_join(client, msg->infos);
            break;
            
        case MULTICAST_QUIT:
            handle_channel_quit(client, msg->infos);
            break;
            
        case MULTICAST_SEND:
        case ECHO_SEND:
            handle_channel_message(client, payload);
            break;
        case FILE_REQUEST:
        case FILE_ACCEPT:
        case FILE_REJECT:
        case FILE_ACK:
            handle_unicast(client, msg, payload);
            break;
            
        default:
            printf("Unknown message type: %s\n", msg_type_str[msg->type]);
            break;
    }
}

void remove_client(int fd) {
    for (int i = 0; i < client_manager.count; i++) {
        if (client_manager.clients[i].fd == fd) {
            remove_from_current_channel(&client_manager.clients[i]);
            
            printf("Client %s disconnected\n", 
                   client_manager.clients[i].has_nickname ? 
                   client_manager.clients[i].nickname : "unknown");
            
            close(fd);
            
            client_manager.count--;
            if (i < client_manager.count) {
                client_manager.clients[i] = client_manager.clients[client_manager.count];
            }
            return;
        }
    }
}

void add_client(int fd, struct sockaddr_in addr) {
    if (client_manager.count >= MAX_CLIENTS) {
        fprintf(stderr, "Maximum clients reached\n");
        close(fd);
        return;
    }
    
    Client *client = &client_manager.clients[client_manager.count++];
    client->fd = fd;
    client->addr = addr;
    client->connection_time = time(NULL);
    client->has_nickname = 0;
    client->nickname[0] = '\0';
    client->current_channel[0] = '\0';
    
    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(addr.sin_addr), ip_str, INET_ADDRSTRLEN);
    printf("New client connected from %s:%d\n", ip_str, ntohs(addr.sin_port));
    
    struct message welcome = {0};
    welcome.type = ECHO_SEND;
    safe_strcpy(welcome.nick_sender, "Server", NICK_LEN);
    safe_strcpy(welcome.infos, "Please login with /nick <your pseudo>", INFOS_LEN);
    send_message(fd, &welcome, NULL);
}

void echo_server(int listen_fd) {
    struct pollfd fds[MAX_CLIENTS + 1];
    int nfds = 1;

    fds[0].fd = listen_fd;
    fds[0].events = POLLIN;

    printf("Server is ready for connections...\n");

    while (1) {
        for (int i = 0; i < client_manager.count; i++) {
            fds[i + 1].fd = client_manager.clients[i].fd;
            fds[i + 1].events = POLLIN;
        }
        nfds = client_manager.count + 1;

        int poll_count = poll(fds, nfds, POLL_TIMEOUT);
        if (poll_count < 0) {
            perror("poll()");
            break;
        }
        if (fds[0].revents & POLLIN) {
            struct sockaddr_in client_addr;
            socklen_t addr_len = sizeof(client_addr);
            int new_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &addr_len);
            if (new_fd < 0) {
                perror("accept()");
                continue;
            }
            
            add_client(new_fd, client_addr);
        }

        for (int i = 1; i < nfds; i++) {
            if (fds[i].revents & POLLIN) {
                struct message msg = {0};
                ssize_t rec = recv(fds[i].fd, &msg, sizeof(struct message), 0);
                
                if (rec <= 0) {
                    if (rec == 0) printf("Client disconnected\n");
                    else perror("recv() message struct");
                    remove_client(fds[i].fd);
                    continue;
                }
                
                char payload[MSG_LEN] = {0};
                if (msg.pld_len > 0) {
                    if (recv(fds[i].fd, payload, msg.pld_len, 0) <= 0) {
                        perror("recv() payload");
                        remove_client(fds[i].fd);
                        continue;
                    }
                    payload[msg.pld_len] = '\0';
                }
                
                handle_client_message(fds[i].fd, &msg, payload);
            }
        }
    }

    // Nettoyage
    for (int i = 0; i < client_manager.count; i++) {
        close(client_manager.clients[i].fd);
    }
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int sfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sfd == -1) {
        perror("socket()");
        exit(EXIT_FAILURE);
    }

    int yes = 1;
    if (setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
        perror("setsockopt()");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(atoi(argv[1]));
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("bind()");
        exit(EXIT_FAILURE);
    }

    if ((listen(sfd, SOMAXCONN)) != 0) {
        perror("listen()");
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port %s...\n", argv[1]);
    echo_server(sfd);
    close(sfd);
    return EXIT_SUCCESS;
}
