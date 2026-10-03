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
    free(client); // Free the memory allocated in main to prevent leaks

    char buffer[1024];
    
    // 1. Mandatory Authentication Check
    ssize_t bytes_read = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
    if (bytes_read > 0) {
        buffer[bytes_read] = '\0';
        
        // Strip trailing newline characters for clean string comparison
        buffer[strcspn(buffer, "\r\n")] = 0;

        // Check against your personalized token[cite: 2, 4]
        if (strcmp(buffer, "AUTH OPS-2441") == 0) {
            char *success_msg = "OK AUTHENTICATED SID:1442\n"; // Personalized SID
            send(client_socket, success_msg, strlen(success_msg), 0);
            
            // TODO: The main command processing loop (SYSINFO, EXEC, PUT, GET) will go here
            
        } else {
            char *err_msg = "ERR 001 AUTH FAILED SID:1442\n";
            send(client_socket, err_msg, strlen(err_msg), 0);
        }
    }
    
    // Close the connection cleanly if authentication fails or client disconnects
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
