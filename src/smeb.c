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


int main() {
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
	
	
	// Setup HTTP server socket.
	// Use SO_REUSEADDR in case we have to restart the server with clients still connected.
	struct sockaddr_in http_bind_addr = { AF_INET, htons(1234), { INADDR_ANY }, {0} };
	
	int http_server_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
	
	int value = 1;
	if ( setsockopt(http_server_fd, SOL_SOCKET, SO_REUSEADDR, &value, sizeof(value)) == -1 )
		perror("setsockopt"), exit(1);
	if ( bind(http_server_fd, &http_bind_addr, sizeof(http_bind_addr)) == -1 )
		perror("bind"), exit(1);
	
	if ( listen(http_server_fd, 3) == -1 )
		perror("listen"), exit(1);
	
	
	// Do the poll loop
	server_t server = {0};
	server.clients = hash_of(client_t);
	server.streams = dict_of(stream_t);
	while (true) {
		size_t pollfds_length = 2 + server.clients->length;
		struct pollfd pollfds[pollfds_length];
		pollfds[0] = (struct pollfd){ signals, POLLIN, 0 };
		pollfds[1] = (struct pollfd){ http_server_fd,  POLLIN, 0 };
		
		size_t i = 2;
		for(hash_elem_t e = hash_start(server.clients); e != NULL; e = hash_next(server.clients, e)) {
			int client_fd = hash_key(e);
			client_p client = hash_value_ptr(e);
			
			short events = 0;
			if (client->read_func)
				events |= POLLIN;
			if (client->write_func && client->buffer.size > 0)
				events |= POLLOUT;
			
			pollfds[i] = (struct pollfd){ client_fd, events, 0 };
			i++;
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
		for(hash_elem_t e = hash_start(server.clients); e != NULL; e = hash_next(server.clients, e)) {
			int client_fd = hash_key(e);
			client_p client = hash_value_ptr(e);
			
			if ( pollfds[i].revents & POLLHUP) {
				printf("client %d disconnected by POLLHUP\n", client_fd);
				
				shutdown(pollfds[i].fd, SHUT_RDWR);
				close(pollfds[i].fd);
				
				hash_remove_elem(server.clients, e);
			} else {
				if ( pollfds[i].revents & POLLERR) {
					int error_code = 0;
					socklen_t error_len = sizeof(error_code);
					if ( getsockopt(pollfds[i].fd, SOL_SOCKET, SO_ERROR, &error_code, &error_len) == -1 )
						perror("getsockopt"), exit(1);
					errno = error_code;
					fprintf(stderr, "client %d ", client_fd);
					perror("");
				}
				
				if ( pollfds[i].revents & POLLIN && client->read_func) {
					ssize_t bytes_read = client->read_func(pollfds[i].fd, client, &server);
					if (bytes_read == 0) {
						printf("client %d disconnected\n", client_fd);
						
						shutdown(pollfds[i].fd, SHUT_RDWR);
						close(pollfds[i].fd);
						
						hash_remove_elem(server.clients, e);
					} else if (bytes_read == -1) {
						perror("read"), exit(1);
					}
				}
				
				if ( pollfds[i].revents & POLLOUT && client->write_func) {
					int result = client->write_func(pollfds[i].fd, client, &server);
					if (result == 0) {
						shutdown(pollfds[i].fd, SHUT_RDWR);
						close(pollfds[i].fd);
						
						hash_remove_elem(server.clients, e);
					}
				}
			}
			
			i++;
		}
		

		// Check for new connections
		if (pollfds[1].revents & POLLIN) {
			int client = accept4(http_server_fd, NULL, NULL, SOCK_NONBLOCK);
			if (client == -1)
				perror("accept4"), exit(1);
			
			printf("new client %d\n", client);
			client_t c = { 0 };
			c.read_func = client_read_http_headers;
			hash_put(server.clients, client, client_t, c);
			
		}
	}
	
	
	// Clean up time
	printf("cleaning up\n");
	close(http_server_fd);
	close(signals);
	if ( sigprocmask(SIG_UNBLOCK, &signal_mask, NULL) == -1 )
		perror("sigprocmask"), exit(1);
	
	return 0;
}