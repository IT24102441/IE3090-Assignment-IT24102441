#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PORT 9410
#define AGENT_IP "127.0.0.1"

int main() {
    int sock;
    struct sockaddr_in server_addr;
    char buffer[1024];

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Socket creation error");
        return -1;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);

    if (inet_pton(AF_INET, AGENT_IP, &server_addr.sin_addr) <= 0) {
        perror("Invalid IP address");
        return -1;
    }

    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection to Agent failed");
        return -1;
    }
    printf("Connected to Agent at %s:%d\n", AGENT_IP, PORT);

    // 1. Automated Authentication
    char *auth_msg = "AUTH OPS-2441";
    send(sock, auth_msg, strlen(auth_msg), 0);
    
    int bytes_read = recv(sock, buffer, sizeof(buffer) - 1, 0);
    if (bytes_read > 0) {
        buffer[bytes_read] = '\0';
        printf("Agent: %s\n", buffer);
    }

    // 2. Interactive Command Loop
while (1) {
        printf("Controller> ");
        fgets(buffer, sizeof(buffer), stdin);
        buffer[strcspn(buffer, "\n")] = 0; // Strip newline

        if (strlen(buffer) == 0) continue;

        // --- INTERCEPT PUT COMMAND ---
        if (strncmp(buffer, "PUT ", 4) == 0) {
            char filename[256];
            if (sscanf(buffer + 4, "%255s", filename) == 1) {
                FILE *file = fopen(filename, "rb");
                if (file) {
                    fseek(file, 0, SEEK_END);
                    long file_size = ftell(file);
                    fseek(file, 0, SEEK_SET);

                    // Send header
                    char header[512];
                    snprintf(header, sizeof(header), "PUT %s %ld\n", filename, file_size);
                    send(sock, header, strlen(header), 0);

                    // Stream file bytes
                    char file_buf[1024];
                    size_t bytes_read_file;
                    while ((bytes_read_file = fread(file_buf, 1, sizeof(file_buf), file)) > 0) {
                        send(sock, file_buf, bytes_read_file, 0);
                    }
                    fclose(file);

                    // Wait for Agent OK/ERR response
                    bytes_read = recv(sock, buffer, sizeof(buffer) - 1, 0);
                    if (bytes_read > 0) {
                        buffer[bytes_read] = '\0';
                        printf("Agent: %s\n", buffer);
                    }
                } else {
                    printf("Error: Cannot open local file '%s'\n", filename);
                }
            }
            continue; 
        }
        
        // --- INTERCEPT GET COMMAND ---
        else if (strncmp(buffer, "GET ", 4) == 0) {
            char filename[256];
            if (sscanf(buffer + 4, "%255s", filename) == 1) {
                // Send GET request
                char header[512];
                snprintf(header, sizeof(header), "GET %s\n", filename);
                send(sock, header, strlen(header), 0);

                // Read Agent response
                bytes_read = recv(sock, buffer, sizeof(buffer) - 1, 0);
                if (bytes_read > 0) {
                    buffer[bytes_read] = '\0';
                    printf("Agent: %s\n", buffer);

                    // If OK, parse size and read raw bytes
                    if (strncmp(buffer, "OK GET ", 7) == 0) {
                        long file_size;
                        if (sscanf(buffer + 7, "%ld", &file_size) == 1) {
                            FILE *file = fopen(filename, "wb");
                            if (file) {
                                long bytes_remaining = file_size;
                                char file_buf[1024];
                                size_t chunk_size;

                                while (bytes_remaining > 0) {
                                    chunk_size = (bytes_remaining < (long)sizeof(file_buf)) ? bytes_remaining : (long)sizeof(file_buf);
                                    ssize_t received = recv(sock, file_buf, chunk_size, 0);
                                    if (received <= 0) break;
                                    fwrite(file_buf, 1, received, file);
                                    bytes_remaining -= received;
                                }
                                fclose(file);
                                printf("Download complete: %s (%ld bytes)\n", filename, file_size);
                            }
                        }
                    }
                }
            }
            continue;
        }

        // --- STANDARD TEXT COMMANDS (SYSINFO, LISTPROC, ETC) ---
        send(sock, buffer, strlen(buffer), 0);
        bytes_read = recv(sock, buffer, sizeof(buffer) - 1, 0);
        if (bytes_read > 0) {
            buffer[bytes_read] = '\0';
            printf("Agent: %s\n", buffer);
        }

        if (strcmp(buffer, "QUIT") == 0 || strstr(buffer, "OK BYE") != NULL) {
            break;
        }
    }
close(sock);
    return 0;
}
