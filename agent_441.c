#include <sys/sysinfo.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define PORT 9410
#define MAX_CONNECTIONS 5

// Structure to pass data safely to the new thread
typedef struct {
    int socket;
} ClientData;

// The worker thread function for each connected Controller
void *client_handler(void *arg) {
    ClientData *client = (ClientData *)arg;
    int client_socket = client->socket;
    free(client);

    char buffer[1024];
    
    // 1. Mandatory Authentication Check
    ssize_t bytes_read = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
    if (bytes_read > 0) {
        buffer[bytes_read] = '\0';
        buffer[strcspn(buffer, "\r\n")] = 0;

        if (strcmp(buffer, "AUTH OPS-2441") == 0) {
            char *success_msg = "OK AUTHENTICATED SID:1442\n";
            send(client_socket, success_msg, strlen(success_msg), 0);
            
            // 2. The Main Command Processing Loop
            while ((bytes_read = recv(client_socket, buffer, sizeof(buffer) - 1, 0)) > 0) {
                buffer[bytes_read] = '\0';
                buffer[strcspn(buffer, "\r\n")] = 0;
                
                if (strcmp(buffer, "SYSINFO") == 0) {
                    struct sysinfo info;
                    sysinfo(&info);
                    long uptime = info.uptime;
                    long mem_used = (info.totalram - info.freeram) / (1024 * 1024);
                    float cpu_load = info.loads[0] / 65536.0; 
                    
                    char response[256];
                    snprintf(response, sizeof(response), "OK SYSINFO %.2f %ld %ld SID:1442\n", cpu_load, mem_used, uptime);
                    send(client_socket, response, strlen(response), 0);
                    
                } else if (strcmp(buffer, "LISTPROC") == 0) {
                    FILE *fp = popen("ps -eo comm --no-headers | head -n 5 | tr '\\n' ','", "r");
                    char procs[512] = {0};
                    if (fp) {
                        fgets(procs, sizeof(procs)-1, fp);
                        pclose(fp);
                        
                        char response[1024];
                        snprintf(response, sizeof(response), "OK PROCS %s SID:1442\n", procs);
                        send(client_socket, response, strlen(response), 0);
                    }
                    
                } else if (strncmp(buffer, "EXEC ", 5) == 0) {
                    char *cmd = buffer + 5; 
                    char *sys_cmd = NULL;
                    
                    // Strict whitelist enforcement[cite: 2, 3]
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
                            snprintf(response, sizeof(response), "OK EXEC_RESULT %s SID:1442\n", output); //[cite: 3]
                            send(client_socket, response, strlen(response), 0);
                        }
                    } else {
                        char *err_msg = "ERR 002 COMMAND NOT ALLOWED SID:1442\n"; //[cite: 3]
                        send(client_socket, err_msg, strlen(err_msg), 0);
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
            char *err_msg = "ERR 001 AUTH FAILED SID:1442\n"; //[cite: 3]
            send(client_socket, err_msg, strlen(err_msg), 0);
        }
    }
    
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
    server_addr.sin_port = htons(PORT); // Port 9410[cite: 3, 4]

    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(server_socket, MAX_CONNECTIONS) < 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }

    printf("Agent started. Listening on port %d...\n", PORT);

    // 2. The Accept Loop
    while(1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        int client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_len);
        if (client_socket < 0) {
            perror("Accept failed");
            continue;
        }

        // Dynamically allocate memory for the client struct to avoid race conditions
        ClientData *client = malloc(sizeof(ClientData));
        client->socket = client_socket;

        // Spawn a new thread to handle the connection
        pthread_t thread_id;
        if (pthread_create(&thread_id, NULL, client_handler, (void *)client) != 0) {
            perror("Thread creation failed");
            free(client);
            close(client_socket);
        } else {
            // Detach the thread so the OS reclaims its memory when it exits
            pthread_detach(thread_id);
        }
    }

    close(server_socket);
    return 0;
}
