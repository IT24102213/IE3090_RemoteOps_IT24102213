#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define DEFAULT_PORT 9410
#define DEFAULT_IP "127.0.0.1"
#define AUTH_TOKEN "OPS-2213"
#define BUFFER_SIZE 1024

int main(int argc, char *argv[]) {
    int sock;
    struct sockaddr_in server_addr;
    char buffer[BUFFER_SIZE];
    char *server_ip = DEFAULT_IP;
    int port = DEFAULT_PORT;

    if (argc >= 2) server_ip = argv[1];
    if (argc >= 3) port = atoi(argv[2]);

    // Create TCP socket
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
        perror("Invalid server address");
        close(sock);
        exit(EXIT_FAILURE);
    }

    // Connect to Agent
    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection to Agent failed");
        close(sock);
        exit(EXIT_FAILURE);
    }

    printf("[Controller] Connected to Agent at %s:%d\n", server_ip, port);

    // Read initial greeting (SID)
    memset(buffer, 0, BUFFER_SIZE);
    int bytes = recv(sock, buffer, BUFFER_SIZE - 1, 0);
    if (bytes > 0) {
        buffer[bytes] = '\0';
        printf("[Agent Greeting] %s", buffer);
    }

    // Send authentication token
    char auth_cmd[128];
    snprintf(auth_cmd, sizeof(auth_cmd), "AUTH %s\n", AUTH_TOKEN);
    send(sock, auth_cmd, strlen(auth_cmd), 0);

    // Receive auth status
    memset(buffer, 0, BUFFER_SIZE);
    bytes = recv(sock, buffer, BUFFER_SIZE - 1, 0);
    if (bytes > 0) {
        buffer[bytes] = '\0';
        printf("[Auth Response] %s", buffer);
        if (strstr(buffer, "AUTH_OK") == NULL) {
            printf("[Controller] Authentication failed. Exiting.\n");
            close(sock);
            return 1;
        }
    }

    printf("[Controller] Successfully authenticated! Enter command (e.g., QUIT):\n> ");

    // Interactive command input
    while (fgets(buffer, BUFFER_SIZE, stdin) != NULL) {
        send(sock, buffer, strlen(buffer), 0);

        if (strncmp(buffer, "QUIT", 4) == 0) {
            break;
        }

        memset(buffer, 0, BUFFER_SIZE);
        bytes = recv(sock, buffer, BUFFER_SIZE - 1, 0);
        if (bytes <= 0) break;
        buffer[bytes] = '\0';
        printf("[Agent Response] %s> ", buffer);
    }

    close(sock);
    printf("[Controller] Connection closed.\n");
    return 0;
}
