#include "../include/common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>

#define BUFFER_SIZE 1024

// Connect to server
int connect_to_server(const char *host, int port) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("Socket creation failed");
        return -1;
    }
    
    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    
    if (inet_pton(AF_INET, host, &serv_addr.sin_addr) <= 0) {
        printf("Invalid address/ Address not supported\n");
        close(sockfd);
        return -1;
    }
    
    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        printf("Connection failed to %s:%d\n", host, port);
        close(sockfd);
        return -1;
    }
    
    printf("Connected to server %s:%d\n", host, port);
    return sockfd;
}

// Helper function to send command and wait for response
void send_and_wait(int sockfd, const char *command) {
    char buffer[BUFFER_SIZE];
    
    // Send command
    printf("Sending: %s", command);
    if (send(sockfd, command, strlen(command), 0) < 0) {
        printf("Failed to send command\n");
        return;
    }
    
    // Wait for and read response
    usleep(100000); // Small delay to ensure server processes the command
    
    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(sockfd, &read_fds);
    
    struct timeval timeout;
    timeout.tv_sec = 2;
    timeout.tv_usec = 0;
    
    // Check if there's data to read
    int ready = select(sockfd + 1, &read_fds, NULL, NULL, &timeout);
    if (ready > 0) {
        ssize_t bytes_received = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
        if (bytes_received > 0) {
            buffer[bytes_received] = '\0';
            printf("Server: %s", buffer);
        }
    } else if (ready == 0) {
        printf("No response from server (timeout)\n");
    } else {
        printf("Error waiting for response\n");
    }
}

// Send command and receive response (simple version)
void send_command(int sockfd, const char *command) {
    char buffer[BUFFER_SIZE];
    
    // Send command
    printf("Sending: %s", command);
    send(sockfd, command, strlen(command), 0);
    
    // Receive response
    ssize_t bytes_received = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
    if (bytes_received > 0) {
        buffer[bytes_received] = '\0';
        printf("Server: %s", buffer);
    } else {
        printf("No response from server\n");
    }
}

// Replace the test_scenario_1 function with this:
void test_scenario_1(int sockfd) {
    printf("\n=== Test Scenario 1: Basic User Operations ===\n");
    
    char buffer[BUFFER_SIZE];
    
    // Step 1: Wait for and respond to authentication prompt
    printf("Waiting for authentication prompt...\n");
    ssize_t bytes = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
    if (bytes > 0) {
        buffer[bytes] = '\0';
        printf("Server: %s", buffer);
    }
    
    // Step 2: Send SIGNUP and wait for response
    printf("Sending: SIGNUP user1 password123\n");
    send(sockfd, "SIGNUP user1 password123\n", 25, 0);
    
    bytes = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
    if (bytes > 0) {
        buffer[bytes] = '\0';
        printf("Server: %s", buffer);
    }
    
    // Step 3: Wait for welcome message
    bytes = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
    if (bytes > 0) {
        buffer[bytes] = '\0';
        printf("Server: %s", buffer);
    }
    
    // Step 4: LIST files (should be empty)
    printf("Sending: LIST\n");
    send(sockfd, "LIST\n", 5, 0);
    
    bytes = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
    if (bytes > 0) {
        buffer[bytes] = '\0';
        printf("Server: %s", buffer);
    }
    
    // Step 5: UPLOAD file
    printf("Sending: UPLOAD test1.txt\n");
    send(sockfd, "UPLOAD test1.txt\n", 17, 0);
    
    // Wait for READY prompt
    bytes = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
    if (bytes > 0) {
        buffer[bytes] = '\0';
        printf("Server: %s", buffer);
    }
    
    // Send file content
    printf("Sending file data...\n");
    send(sockfd, "This is test file content for user1!\n", 36, 0);
    
    // Wait for upload completion
    bytes = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
    if (bytes > 0) {
        buffer[bytes] = '\0';
        printf("Server: %s", buffer);
    }
    
    // Step 6: LIST files again (should have test1.txt)
    printf("Sending: LIST\n");
    send(sockfd, "LIST\n", 5, 0);
    
    bytes = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
    if (bytes > 0) {
        buffer[bytes] = '\0';
        printf("Server: %s", buffer);
    }
    
    printf("=== Test Scenario 1 Completed ===\n");
}

// Replace test_scenario_2 with this simpler version:
void test_scenario_2() {
    printf("\n=== Test Scenario 2: Multiple Users ===\n");
    
    // User 1 - Simple connection test only
    printf("Testing User 1 connection...\n");
    int sock1 = connect_to_server("127.0.0.1", 8080);
    if (sock1 >= 0) {
        char buffer[BUFFER_SIZE];
        
        // Wait for auth prompt and send SIGNUP
        recv(sock1, buffer, sizeof(buffer) - 1, 0);
        send(sock1, "SIGNUP user2 pass456\n", 21, 0);
        recv(sock1, buffer, sizeof(buffer) - 1, 0); // SIGNUP response
        recv(sock1, buffer, sizeof(buffer) - 1, 0); // Welcome message
        
        send(sock1, "LIST\n", 5, 0);
        recv(sock1, buffer, sizeof(buffer) - 1, 0);
        
        close(sock1);
        printf("User 1 test completed\n");
    }
    
    // Small delay between users
    sleep(1);
    
    // User 2
    printf("Testing User 2 connection...\n");
    int sock2 = connect_to_server("127.0.0.1", 8080);
    if (sock2 >= 0) {
        char buffer[BUFFER_SIZE];
        
        // Wait for auth prompt and send SIGNUP
        recv(sock2, buffer, sizeof(buffer) - 1, 0);
        send(sock2, "SIGNUP user3 pass789\n", 21, 0);
        recv(sock2, buffer, sizeof(buffer) - 1, 0); // SIGNUP response
        recv(sock2, buffer, sizeof(buffer) - 1, 0); // Welcome message
        
        send(sock2, "LIST\n", 5, 0);
        recv(sock2, buffer, sizeof(buffer) - 1, 0);
        
        close(sock2);
        printf("User 2 test completed\n");
    }
    
    printf("=== Test Scenario 2 Completed ===\n");
}

// Interactive mode
void interactive_mode(int sockfd) {
    printf("\n=== Interactive Mode ===\n");
    printf("Available commands:\n");
    printf("  SIGNUP <username> <password>\n");
    printf("  LOGIN <username> <password>\n");
    printf("  UPLOAD <filename>\n");
    printf("  DOWNLOAD <filename>\n");
    printf("  DELETE <filename>\n");
    printf("  LIST\n");
    printf("  QUIT\n");
    printf("Type 'quit' to exit interactive mode\n\n");
    
    char buffer[BUFFER_SIZE];
    
    while (1) {
        printf("client> ");
        fflush(stdout);
        
        if (fgets(buffer, sizeof(buffer), stdin) == NULL) {
            break;
        }
        
        // Remove newline
        buffer[strcspn(buffer, "\n")] = 0;
        
        if (strcasecmp(buffer, "quit") == 0 || strcasecmp(buffer, "exit") == 0) {
            break;
        }
        
        // Add newline back for server protocol
        strcat(buffer, "\n");
        
        send_command(sockfd, buffer);
    }
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        printf("Usage: %s <server_ip> <port> [mode]\n", argv[0]);
        printf("Example: %s 127.0.0.1 8080\n", argv[0]);
        printf("Test modes: %s 127.0.0.1 8080 test1\n", argv[0]);
        printf("           %s 127.0.0.1 8080 test2\n", argv[0]);
        printf("           %s 127.0.0.1 8080 interactive\n", argv[0]);
        return 1;
    }
    
    const char *server_ip = argv[1];
    int port = atoi(argv[2]);
    const char *mode = (argc >= 4) ? argv[3] : "interactive";
    
    int sockfd = connect_to_server(server_ip, port);
    if (sockfd < 0) {
        return 1;
    }
    
    // Handle different test modes
    if (strcmp(mode, "test1") == 0) {
        test_scenario_1(sockfd);
    } else if (strcmp(mode, "test2") == 0) {
        close(sockfd);
        test_scenario_2();
    } else if (strcmp(mode, "interactive") == 0) {
        interactive_mode(sockfd);
    } else {
        printf("Unknown mode: %s\n", mode);
        printf("Use: test1, test2, or interactive\n");
    }
    
    close(sockfd);
    printf("Disconnected from server\n");
    
    return 0;
}
