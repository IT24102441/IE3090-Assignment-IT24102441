#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/sysinfo.h>
#include <time.h>

#define PORT 9410
#define MAX_CONNECTIONS 5

// Global mutex and log file for thread-safe writing[cite: 2, 4]
pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;
const char *LOG_FILE = "remoteops_IT24102441.log";

typedef struct {
    int socket;
    char ip[INET_ADDRSTRLEN];
} ClientData;

// Thread-safe logging function
void log_action(const char *ip, const char *action) {
    pthread_mutex_lock(&log_mutex);
    FILE *f = fopen(LOG_FILE, "a");
    if (f) {
        time_t now = time(NULL);
        char *dt = ctime(&now);
        dt[strcspn(dt, "\n")] = 0; // Strip newline from time
        fprintf(f, "[%s] IP: %s - %s\n", dt, ip, action);
        fclose(f);
    }
    pthread_mutex_unlock(&log_mutex);
}
// Struct for the UDP monitor thread
typedef struct {
    char ip[INET_ADDRSTRLEN];
    int active;
} MonitorData;

// UDP Streaming Thread
void *monitor_thread(void *arg) {
    MonitorData *md = (MonitorData *)arg;
    int udp_socket = socket(AF_INET, SOCK_DGRAM, 0);
    
    struct sockaddr_in dest_addr;
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(9411); 
    inet_pton(AF_INET, md->ip, &dest_addr.sin_addr);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(9411); // Controller's UDP listening port
    inet_pton(AF_INET, md->ip, &dest_addr.sin_addr);

    while(md->active) {
        struct sysinfo info;
        sysinfo(&info);
        char payload[256];
        snprintf(payload, sizeof(payload), "MONITOR_UPDATE CPU:%.2f MEM:%ldMB\n", 
                 info.loads[0] / 65536.0, (info.totalram - info.freeram) / (1024 * 1024));
        
        sendto(udp_socket, payload, strlen(payload), 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
        sleep(5); // Wait 5 seconds between streams
    }
    
    close(udp_socket);
    free(md); // Clean up memory when stopped
    pthread_exit(NULL);
}
void *client_handler(void *arg) {
    ClientData *client = (ClientData *)arg;
    int client_socket = client->socket;
    char client_ip[INET_ADDRSTRLEN];
    strcpy(client_ip, client->ip);
    MonitorData *mon_data = NULL;
    free(client);

    char buffer[1024];
    char log_msg[2048];
    
    log_action(client_ip, "Connected");

    ssize_t bytes_read = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
    if (bytes_read > 0) {
        buffer[bytes_read] = '\0';
        buffer[strcspn(buffer, "\r\n")] = 0;

        if (strcmp(buffer, "AUTH OPS-2441") == 0) {
            log_action(client_ip, "Authentication Successful");
            char *success_msg = "OK AUTHENTICATED SID:1442\n";
            send(client_socket, success_msg, strlen(success_msg), 0);
            
            while ((bytes_read = recv(client_socket, buffer, sizeof(buffer) - 1, 0)) > 0) {
                buffer[bytes_read] = '\0';
                buffer[strcspn(buffer, "\r\n")] = 0;
                
                snprintf(log_msg, sizeof(log_msg), "Command Received: %s", buffer);
                log_action(client_ip, log_msg);
                
                if (strcmp(buffer, "SYSINFO") == 0) {
                    struct sysinfo info;
                    sysinfo(&info);
                    char response[256];
                    snprintf(response, sizeof(response), "OK SYSINFO %.2f %ld %ld SID:1442\n", 
                             info.loads[0] / 65536.0, (info.totalram - info.freeram) / (1024 * 1024), info.uptime);
                            // Optional Feature: Persist SYSINFO snapshot
                            FILE *hist_file = fopen("./agentfiles/IT24102441/history.txt", "a");
                            if (hist_file) {
                                fprintf(hist_file, "[SYSINFO] %s", response);
                                fclose(hist_file);
                            }
			    send(client_socket, response, strlen(response), 0);
                    
                } else if (strcmp(buffer, "LISTPROC") == 0) {
                    FILE *fp = popen("ps -eo comm --no-headers | head -n 5 | tr '\\n' ','", "r");
                    char procs[512] = {0};
                    if (fp) {
                        fgets(procs, sizeof(procs)-1, fp);
                        pclose(fp);
                        char response[1024];
                        snprintf(response, sizeof(response), "OK PROCS %s SID:1442\n", procs);
                        FILE *hist_file = fopen("./agentfiles/IT24102441/history.txt", "a");
           		 if (hist_file) {
                		fprintf(hist_file, "[LISTPROC] %s", response);
                		fclose(hist_file);
           		 }
		        send(client_socket, response, strlen(response), 0);
                    }
                    
                } else if (strncmp(buffer, "EXEC ", 5) == 0) {
                    char *cmd = buffer + 5; 
                    char *sys_cmd = NULL;
                    
                    if (strcmp(cmd, "DATE") == 0) sys_cmd = "date";
                    else if (strcmp(cmd, "UPTIME") == 0) sys_cmd = "uptime -p";
                    else if (strcmp(cmd, "DISKFREE") == 0) sys_cmd = "df -h / | tail -1 | awk '{print $4}'";
                    else if (strcmp(cmd, "HOSTNAME") == 0) sys_cmd = "hostname";
                    else if (strcmp(cmd, "WHOAMI") == 0) sys_cmd = "whoami";
                    
                    if (sys_cmd != NULL) {
                        FILE *fp = popen(sys_cmd, "r");
                        if (fp) {
                            char output[512] = {0};
                            fgets(output, sizeof(output)-1, fp); 
                            pclose(fp);
                            output[strcspn(output, "\r\n")] = 0; 
                            char response[1024];
                            snprintf(response, sizeof(response), "OK EXEC_RESULT %s SID:1442\n", output);
                            send(client_socket, response, strlen(response), 0);
                        }
                    } else {
                        char *err_msg = "ERR 002 COMMAND NOT ALLOWED SID:1442\n";
                        send(client_socket, err_msg, strlen(err_msg), 0);
                    }
                    } else if (strncmp(buffer, "PUT ", 4) == 0) {
                    char filename[256];
                    long file_size;
                    
                    // Parse the PUT <filename> <size> command
                    if (sscanf(buffer + 4, "%255s %ld", filename, &file_size) == 2) {
                        char filepath[512];
                        snprintf(filepath, sizeof(filepath), "./agentfiles/IT24102441/%s", filename);
                        
                        FILE *file = fopen(filepath, "wb"); // Open in binary write mode
                        if (file) {
                            long bytes_remaining = file_size;
                            char file_buf[1024];
                            size_t chunk_size;
                            
                            // The exact byte-counting loop
                            while (bytes_remaining > 0) {
                                chunk_size = (bytes_remaining <(long)sizeof(file_buf)) ? bytes_remaining : (long)sizeof(file_buf);
                                ssize_t received = recv(client_socket, file_buf, chunk_size, 0);
                                
                                if (received <= 0) break; // Network error or client disconnected
                                
                                fwrite(file_buf, 1, received, file);
                                bytes_remaining -= received;
                            }
                            fclose(file);
                            
                            if (bytes_remaining == 0) {
                                char response[512];
                                snprintf(response, sizeof(response), "OK PUT %s SID:1442\n", filename);
                                send(client_socket, response, strlen(response), 0);
                                
                                // Log the successful upload
                                char log_up[512];
                                snprintf(log_up, sizeof(log_up), "File Uploaded: %s (%ld bytes)", filename, file_size);
                                log_action(client_ip, log_up);
                            } else {
                                char *err = "ERR 003 UPLOAD INCOMPLETE SID:1442\n";
                                send(client_socket, err, strlen(err), 0);
                            }
                        } else {
                            char *err = "ERR 004 FILE CREATION FAILED SID:1442\n";
                            send(client_socket, err, strlen(err), 0);
                        }
                    } else {
                        char *err = "ERR 005 INVALID PUT FORMAT SID:1442\n";
                        send(client_socket, err, strlen(err), 0);
                    }
} else if (strncmp(buffer, "GET ", 4) == 0) {
                    char filename[256];
                    
                    if (sscanf(buffer + 4, "%255s", filename) == 1) {
                        char filepath[512];
                        snprintf(filepath, sizeof(filepath), "./agentfiles/IT24102441/%s", filename);
                        
                        FILE *file = fopen(filepath, "rb"); 
                        if (file) {
                            fseek(file, 0, SEEK_END);
                            long file_size = ftell(file);
                            fseek(file, 0, SEEK_SET);
                            
                            char response[256];
                            snprintf(response, sizeof(response), "OK GET %ld SID:1442\n", file_size);
                            send(client_socket, response, strlen(response), 0);
                            
                            char file_buf[1024];
                            size_t bytes_read_file;
                            while ((bytes_read_file = fread(file_buf, 1, sizeof(file_buf), file)) > 0) {
                                send(client_socket, file_buf, bytes_read_file, 0);
                            }
                            fclose(file);
                            
                            char log_down[512];
                            snprintf(log_down, sizeof(log_down), "File Downloaded: %s (%ld bytes)", filename, file_size);
                            log_action(client_ip, log_down);
                        } else {
                            char *err = "ERR 006 FILE NOT FOUND SID:1442\n";
                            send(client_socket, err, strlen(err), 0);
                        }
                    } else {
                        char *err = "ERR 007 INVALID GET FORMAT SID:1442\n";
                        send(client_socket, err, strlen(err), 0);
                    }
                   } else if (strcmp(buffer, "MONITOR START") == 0) {
                    if (mon_data == NULL) {
                        mon_data = malloc(sizeof(MonitorData));
                        strcpy(mon_data->ip, client_ip);
                        mon_data->active = 1;
                        
                        pthread_t mon_tid;
                        pthread_create(&mon_tid, NULL, monitor_thread, mon_data);
                        pthread_detach(mon_tid); // Run independently 
                        
                        char *resp = "OK MONITOR STARTED SID:1442\n";
                        send(client_socket, resp, strlen(resp), 0);
                        log_action(client_ip, "MONITOR START initiated");
                    } else {
                        char *err = "ERR 008 MONITOR ALREADY RUNNING SID:1442\n";
                        send(client_socket, err, strlen(err), 0);
                    }
                    
                } else if (strcmp(buffer, "MONITOR STOP") == 0) {
                    if (mon_data != NULL) {
                        mon_data->active = 0; // Signals the while-loop to terminate
                        mon_data = NULL;
                        
                        char *resp = "OK MONITOR STOPPED SID:1442\n";
                        send(client_socket, resp, strlen(resp), 0);
                        log_action(client_ip, "MONITOR STOP initiated");
                    } else {
                        char *err = "ERR 009 MONITOR NOT RUNNING SID:1442\n";
                        send(client_socket, err, strlen(err), 0);
                    }
                } else if (strcmp(buffer, "QUIT") == 0) {
                    char *bye_msg = "OK BYE SID:1442\n";
                    send(client_socket, bye_msg, strlen(bye_msg), 0);
                    break; 
                    
                } else {
                    char *err_msg = "ERR 002 COMMAND NOT ALLOWED SID:1442\n";
                    send(client_socket, err_msg, strlen(err_msg), 0);
                }
            }
        } else {
            log_action(client_ip, "Authentication Failed");
            char *err_msg = "ERR 001 AUTH FAILED SID:1442\n";
            send(client_socket, err_msg, strlen(err_msg), 0);
        }
    }
    // Safely stop the UDP thread if client drops connection
    if (mon_data != NULL) {
        mon_data->active = 0;
    }
    log_action(client_ip, "Disconnected");
    close(client_socket);
    pthread_exit(NULL);
}

int main() {
    int server_socket;
    struct sockaddr_in server_addr;

    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT); 

    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(server_socket, MAX_CONNECTIONS) < 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }

    printf("Agent started. Listening on port %d...\n", PORT);

    // Auto-create required directories
    struct stat st = {0};
    if (stat("./agentfiles", &st) == -1) {
        mkdir("./agentfiles", 0700);
    }
    if (stat("./agentfiles/IT24102441", &st) == -1) {
        mkdir("./agentfiles/IT24102441", 0700);
    }

    while(1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        int client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_len);
        if (client_socket < 0) continue;

        ClientData *client = malloc(sizeof(ClientData));
        client->socket = client_socket;
        
        // Convert client IP address to string format
        inet_ntop(AF_INET, &(client_addr.sin_addr), client->ip, INET_ADDRSTRLEN);

        pthread_t thread_id;
        if (pthread_create(&thread_id, NULL, client_handler, (void *)client) != 0) {
            free(client);
            close(client_socket);
        } else {
            pthread_detach(thread_id);
        }
    }

    close(server_socket);
    return 0;
}
