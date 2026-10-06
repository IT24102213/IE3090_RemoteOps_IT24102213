#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>

#define PORT 9410
#define AUTH_TOKEN "OPS-2213"
#define SID_TAG "SID:3122"
#define BUFFER_SIZE 4096
#define LOG_FILE "remoteops_IT24102213.log"
#define STORAGE_DIR "./agentfiles/IT24102213/"

pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

void log_event(const char *event_type, const char *client_ip, const char *details) {
    pthread_mutex_lock(&log_mutex);
    FILE *fp = fopen(LOG_FILE, "a");
    if (fp) {
        time_t now = time(NULL);
        char time_str[64];
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", localtime(&now));
        fprintf(fp, "[%s] [%s] [%s] %s\n", time_str, event_type, client_ip, details);
        fclose(fp);
    }
    pthread_mutex_unlock(&log_mutex);
}

void get_sysinfo_stats(char *output, size_t max_len) {
    struct sysinfo s_info;
    if (sysinfo(&s_info) != 0) {
        snprintf(output, max_len, "0.10 512 3600");
        return;
    }
    double load = s_info.loads[0] / 65536.0;
    long total_ram = s_info.totalram * s_info.mem_unit / (1024 * 1024);
    long free_ram = s_info.freeram * s_info.mem_unit / (1024 * 1024);
    long used_ram = total_ram - free_ram;
    long uptime = s_info.uptime;

    snprintf(output, max_len, "%.2f %ld %ld", load, used_ram, uptime);
}

void get_process_list(char *output, size_t max_len) {
    FILE *fp = popen("ps -eo comm= | head -n 15 | tr '\n' ',' | sed 's/,$//'", "r");
    if (!fp) {
        snprintf(output, max_len, "systemd,bash,agent_213");
        return;
    }
    if (fgets(output, max_len, fp) == NULL) {
        snprintf(output, max_len, "none");
    }
    pclose(fp);
    output[strcspn(output, "\r\n")] = 0;
}

struct monitor_args {
    char client_ip[INET_ADDRSTRLEN];
    int udp_port;
    volatile int *running;
};

void *udp_monitor_thread(void *arg) {
    struct monitor_args *margs = (struct monitor_args *)arg;
    int udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_sock < 0) {
        free(margs);
        return NULL;
    }

    struct sockaddr_in dest_addr;
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(margs->udp_port);
    inet_pton(AF_INET, margs->client_ip, &dest_addr.sin_addr);

    char stats[128];
    char packet[256];

    while (*(margs->running)) {
        get_sysinfo_stats(stats, sizeof(stats));
        snprintf(packet, sizeof(packet), "SYSINFO %s %s\n", stats, SID_TAG);
        sendto(udp_sock, packet, strlen(packet), 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
        sleep(2);
    }

    close(udp_sock);
    free(margs);
    return NULL;
}

struct client_info {
    int sock;
    char ip[INET_ADDRSTRLEN];
};

void *handle_client(void *arg) {
    struct client_info *cinfo = (struct client_info *)arg;
    int sock = cinfo->sock;
    char client_ip[INET_ADDRSTRLEN];
    strncpy(client_ip, cinfo->ip, INET_ADDRSTRLEN);
    free(cinfo);

    char buffer[BUFFER_SIZE];
    int authenticated = 0;
    pthread_t monitor_tid = 0;
    int monitor_running = 0;

    log_event("CONNECT", client_ip, "New connection established");

    char greeting[128];
    snprintf(greeting, sizeof(greeting), "OK CONNECTED %s\n", SID_TAG);
    send(sock, greeting, strlen(greeting), 0);

    while (1) {
        memset(buffer, 0, BUFFER_SIZE);
        int bytes_received = recv(sock, buffer, BUFFER_SIZE - 1, 0);
        if (bytes_received <= 0) break;

        buffer[strcspn(buffer, "\r\n")] = 0;
        if (strlen(buffer) == 0) continue;

        if (!authenticated) {
            if (strncmp(buffer, "AUTH ", 5) == 0) {
                char *token = buffer + 5;
                if (strcmp(token, AUTH_TOKEN) == 0) {
                    authenticated = 1;
                    char resp[128];
                    snprintf(resp, sizeof(resp), "OK AUTHENTICATED %s\n", SID_TAG);
                    send(sock, resp, strlen(resp), 0);
                    log_event("AUTH_SUCCESS", client_ip, "Token verified successfully");
                } else {
                    char resp[128];
                    snprintf(resp, sizeof(resp), "ERR 001 AUTH FAILED %s\n", SID_TAG);
                    send(sock, resp, strlen(resp), 0);
                    log_event("AUTH_FAILURE", client_ip, "Invalid token attempt");
                    break;
                }
            } else {
                char resp[128];
                snprintf(resp, sizeof(resp), "ERR 001 AUTH FAILED %s\n", SID_TAG);
                send(sock, resp, strlen(resp), 0);
                break;
            }
        } else {
            if (strcmp(buffer, "QUIT") == 0) {
                if (monitor_running) {
                    monitor_running = 0;
                    pthread_join(monitor_tid, NULL);
                }
                char resp[128];
                snprintf(resp, sizeof(resp), "OK BYE %s\n", SID_TAG);
                send(sock, resp, strlen(resp), 0);
                log_event("DISCONNECT", client_ip, "Clean QUIT issued");
                break;
            } else if (strcmp(buffer, "SYSINFO") == 0) {
                char stats[128];
                get_sysinfo_stats(stats, sizeof(stats));
                char resp[256];
                snprintf(resp, sizeof(resp), "OK SYSINFO %s %s\n", stats, SID_TAG);
                send(sock, resp, strlen(resp), 0);
                log_event("SYSINFO", client_ip, stats);
            } else if (strcmp(buffer, "LISTPROC") == 0) {
                char procs[1024];
                get_process_list(procs, sizeof(procs));
                char resp[1200];
                snprintf(resp, sizeof(resp), "OK PROCS %s %s\n", procs, SID_TAG);
                send(sock, resp, strlen(resp), 0);
                log_event("LISTPROC", client_ip, "Process list fetched");
            } else if (strncmp(buffer, "EXEC ", 5) == 0) {
                char *cmd_arg = buffer + 5;
                char real_cmd[64] = "";

                if (strcmp(cmd_arg, "DATE") == 0) strcpy(real_cmd, "date");
                else if (strcmp(cmd_arg, "UPTIME") == 0) strcpy(real_cmd, "uptime");
                else if (strcmp(cmd_arg, "DISKFREE") == 0) strcpy(real_cmd, "df -h / | tail -n 1 | awk '{print $4}'");
                else if (strcmp(cmd_arg, "HOSTNAME") == 0) strcpy(real_cmd, "hostname");
                else if (strcmp(cmd_arg, "WHOAMI") == 0) strcpy(real_cmd, "whoami");

                if (strlen(real_cmd) == 0) {
                    char resp[128];
                    snprintf(resp, sizeof(resp), "ERR 002 COMMAND NOT ALLOWED %s\n", SID_TAG);
                    send(sock, resp, strlen(resp), 0);
                    log_event("EXEC_BLOCKED", client_ip, cmd_arg);
                } else {
                    FILE *fp = popen(real_cmd, "r");
                    char out_buf[256] = "";
                    if (fp) {
                        if (fgets(out_buf, sizeof(out_buf), fp) != NULL) {
                            out_buf[strcspn(out_buf, "\r\n")] = 0;
                        }
                        pclose(fp);
                    }
                    char resp[512];
                    snprintf(resp, sizeof(resp), "OK EXEC_RESULT %s %s\n", out_buf, SID_TAG);
                    send(sock, resp, strlen(resp), 0);
                    log_event("EXEC_OK", client_ip, cmd_arg);
                }
            } else if (strncmp(buffer, "PUT ", 4) == 0) {
                char filename[128];
                long filesize = 0;
                if (sscanf(buffer + 4, "%127s %ld", filename, &filesize) == 2) {
                    mkdir("./agentfiles", 0777);
                    mkdir(STORAGE_DIR, 0777);
                    char filepath[256];
                    snprintf(filepath, sizeof(filepath), "%s%s", STORAGE_DIR, filename);

                    FILE *fp = fopen(filepath, "wb");
                    if (!fp) {
                        char resp[128];
                        snprintf(resp, sizeof(resp), "ERR 004 FILE OPEN FAILED %s\n", SID_TAG);
                        send(sock, resp, strlen(resp), 0);
                    } else {
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

                        char resp[256];
                        snprintf(resp, sizeof(resp), "OK FILE RECEIVED %s %s\n", filename, SID_TAG);
                        send(sock, resp, strlen(resp), 0);
                        log_event("PUT", client_ip, filename);
                    }
                }
            } else if (strncmp(buffer, "GET ", 4) == 0) {
                char filename[128];
                if (sscanf(buffer + 4, "%127s", filename) == 1) {
                    char filepath[256];
                    snprintf(filepath, sizeof(filepath), "%s%s", STORAGE_DIR, filename);

                    FILE *fp = fopen(filepath, "rb");
                    if (!fp) {
                        char resp[128];
                        snprintf(resp, sizeof(resp), "ERR 005 FILE NOT FOUND %s\n", SID_TAG);
                        send(sock, resp, strlen(resp), 0);
                        log_event("GET_ERR", client_ip, filename);
                    } else {
                        fseek(fp, 0, SEEK_END);
                        long filesize = ftell(fp);
                        fseek(fp, 0, SEEK_SET);

                        char header[256];
                        snprintf(header, sizeof(header), "OK FILE SEND %s %ld %s\n", filename, filesize, SID_TAG);
                        send(sock, header, strlen(header), 0);

                        usleep(50000);
                        char file_buf[BUFFER_SIZE];
                        size_t n;
                        while ((n = fread(file_buf, 1, sizeof(file_buf), fp)) > 0) {
                            send(sock, file_buf, n, 0);
                        }
                        fclose(fp);
                        log_event("GET_OK", client_ip, filename);
                    }
                }
            } else if (strncmp(buffer, "MONITOR START ", 14) == 0) {
                int udp_port = atoi(buffer + 14);
                if (udp_port > 0 && !monitor_running) {
                    monitor_running = 1;
                    struct monitor_args *margs = malloc(sizeof(struct monitor_args));
                    strncpy(margs->client_ip, client_ip, INET_ADDRSTRLEN);
                    margs->udp_port = udp_port;
                    margs->running = &monitor_running;

                    pthread_create(&monitor_tid, NULL, udp_monitor_thread, margs);
                    char resp[128];
                    snprintf(resp, sizeof(resp), "OK MONITOR STARTED %s\n", SID_TAG);
                    send(sock, resp, strlen(resp), 0);
                    log_event("MONITOR_START", client_ip, "UDP monitoring started");
                }
            } else if (strcmp(buffer, "MONITOR STOP") == 0) {
                if (monitor_running) {
                    monitor_running = 0;
                    pthread_join(monitor_tid, NULL);
                }
                char resp[128];
                snprintf(resp, sizeof(resp), "OK MONITOR STOPPED %s\n", SID_TAG);
                send(sock, resp, strlen(resp), 0);
                log_event("MONITOR_STOP", client_ip, "UDP monitoring stopped");
            } else {
                char resp[128];
                snprintf(resp, sizeof(resp), "ERR 999 INVALID COMMAND %s\n", SID_TAG);
                send(sock, resp, strlen(resp), 0);
            }
        }
    }

    if (monitor_running) {
        monitor_running = 0;
        pthread_join(monitor_tid, NULL);
    }
    close(sock);
    return NULL;
}

int main() {
    int server_fd;
    struct sockaddr_in address;
    int opt = 1;

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    listen(server_fd, 10);
    mkdir("./agentfiles", 0777);
    mkdir(STORAGE_DIR, 0777);

    printf("[Agent] Running on port %d with Auth Token %s and %s\n", PORT, AUTH_TOKEN, SID_TAG);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int client_sock = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len);
        if (client_sock < 0) continue;

        struct client_info *cinfo = malloc(sizeof(struct client_info));
        cinfo->sock = client_sock;
        inet_ntop(AF_INET, &client_addr.sin_addr, cinfo->ip, INET_ADDRSTRLEN);

        pthread_t tid;
        pthread_create(&tid, NULL, handle_client, cinfo);
        pthread_detach(tid);
    }

    close(server_fd);
    return 0;
}
