/*

Useful ffmpeg commands to stream sources:

ffmpeg -re -i [path] -quality realtime -chunked_post 0 http://localhost:1234/test.webm
ffmpeg -re -fflags genpts -i [path] -c copy -chunked_post 0 http://localhost:1234/test.mkv

The "-fflags genpts" parameter fixes incorrectly ordered timestamps. Makes problematic videos
work without transcoding.

*/

// for accept4()
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <poll.h>

#include <signal.h>
#include <sys/signalfd.h>

#include "common.h"
#include "client.h"


int main(int argc, char** argv) {
	if (argc != 2) {
		fprintf(stderr, "usage: %s port\n", argv[0]);
		return 1;
	}
	
	// Setup SIGINT and SIGTERM to terminate our poll loop. For that we read them via a signal fd.
	// To prevent the signals from interrupting our process we need to block them first.
	sigset_t signal_mask;
	sigemptyset(&signal_mask);
	sigaddset(&signal_mask, SIGINT);
	sigaddset(&signal_mask, SIGTERM);
	
	if ( sigprocmask(SIG_BLOCK, &signal_mask, NULL) == -1 )
		perror("sigprocmask"), exit(1);
	
	int signals = signalfd(-1, &signal_mask, SFD_NONBLOCK);
	if (signals == -1)
		perror("signalfd"), exit(1);
	
	
	client_handlers_init();
	
	
	// Setup HTTP server socket.
	// Use SO_REUSEADDR in case we have to restart the server with clients still connected.
	uint16_t port = atoi(argv[1]);
	struct sockaddr_in http_bind_addr = { AF_INET, htons(port), { INADDR_ANY }, {0} };
	
	int http_server_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
	
	int value = 1;
	if ( setsockopt(http_server_fd, SOL_SOCKET, SO_REUSEADDR, &value, sizeof(value)) == -1 )
		perror("setsockopt"), exit(1);
	if ( bind(http_server_fd, &http_bind_addr, sizeof(http_bind_addr)) == -1 )
		perror("bind"), exit(1);
	
	if ( listen(http_server_fd, 3) == -1 )
		perror("listen"), exit(1);
	
	
	// Setup stuff for the poll loop
	server_t server = {0};
	server.clients = hash_of(client_t);
	server.streams = dict_of(stream_t);
	
	// Small helper used multiple times in the poll loop
	void disconnect_client(int client_fd, client_p client, hash_elem_t e) {
		shutdown(client_fd, SHUT_RDWR);
		close(client_fd);
		
		client_handler(client_fd, client, &server, CLIENT_CON_CLEANUP);
		hash_remove_elem(server.clients, e);
	}
	
	// Do the poll loop
	printf("server: willing and able\n");
	while (true) {
		size_t pollfds_length = 2 + server.clients->length;
		struct pollfd pollfds[pollfds_length];
		pollfds[0] = (struct pollfd){ signals, POLLIN, 0 };
		pollfds[1] = (struct pollfd){ http_server_fd,  POLLIN, 0 };
		
		size_t i = 2;
		for(hash_elem_t e = hash_start(server.clients); e != NULL; e = hash_next(server.clients, e), i++) {
			int client_fd = hash_key(e);
			client_p client = hash_value_ptr(e);
			
			short events = 0;
			if (client->flags & CLIENT_POLL_FOR_READ)
				events |= POLLIN;
			if (client->flags & CLIENT_POLL_FOR_WRITE)
				events |= POLLOUT;
			
			pollfds[i] = (struct pollfd){ client_fd, events, 0 };
		}
		
		if ( poll(pollfds, sizeof(pollfds) / sizeof(pollfds[0]), -1) == -1 )
			perror("poll"), exit(1);
		
		// Check for incomming signals to shutdown the server
		if (pollfds[0].revents & POLLIN) {
			// Consume signal (so SIGTERM will not kill us after unblocking signals)
			struct signalfd_siginfo infos;
			if ( read(signals, &infos, sizeof(infos)) == -1 )
				perror("read from signalfd"), exit(1);
			
			// Break poll loop
			break;
		}
		
		// Check for incomming data from clients. For this to work the clients hash must not
		// be changed between the poll() call and here. Because of this we handle new connections
		// at the end.
		i = 2;
		for(hash_elem_t e = hash_start(server.clients); e != NULL; e = hash_next(server.clients, e), i++) {
			int client_fd = pollfds[i].fd;
			client_p client = hash_value_ptr(e);
			
			if ( pollfds[i].revents & POLLHUP ) {
				printf("server: client %d disconnected via POLLHUP\n", client_fd);
				disconnect_client(client_fd, client, e);
				continue;
			}
			
			// In case of an error we disconnect the client. Not perfect but this way we
			// at least will notice errors.
			if ( pollfds[i].revents & POLLERR ) {
				int error = 0;
				socklen_t error_len = sizeof(error);
				if ( getsockopt(client_fd, SOL_SOCKET, SO_ERROR, &error, &error_len) == -1 )
					perror("getsockopt"), exit(1);
				printf("server: client %d disconnected by error: %s\n", client_fd, strerror(error));
				disconnect_client(client_fd, client, e);
				continue;
			}
			
			if ( pollfds[i].revents & POLLIN ) {
				if ( client_handler(client_fd, client, &server, CLIENT_CON_READABLE) == -1 ) {
					printf("server: client %d disconnected via client handler\n", client_fd);
					disconnect_client(client_fd, client, e);
				}
			}
			
			if ( pollfds[i].revents & POLLOUT ) {
				if ( client_handler(client_fd, client, &server, CLIENT_CON_WRITABLE) == -1 ) {
					printf("server: client %d disconnected via client handler\n", client_fd);
					disconnect_client(client_fd, client, e);
				}
			}
		}
		
		// Check for new connections
		if (pollfds[1].revents & POLLIN) {
			int client_fd = accept4(http_server_fd, NULL, NULL, SOCK_NONBLOCK);
			if (client_fd == -1)
				perror("accept4"), exit(1);
			
			printf("server: client %d connected\n", client_fd);
			client_p client = hash_put_ptr(server.clients, client_fd);
			memset(client, 0, sizeof(client_t));
			client_handler(client_fd, client, &server, 0);
		}
	}
	
	
	// Clean up time
	printf("server: cleaning up\n");
	close(http_server_fd);
	close(signals);
	if ( sigprocmask(SIG_UNBLOCK, &signal_mask, NULL) == -1 )
		perror("sigprocmask"), exit(1);
	
	return 0;
}