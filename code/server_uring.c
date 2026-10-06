#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <liburing.h>

#define PORT 24000
#define MAX_CLIENTS 2048

struct client
{
    int fd, received, sent;
    char buffer[4096];
};

struct client clients[MAX_CLIENTS];

int main(int argc, char* argv[])
{
    // Read arguments
    if (argc != 5) return 1;

    int active = atoi(argv[1]), idle = atoi(argv[2]);
    int size = atoi(argv[3]), messages = atoi(argv[4]);

    int count = active + idle;
    if (active < 1 || idle < 0 || count > MAX_CLIENTS || size < 1 || size > 4096 || messages < 1) return 1;
    // Count bytes
    uint64_t target = (uint64_t)active * messages * size, total = 0;

    // Create ring
    struct io_uring ring;
    int status = io_uring_queue_init(MAX_CLIENTS, &ring, 0);
    if (status < 0)
    {
        fprintf(stderr, "io_uring_queue_init: %s\n", strerror(-status));
        return 1;
    }

    // Create socket
    int fd = socket(AF_INET, SOCK_STREAM, 0), one = 1;

    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    // Set address
    struct sockaddr_in address = {
        .sin_family = AF_INET,
        .sin_port = htons(PORT),
        .sin_addr.s_addr = htonl(INADDR_LOOPBACK)
    };

    // Bind socket
    if (bind(fd, (struct sockaddr*)&address, sizeof(address)) < 0)
    {
        perror("bind");
        return 1;
    }

    // Start listening
    listen(fd, MAX_CLIENTS);
    printf("Cekam %d konekcija...\n", count);
    fflush(stdout);

    // Accept connections
    int i = 0;
    while (i < count)
    {
        clients[i].fd = accept(fd, NULL, NULL);

        if (clients[i].fd < 0) return 1;

        setsockopt(clients[i].fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
        // Prepare receive
        struct io_uring_sqe* sqe = io_uring_get_sqe(&ring);

        io_uring_prep_recv(sqe, clients[i].fd, clients[i].buffer, size, 0);
        sqe->user_data = i++;
    }

    // Submit operations
    while (io_uring_sq_ready(&ring))
        if (io_uring_submit(&ring) <= 0) return 1;

    // Start timer
    struct timespec start, end, cpu_start, cpu_end;
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &cpu_start);
    clock_gettime(CLOCK_MONOTONIC, &start);
    // Start signal
    send(clients[0].fd, "S", 1, MSG_NOSIGNAL);

    while (total < target)
    {
        // Wait completion
        struct io_uring_cqe* first;
        if (io_uring_wait_cqe(&ring, &first) < 0) return 1;
        
        // Read completions
        struct io_uring_cqe* completions[64];
        unsigned ready = io_uring_peek_batch_cqe(&ring, completions, 64), j = 0;

        while (j < ready)
        {
            // Find client
            struct io_uring_cqe* cqe = completions[j++];
            unsigned index = cqe->user_data;
            struct client* c = &clients[index];
            int n = cqe->res;

            if (n <= 0) return 1;

            // Update counters
            if (!c->received) c->received = n;
            else
            {
                c->sent += n;
                total += n;
                if (c->sent == c->received) c->received = c->sent = 0;
            }

            if (total == target) continue;

            // Prepare operation
            struct io_uring_sqe* sqe = io_uring_get_sqe(&ring);

            if (c->received)
                io_uring_prep_send(sqe, c->fd, c->buffer + c->sent,
                    c->received - c->sent, MSG_NOSIGNAL);
            else
                io_uring_prep_recv(sqe, c->fd, c->buffer, size, 0);
            sqe->user_data = index;
        }

        // Consume completions
        io_uring_cq_advance(&ring, ready);

        // Submit operations
        while (io_uring_sq_ready(&ring))
            if (io_uring_submit(&ring) <= 0) return 1;
    }

    // Stop timer
    clock_gettime(CLOCK_MONOTONIC, &end);
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &cpu_end);

    // Compute metrics
    double seconds = end.tv_sec - start.tv_sec + (end.tv_nsec - start.tv_nsec) / 1e9;
    double cpu = cpu_end.tv_sec - cpu_start.tv_sec + (cpu_end.tv_nsec - cpu_start.tv_nsec) / 1e9;
    double number = (double)active * messages;

    // Save results
    FILE* result = fopen("io_uring.txt", "a");
    if (!result) return 1;
    fprintf(result, "active=%d idle=%d bytes=%d messages=%d seconds=%.6f "
        "messages_s=%.2f MiB_s=%.2f cpu_percent=%.2f cpu_us_message=%.3f\n",
        active, idle, size, messages, seconds, number / seconds,
        total / seconds / (1024 * 1024), cpu / seconds * 100, cpu / number * 1e6);
    fclose(result);

    // Close ring
    io_uring_queue_exit(&ring);

    // Close sockets
    i = 0;
    while (i < count) close(clients[i++].fd);
    close(fd);
    
    return 0;
}
