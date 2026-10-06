.PHONY: all clean

all: server_epoll server_uring client

server_epoll: server_epoll.c
	gcc -O2 -Wall -Wextra server_epoll.c -o server_epoll

server_uring: server_uring.c
	gcc -O2 -Wall -Wextra server_uring.c -luring -o server_uring

client: client.c
	gcc -O2 -Wall -Wextra client.c -o client

clean:
	rm -f server_epoll server_uring client
