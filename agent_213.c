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
#define BUFFER_SIZE 2048

void execute_system_command(int sock, const char *cmd) {
    FILE *fp;
    char path[1024];

    if (strstr(cmd, "rm -rf") || strstr(cmd, ":(){ :|:& };:")) {
        char *err_msg = "ERROR COMMAND_BLOCKED_SECURITY\n";
        send(sock, err_msg, strlen(err_msg), 0);
        return;
    }

    fp = popen(cmd, "r");
    if (fp == NULL) {
        char *err_msg = "ERROR EXEC_FAILED\n";
        send(sock, err_msg, strlen(err_msg), 0);
        return;
    }

    char *header = "--- COMMAND OUTPUT START ---\n";
    send(sock, header, strlen(header), 0);

    while (fgets(path, sizeof(path), fp) != NULL) {
        send(sock, path, strlen(path), 0);
    }

    pclose(fp);
    char *footer = "--- COMMAND OUTPUT END ---\n";
    send(sock, footer, strlen(footer), 0);
}

void handle_file_put(int sock, const char *filename, long filesize) {
    FILE *fp = fopen(filename, "wb");
    if (!fp) {
        char *resp = "ERROR FILE_OPEN_FAILED\n";
        send(sock, resp, strlen(resp), 0);
        return;
    }

    char *resp = "READY_FOR_DATA\n";
    send(sock, resp, strlen(resp), 0);

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
    char *done = "FILE_STORED_OK\n";
    send(sock, done, strlen(done), 0);
    printf("[Agent] Stored uploaded file: %s (%ld bytes)\n", filename, filesize);
}

void handle_file_get(int sock, const char *filename) {
    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        char *resp = "ERROR FILE_NOT_FOUND\n";
        send(sock, resp, strlen(resp), 0);
        return;
    }

    fseek(fp, 0, SEEK_END);
    long filesize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    char header[128];
    snprintf(header, sizeof(header), "FILE_DATA %ld\n", filesize);
    send(sock, header, strlen(header), 0);

    // Header එක සහ Data වෙන්ව හඳුනාගැනීම සඳහා කෙටි ප්‍රමාදයක් (delay)
    usleep(50000);

    char buffer[BUFFER_SIZE];
    size_t bytes_read;
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), fp)) > 0) {
        send(sock, buffer, bytes_read, 0);
    }

    fclose(fp);
    printf("[Agent] Sent file: %s (%ld bytes)\n", filename, filesize);
}

void *handle_client(void *socket_desc) {
    int sock = *(int *)socket_desc;
    free(socket_desc);

    char buffer[BUFFER_SIZE];
    int authenticated = 0;

    char greeting[128];
    snprintf(greeting, sizeof(greeting), "OK CONNECTED %s\n", SID_TAG);
    send(sock, greeting, strlen(greeting), 0);

    while (1) {
        memset(buffer, 0, BUFFER_SIZE);
        int bytes_received = recv(sock, buffer, BUFFER_SIZE - 1, 0);
        if (bytes_received <= 0) {
            break;
        }

        buffer[strcspn(buffer, "\r\n")] = 0;
        if (strlen(buffer) == 0) continue;

        if (!authenticated) {
            if (strncmp(buffer, "AUTH ", 5) == 0) {
                char *provided_token = buffer + 5;
                if (strcmp(provided_token, AUTH_TOKEN) == 0) {
                    authenticated = 1;
                    char *resp = "AUTH_OK\n";
                    send(sock, resp, strlen(resp), 0);
                    printf("[Agent] Client authenticated on %s.\n", SID_TAG);
                } else {
                    char *resp = "AUTH_FAILED\n";
                    send(sock, resp, strlen(resp), 0);
                    break;
                }
            } else {
                char *resp = "ERROR NOT_AUTHENTICATED\n";
                send(sock, resp, strlen(resp), 0);
            }
        } else {
            if (strcmp(buffer, "QUIT") == 0) {
                char *resp = "BYE\n";
                send(sock, resp, strlen(resp), 0);
                break;
            } else if (strncmp(buffer, "EXEC ", 5) == 0) {
                char *cmd = buffer + 5;
                printf("[Agent] Executing command: %s\n", cmd);
                execute_system_command(sock, cmd);
            } else if (strncmp(buffer, "PUT ", 4) == 0) {
                char filename[256];
                long filesize = 0;
                if (sscanf(buffer + 4, "%255s %ld", filename, &filesize) == 2) {
                    handle_file_put(sock, filename, filesize);
                } else {
                    char *err = "ERROR INVALID_PUT_FORMAT\n";
                    send(sock, err, strlen(err), 0);
                }
            } else if (strncmp(buffer, "GET ", 4) == 0) {
                char filename[256];
                if (sscanf(buffer + 4, "%255s", filename) == 1) {
                    handle_file_get(sock, filename);
                } else {
                    char *err = "ERROR INVALID_GET_FORMAT\n";
                    send(sock, err, strlen(err), 0);
                }
            } else {
                char *resp = "ERROR INVALID_COMMAND\n";
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

    printf("[Agent] Server listening on port %d with Auth Token %s...\n", PORT, AUTH_TOKEN);

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
