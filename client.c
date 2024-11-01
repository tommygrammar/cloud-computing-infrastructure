#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/sendfile.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/stat.h>  // For struct stat and fstat()

#define PORT 6435
#define MAX_DATA_SIZE 65536000  // Adjust buffer size based on expected data

int main() {
    int sockfd;
    struct sockaddr_in serv_addr;
    char *file_name = "sample.c";
    int fd;
    struct stat file_stat;

    // Open the file to upload
    fd = open(file_name, O_RDONLY);
    if (fd == -1) {
        perror("Failed to open file");
        exit(EXIT_FAILURE);
    }

    // Get file size
    if (fstat(fd, &file_stat) < 0) {
        perror("Failed to get file stats");
        close(fd);
        exit(EXIT_FAILURE);
    }

    // Create socket
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Socket creation failed");
        close(fd);  // Close file on failure
        exit(EXIT_FAILURE);
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);

    // Convert IPv4 address from text to binary form
    if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
        perror("Invalid address/ Address not supported");
        close(fd);  // Close file on failure
        close(sockfd);  // Close socket on failure
        exit(EXIT_FAILURE);
    }

    // Connect to server
    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Connection Failed");
        close(fd);  // Close file on failure
        close(sockfd);  // Close socket on failure
        exit(EXIT_FAILURE);
    }

    // Zero-copy file transfer using sendfile()
    off_t offset = 0;
    if (sendfile(sockfd, fd, &offset, file_stat.st_size) == -1) {
        
        perror("Failed to send file");
        close(fd);  // Close file on failure
        close(sockfd);  // Close socket on failure
        exit(EXIT_FAILURE);
    }

    printf("File sent successfully.\n");

    // Receive the execution output from the server
    // Memory mapping for zero-copy data reception
    char *recv_data = mmap(NULL, MAX_DATA_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (recv_data == MAP_FAILED) {
        perror("Memory mapping failed");
        close(fd);  // Close file on failure
        close(sockfd);  // Close socket on failure
        exit(EXIT_FAILURE);
    }

    // Directly read data from the socket into the mapped memory region
    ssize_t bytes_received = recv(sockfd, recv_data, MAX_DATA_SIZE, 0);
    if (bytes_received == -1) {
        perror("Failed to receive data");
        munmap(recv_data, MAX_DATA_SIZE);  // Cleanup memory mapping on failure
        close(fd);  // Close file on failure
        close(sockfd);  // Close socket on failure
        exit(EXIT_FAILURE);
    }

    // Print the received data
    printf("Received data:\n%.*s\n", (int)bytes_received, recv_data);

    // Cleanup resources after successful operation
    munmap(recv_data, MAX_DATA_SIZE);  // Unmap the memory used for receiving data
    close(fd);  // Close the file descriptor
    close(sockfd);  // Close the socket

    printf("Cleaned up and ready for the next operation.\n");

    return 0;
}
