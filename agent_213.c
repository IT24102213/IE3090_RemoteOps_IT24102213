#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PORT 9410
#define AUTH_TOKEN "OPS-2213"
#define SID_TAG "SID:3122"
#define BUFFER_SIZE 1024

void *handle_client(void *socket_desc) {
    int sock = *(int *)socket_desc;
    free(socket_desc);

    char buffer[BUFFER_SIZE];
    int authenticated = 0;

    // Send greeting with personalized Session ID
    char greeting[128];
    snprintf(greeting, sizeof(greeting), "OK CONNECTED %s\n", SID_TAG);
    send(sock, greeting, strlen(greeting), 0);

    while (1) {
        memset(buffer, 0, BUFFER_SIZE);
        int bytes_received = recv(sock, buffer, BUFFER_SIZE - 1, 0);
        if (bytes_received <= 0) {
            break; // Client disconnected or error
        }

        // Remove trailing newline / carriage return
        buffer[strcspn(buffer, "\r\n")] = 0;

        if (strlen(buffer) == 0) continue;

        // Check authentication status
        if (!authenticated) {
            if (strncmp(buffer, "AUTH ", 5) == 0) {
                char *provided_token = buffer + 5;
                if (strcmp(provided_token, AUTH_TOKEN) == 0) {
                    authenticated = 1;
                    char *resp = "AUTH_OK\n";
                    send(sock, resp, strlen(resp), 0);
                    printf("[Agent] Client authenticated successfully on %s.\n", SID_TAG);
                } else {
                    char *resp = "AUTH_FAILED\n";
                    send(sock, resp, strlen(resp), 0);
                    printf("[Agent] Authentication failed: Invalid token.\n");
                    break; // Close connection on failed authentication
                }
            } else {
                char *resp = "ERROR NOT_AUTHENTICATED\n";
                send(sock, resp, strlen(resp), 0);
            }
        } else {
            // Once authenticated, client can execute commands
            if (strcmp(buffer, "QUIT") == 0) {
                char *resp = "BYE\n";
                send(sock, resp, strlen(resp), 0);
                break;
            } else {
                char *resp = "COMMAND_RECEIVED\n";
                send(sock, resp, strlen(resp), 0);
            }
        }
    }

    close(sock);
    return NULL;
}

int main() {
    int server_fd;
    struct sockaddr_in address;
    int opt = 1;

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt failed");
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 10) < 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }

    printf("[Agent] Server running on port %d with Auth Token %s...\n", PORT, AUTH_TOKEN);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int client_sock = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len);
        if (client_sock < 0) {
            perror("Accept failed");
            continue;
        }

        pthread_t tid;
        int *new_sock = malloc(sizeof(int));
        *new_sock = client_sock;

        if (pthread_create(&tid, NULL, handle_client, (void *)new_sock) < 0) {
            perror("Thread creation failed");
            free(new_sock);
            close(client_sock);
        } else {
            pthread_detach(tid);
        }
    }

    close(server_fd);
    return 0;
}
