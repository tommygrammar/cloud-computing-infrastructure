#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/sendfile.h>  // For sendfile()
#include <sys/syscall.h>   // For memfd_create()

#define PORT 6435
#define FILE_NAME "generated_sample.c"
#define MAX_DATA_SIZE 65536000

char *dto_data = NULL;

void compile_and_run(const char* file_name) {


    // Compile the file
    if (fork() == 0) {
        execlp("gcc", "gcc", file_name, "-o", "hello", NULL);
        perror("Failed to compile");
        exit(EXIT_FAILURE);
    }
    wait(NULL);  // Wait for the compilation to finish

    // Run the compiled executable
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        perror("Failed to create pipe");
        exit(EXIT_FAILURE);
    }

    if (fork() == 0) {
        close(pipefd[0]);  // Close read end
        dup2(pipefd[1], STDOUT_FILENO);  // Redirect stdout to pipe
        execlp("./hello", "./hello", NULL);
        perror("Failed to run executable");
        exit(EXIT_FAILURE);
    }

    // Read the output from the child process
    close(pipefd[1]);  // Close write end
    char output[1024] = {0};
    read(pipefd[0], output, sizeof(output));
    close(pipefd[0]);

    // Save the output to dto_data
    dto_data = strdup(output);  // Make a copy of the output

    printf("Program output: %s\n", dto_data);



    
}

int main() {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    int fd;
    struct stat file_stat;
    void* file_memory;

    // Create socket file descriptor
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Socket failed");
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    // Bind the socket
    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    // Listen for incoming connections
    if (listen(server_fd, 3) < 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }

    printf("Waiting for connections...\n");

    while (1) {
        // Accept incoming connection
        if ((new_socket = accept(server_fd, (struct sockaddr*)&address, (socklen_t*)&addrlen)) < 0) {
            perror("Accept failed");
            exit(EXIT_FAILURE);
        }

        // Open the file for writing and truncate (to ensure it's fresh)
        if ((fd = open(FILE_NAME, O_CREAT | O_RDWR | O_TRUNC, 0666)) < 0) {
            perror("Failed to open file");
            close(new_socket);
            continue;
        }

        // Dynamically determine the size based on the client's data
        size_t file_size = 1024;  // Assuming 1024 bytes for now
        ftruncate(fd, file_size);  // Preallocate file size

        // Map the file to memory for efficient file write
        file_memory = mmap(NULL, file_size, PROT_WRITE, MAP_SHARED, fd, 0);
        if (file_memory == MAP_FAILED) {
            perror("Memory mapping failed");
            close(fd);
            close(new_socket);
            continue;
        }

        // Receive file data from the client
        ssize_t bytes_received = read(new_socket, file_memory, file_size);
        if (bytes_received < 0) {
            perror("Failed to receive data");
            munmap(file_memory, file_size);
            close(fd);
            close(new_socket);
            continue;
        }

        // Print the received file contents
        printf("Received file contents:\n%.*s\n", (int)bytes_received, (char*)file_memory);

        // Ensure the data is written to the disk
        msync((char*)file_memory, (int)bytes_received, MS_SYNC);
        fsync(fd);

        // Compile and run the file
        compile_and_run(FILE_NAME);

        // Use in-memory file for zero-copy send
        const char* data = dto_data;  // Access the DTO data
        size_t data_len = strlen(data);

        if (data_len > 0) {
            // Create in-memory file using memfd_create for zero-copy transfer
            int mem_fd = syscall(SYS_memfd_create, "shm_memfile", 0);
            write(mem_fd, data, data_len);  // Write shared memory data
            lseek(mem_fd, 0, SEEK_SET);     // Reset file offset to the beginning

            // Send the data using zero-copy with sendfile
            if (sendfile(new_socket, mem_fd, NULL, data_len) == -1) {
                perror("Failed to send data using zero-copy");
            } else {
                printf("Successfully sent using zero copy\n");
            }

            // Close memory file descriptor
            close(mem_fd);
            free(dto_data);
            dto_data = NULL;
        } else {
            printf("No data to send\n");
        }

        // Clean up memory and file descriptors
        munmap(file_memory, file_size);
        close(fd);
        close(new_socket);
    }

    return 0;
}
