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
#define BUFFER_SIZE 2048

void send_file_put(int sock, const char *local_file) {
    FILE *fp = fopen(local_file, "rb");
    if (!fp) {
        printf("[Controller] Local file not found: %s\n", local_file);
        return;
    }

    fseek(fp, 0, SEEK_END);
    long filesize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    char put_cmd[512];
    snprintf(put_cmd, sizeof(put_cmd), "PUT %s %ld\n", local_file, filesize);
    send(sock, put_cmd, strlen(put_cmd), 0);

    char resp[128];
    memset(resp, 0, sizeof(resp));
    recv(sock, resp, sizeof(resp) - 1, 0);

    if (strncmp(resp, "READY_FOR_DATA", 14) == 0) {
        char buffer[BUFFER_SIZE];
        size_t bytes_read;
        while ((bytes_read = fread(buffer, 1, sizeof(buffer), fp)) > 0) {
            send(sock, buffer, bytes_read, 0);
        }
        memset(resp, 0, sizeof(resp));
        recv(sock, resp, sizeof(resp) - 1, 0);
        printf("[Controller PUT Status] %s", resp);
    } else {
        printf("[Controller PUT Error] %s\n", resp);
    }

    fclose(fp);
}

void get_file_download(int sock, const char *remote_file, const char *saved_as) {
    char get_cmd[512];
    snprintf(get_cmd, sizeof(get_cmd), "GET %s\n", remote_file);
    send(sock, get_cmd, strlen(get_cmd), 0);

    char header[128];
    memset(header, 0, sizeof(header));
    recv(sock, header, sizeof(header) - 1, 0);

    if (strncmp(header, "FILE_DATA", 9) == 0) {
        long filesize = 0;
        sscanf(header + 10, "%ld", &filesize);

        FILE *fp = fopen(saved_as, "wb");
        if (!fp) {
            printf("[Controller] Failed to create local file: %s\n", saved_as);
            return;
        }

        char buffer[BUFFER_SIZE];
        long received = 0;
        while (received < filesize) {
            int to_read = (filesize - received < BUFFER_SIZE) ? (filesize - received) : BUFFER_SIZE;
            int n = recv(sock, buffer, to_read, 0);
            if (n <= 0) break;
            fwrite(buffer, 1, n, fp);
            received += n;
        }

        fclose(fp);
        printf("[Controller GET Status] Downloaded %s successfully (%ld bytes)\n", saved_as, filesize);
    } else {
        printf("[Controller GET Error] %s\n", header);
    }
}

int main(int argc, char *argv[]) {
    int sock;
    struct sockaddr_in server_addr;
    char buffer[BUFFER_SIZE];
    char *server_ip = DEFAULT_IP;
    int port = DEFAULT_PORT;

    if (argc >= 2) server_ip = argv[1];
    if (argc >= 3) port = atoi(argv[2]);

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

    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection failed");
        close(sock);
        exit(EXIT_FAILURE);
    }

    memset(buffer, 0, BUFFER_SIZE);
    recv(sock, buffer, BUFFER_SIZE - 1, 0);
    printf("[Agent Greeting] %s", buffer);

    char auth_cmd[128];
    snprintf(auth_cmd, sizeof(auth_cmd), "AUTH %s\n", AUTH_TOKEN);
    send(sock, auth_cmd, strlen(auth_cmd), 0);

    memset(buffer, 0, BUFFER_SIZE);
    recv(sock, buffer, BUFFER_SIZE - 1, 0);
    printf("[Auth Response] %s", buffer);

    if (strstr(buffer, "AUTH_OK") == NULL) {
        printf("Auth failed.\n");
        close(sock);
        return 1;
    }

    printf("[Controller] Commands: EXEC <cmd> | PUT <file> | GET <remote_file> <save_as> | QUIT\n> ");

    while (fgets(buffer, BUFFER_SIZE, stdin) != NULL) {
        buffer[strcspn(buffer, "\r\n")] = 0;
        if (strlen(buffer) == 0) {
            printf("> ");
            continue;
        }

        if (strcmp(buffer, "QUIT") == 0) {
            send(sock, "QUIT\n", 5, 0);
            break;
        } else if (strncmp(buffer, "PUT ", 4) == 0) {
            char filename[256];
            sscanf(buffer + 4, "%255s", filename);
            send_file_put(sock, filename);
            printf("> ");
        } else if (strncmp(buffer, "GET ", 4) == 0) {
            char remote_file[256], saved_as[256];
            if (sscanf(buffer + 4, "%255s %255s", remote_file, saved_as) == 2) {
                get_file_download(sock, remote_file, saved_as);
            } else {
                printf("Usage: GET <remote_file> <save_as>\n");
            }
            printf("> ");
        } else {
            strcat(buffer, "\n");
            send(sock, buffer, strlen(buffer), 0);
            memset(buffer, 0, BUFFER_SIZE);
            int bytes = recv(sock, buffer, BUFFER_SIZE - 1, 0);
            if (bytes > 0) {
                buffer[bytes] = '\0';
                printf("[Agent Response] %s> ", buffer);
            }
        }
    }

    close(sock);
    return 0;
}
