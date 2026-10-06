#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>

#define BUFFER_SIZE 4096
#define AUTH_TOKEN "OPS-2213"

volatile int udp_listener_active = 0;
pthread_t udp_listener_tid = 0;

void *udp_listener_thread(void *arg) {
    int port = *(int *)arg;
    free(arg);

    int udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in saddr;
    memset(&saddr, 0, sizeof(saddr));
    saddr.sin_family = AF_INET;
    saddr.sin_addr.s_addr = INADDR_ANY;
    saddr.sin_port = htons(port);

    if (bind(udp_sock, (struct sockaddr *)&saddr, sizeof(saddr)) < 0) {
        perror("UDP bind failed");
        close(udp_sock);
        return NULL;
    }

    char buf[512];
    while (udp_listener_active) {
        struct timeval tv = {1, 0};
        setsockopt(udp_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        int n = recvfrom(udp_sock, buf, sizeof(buf) - 1, 0, NULL, NULL);
        if (n > 0) {
            buf[n] = '\0';
            printf("\n[UDP MONITOR DATAGRAM] %s> ", buf);
            fflush(stdout);
        }
    }
    close(udp_sock);
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        printf("Usage: %s <Agent_IP> <Port>\n", argv[0]);
        return 1;
    }

    char *server_ip = argv[1];
    int port = atoi(argv[2]);

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    inet_pton(AF_INET, server_ip, &serv_addr.sin_addr);

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Connection Failed");
        return 1;
    }

    char buffer[BUFFER_SIZE];
    int bytes = recv(sock, buffer, BUFFER_SIZE - 1, 0);
    buffer[bytes] = '\0';
    printf("[Agent Greeting] %s", buffer);

    char auth_cmd[128];
    snprintf(auth_cmd, sizeof(auth_cmd), "AUTH %s\n", AUTH_TOKEN);
    send(sock, auth_cmd, strlen(auth_cmd), 0);

    bytes = recv(sock, buffer, BUFFER_SIZE - 1, 0);
    buffer[bytes] = '\0';
    printf("[Auth Response] %s", buffer);

    if (strncmp(buffer, "OK AUTHENTICATED", 16) != 0) {
        printf("Authentication failed. Exiting.\n");
        close(sock);
        return 1;
    }

    printf("\n=== RemoteOps Controller Connected ===\n");
    printf("Commands: SYSINFO | LISTPROC | EXEC <cmd> | PUT <file> | GET <file> | MONITOR START <port> | MONITOR STOP | QUIT\n");

    while (1) {
        printf("> ");
        fflush(stdout);

        char user_input[512];
        if (!fgets(user_input, sizeof(user_input), stdin)) break;
        user_input[strcspn(user_input, "\r\n")] = 0;
        if (strlen(user_input) == 0) continue;

        if (strcmp(user_input, "QUIT") == 0) {
            send(sock, "QUIT\n", 5, 0);
            recv(sock, buffer, BUFFER_SIZE - 1, 0);
            if (udp_listener_active) {
                udp_listener_active = 0;
                pthread_join(udp_listener_tid, NULL);
            }
            break;
        } else if (strncmp(user_input, "PUT ", 4) == 0) {
            char filename[128];
            sscanf(user_input + 4, "%127s", filename);

            FILE *fp = fopen(filename, "rb");
            if (!fp) {
                printf("[Error] Cannot open local file %s\n", filename);
                continue;
            }
            fseek(fp, 0, SEEK_END);
            long filesize = ftell(fp);
            fseek(fp, 0, SEEK_SET);

            char cmd[256];
            snprintf(cmd, sizeof(cmd), "PUT %s %ld\n", filename, filesize);
            send(sock, cmd, strlen(cmd), 0);

            usleep(20000);
            char file_buf[BUFFER_SIZE];
            size_t n;
            while ((n = fread(file_buf, 1, sizeof(file_buf), fp)) > 0) {
                send(sock, file_buf, n, 0);
            }
            fclose(fp);

            bytes = recv(sock, buffer, BUFFER_SIZE - 1, 0);
            buffer[bytes] = '\0';
            printf("[Agent Response] %s", buffer);
        } else if (strncmp(user_input, "GET ", 4) == 0) {
            char filename[128];
            sscanf(user_input + 4, "%127s", filename);

            char cmd[256];
            snprintf(cmd, sizeof(cmd), "GET %s\n", filename);
            send(sock, cmd, strlen(cmd), 0);

            bytes = recv(sock, buffer, BUFFER_SIZE - 1, 0);
            buffer[bytes] = '\0';

            if (strncmp(buffer, "OK FILE SEND", 12) == 0) {
                char fname[128];
                long filesize = 0;
                sscanf(buffer + 13, "%127s %ld", fname, &filesize);

                char out_name[256];
                snprintf(out_name, sizeof(out_name), "downloaded_%s", fname);
                FILE *fp = fopen(out_name, "wb");

                char file_buf[BUFFER_SIZE];
                long received = 0;
                while (received < filesize) {
                    int to_read = (filesize - received < BUFFER_SIZE) ? (filesize - received) : BUFFER_SIZE;
                    int n = recv(sock, file_buf, to_read, 0);
                    if (n <= 0) break;
                    fwrite(file_buf, 1, n, fp);
                    received += n;
                }
                fclose(fp);
                printf("[Controller] Successfully received %s (%ld bytes) -> saved as %s\n", fname, filesize, out_name);
            } else {
                printf("[Agent Response] %s", buffer);
            }
        } else if (strncmp(user_input, "MONITOR START ", 14) == 0) {
            int uport = atoi(user_input + 14);
            char cmd[64];
            snprintf(cmd, sizeof(cmd), "MONITOR START %d\n", uport);
            send(sock, cmd, strlen(cmd), 0);

            bytes = recv(sock, buffer, BUFFER_SIZE - 1, 0);
            buffer[bytes] = '\0';
            printf("[Agent Response] %s", buffer);

            if (strncmp(buffer, "OK MONITOR STARTED", 18) == 0 && !udp_listener_active) {
                udp_listener_active = 1;
                int *port_arg = malloc(sizeof(int));
                *port_arg = uport;
                pthread_create(&udp_listener_tid, NULL, udp_listener_thread, port_arg);
            }
        } else if (strcmp(user_input, "MONITOR STOP") == 0) {
            send(sock, "MONITOR STOP\n", 13, 0);
            bytes = recv(sock, buffer, BUFFER_SIZE - 1, 0);
            buffer[bytes] = '\0';
            printf("[Agent Response] %s", buffer);

            if (udp_listener_active) {
                udp_listener_active = 0;
                pthread_join(udp_listener_tid, NULL);
            }
        } else {
            char cmd[1024];
            snprintf(cmd, sizeof(cmd), "%s\n", user_input);
            send(sock, cmd, strlen(cmd), 0);

            bytes = recv(sock, buffer, BUFFER_SIZE - 1, 0);
            if (bytes > 0) {
                buffer[bytes] = '\0';
                printf("[Agent Response] %s", buffer);
            }
        }
    }

    close(sock);
    return 0;
}
