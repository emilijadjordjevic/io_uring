#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#define PORT 24000
#define MAX_CLIENTS 2048
#define MAX_EVENTS 64

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
    long long target = (long long)active * messages * size, total = 0;

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
    // Create epoll
    int epoll_fd = epoll_create1(0);

    printf("Cekam %d konekcija...\n", count);
    fflush(stdout);

    // Accept connections
    int i = 0;
    while (i < count)
    {
        clients[i].fd = accept(fd, NULL, NULL);

        if (clients[i].fd < 0) return 1;

        setsockopt(clients[i].fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

        // Register socket
        struct epoll_event event = {.events = EPOLLIN, .data.u32 = i};

        epoll_ctl(epoll_fd, EPOLL_CTL_ADD, clients[i].fd, &event);
        ++i;
    }

    // Start timer
    struct timespec start, end, cpu_start, cpu_end;
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &cpu_start);
    clock_gettime(CLOCK_MONOTONIC, &start);

    // Start signal
    send(clients[0].fd, "S", 1, MSG_NOSIGNAL);

    struct epoll_event events[MAX_EVENTS];
    while (total < target)
    {
        // Wait events
        int ready = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);

        if (ready < 0) return 1;

        i = 0;
        while (i < ready)
        {
            // Find client
            unsigned index = events[i++].data.u32;
            struct client* c = &clients[index];

            // Receive data
            if (!c->received)
            {
                int n = recv(c->fd, c->buffer, size, MSG_DONTWAIT);

                if (n < 0 && errno == EAGAIN) continue;
                if (n <= 0) return 1;

                c->received = n;
            }

            // Send reply
            int n = send(c->fd, c->buffer + c->sent, c->received - c->sent, MSG_DONTWAIT | MSG_NOSIGNAL);

            if (n > 0)
            {
                c->sent += n;
                total += n;
            }

            else if (n == 0 || errno != EAGAIN) return 1;

            if (c->sent == c->received) c->received = c->sent = 0;

            // Update events
            struct epoll_event event = {
                .events = c->received ? EPOLLOUT : EPOLLIN,
                .data.u32 = index
            };

            epoll_ctl(epoll_fd, EPOLL_CTL_MOD, c->fd, &event);
        }
    }

    // Stop timer
    clock_gettime(CLOCK_MONOTONIC, &end);
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &cpu_end);

    // Compute metrics
    double seconds = end.tv_sec - start.tv_sec + (end.tv_nsec - start.tv_nsec) / 1e9;
    double cpu = cpu_end.tv_sec - cpu_start.tv_sec + (cpu_end.tv_nsec - cpu_start.tv_nsec) / 1e9;
    double number = (double)active * messages;

    // Save results
    FILE* result = fopen("epoll.txt", "a");
    if (!result) return 1;

    fprintf(result, "active=%d idle=%d bytes=%d messages=%d seconds=%.6f "
        "messages_s=%.2f MiB_s=%.2f cpu_percent=%.2f cpu_us_message=%.3f\n",
        active, idle, size, messages, seconds, number / seconds,
        total / seconds / (1024 * 1024), cpu / seconds * 100, cpu / number * 1e6);
    fclose(result);

    // Close sockets
    i = 0;
    while (i < count) close(clients[i++].fd);

    close(epoll_fd);
    close(fd);

    return 0;
}
