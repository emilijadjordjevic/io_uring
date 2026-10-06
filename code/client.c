#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <poll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#define PORT 24000
#define MAX_CLIENTS 2048

struct client
{
    int fd, sent, received, completed;
    struct timespec message_start;
};

struct client clients[MAX_CLIENTS];
struct pollfd poll_fds[MAX_CLIENTS];

int main(int argc, char* argv[])
{
    // Read arguments
    if (argc != 5 && argc != 6) return 1;

    int active = atoi(argv[1]), idle = atoi(argv[2]);
    int size = atoi(argv[3]), messages = atoi(argv[4]);
    int count = active + idle;
    if (active < 1 || idle < 0 || count > MAX_CLIENTS ||
        size < 1 || size > 4096 || messages < 1) return 1;
    // Set address
    struct sockaddr_in address = {
        .sin_family = AF_INET,
        .sin_port = htons(PORT),
        .sin_addr.s_addr = htonl(INADDR_LOOPBACK)
    };
    // Connect sockets
    int i = 0, one = 1;
    while (i < count)
    {
        clients[i].fd = socket(AF_INET, SOCK_STREAM, 0);
        if (connect(clients[i].fd, (struct sockaddr*)&address, sizeof(address)) < 0)
        {
            perror("connect");
            return 1;
        }
        setsockopt(clients[i].fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
        // Track active
        if (i < active)
            poll_fds[i] = (struct pollfd){
                .fd = clients[i].fd, .events = POLLIN | POLLOUT
            };
        ++i;
    }
    // Prepare message
    char message[4096], reply[4096], start_signal;
    memset(message, 'A', size);
    // Wait start
    if (recv(clients[0].fd, &start_signal, 1, 0) != 1) return 1;
    // Start timer
    struct timespec start, end, cpu_start, cpu_end;
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &cpu_start);
    clock_gettime(CLOCK_MONOTONIC, &start);
    i = 0;
    while (i < active) clock_gettime(CLOCK_MONOTONIC, &clients[i++].message_start);
    // Count messages
    uint64_t target = (uint64_t)active * messages, completed = 0;
    double latency = 0;

    while (completed < target)
    {
        // Wait sockets
        if (poll(poll_fds, active, 10000) <= 0) return 1;
        i = 0;
        while (i < active)
        {
            // Find connection
            struct client* c = &clients[i];
            struct pollfd* p = &poll_fds[i++];
            if (!p->revents) continue;
            // Send message
            if (p->revents & POLLOUT)
            {
                int n = send(c->fd, message + c->sent, size - c->sent,
                    MSG_DONTWAIT | MSG_NOSIGNAL);
                if (n > 0) c->sent += n;
                else if (n == 0 || errno != EAGAIN) return 1;
            }
            // Receive reply
            if (p->revents & (POLLIN | POLLHUP | POLLERR))
            {
                int n = recv(c->fd, reply, size - c->received, MSG_DONTWAIT);
                if (n > 0)
                {
                    // Check reply
                    if (memcmp(reply, message + c->received, n)) return 1;
                    c->received += n;
                }
                else if (n == 0 || errno != EAGAIN) return 1;
            }
            // Measure response
            if (c->received == size)
            {
                struct timespec now;
                clock_gettime(CLOCK_MONOTONIC, &now);
                latency += now.tv_sec - c->message_start.tv_sec
                    + (now.tv_nsec - c->message_start.tv_nsec) / 1e9;
                ++c->completed;
                ++completed;
                // Finish connection
                if (c->completed == messages)
                {
                    p->fd = -1;
                    continue;
                }
                // Next message
                c->sent = c->received = 0;
                clock_gettime(CLOCK_MONOTONIC, &c->message_start);
            }
            // Update events
            p->events = POLLIN | (c->sent < size ? POLLOUT : 0);
        }
    }

    // Stop timer
    clock_gettime(CLOCK_MONOTONIC, &end);
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &cpu_end);
    // Compute metrics
    double seconds = end.tv_sec - start.tv_sec
        + (end.tv_nsec - start.tv_nsec) / 1e9;
    double cpu = cpu_end.tv_sec - cpu_start.tv_sec
        + (cpu_end.tv_nsec - cpu_start.tv_nsec) / 1e9;
    // Save results
    const char* file_name = argc == 6 ? argv[5] : "client.txt";
    FILE* result = fopen(file_name, "a");
    if (!result) return 1;
    fprintf(result, "active=%d idle=%d bytes=%d messages=%d seconds=%.6f "
        "messages_s=%.2f average_response_us=%.2f cpu_percent=%.2f\n",
        active, idle, size, messages, seconds, target / seconds,
        latency / target * 1e6, cpu / seconds * 100);
    fclose(result);
    printf("Rezultat: %s\n", file_name);
    // Close sockets
    i = 0;
    while (i < count) close(clients[i++].fd);
    return 0;
}
