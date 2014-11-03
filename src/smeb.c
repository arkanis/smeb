/*

Useful ffmpeg commands to stream sources:

ffmpeg -re -i [path] -quality realtime -chunked_post 0 http://localhost:1234/test.webm
ffmpeg -re -fflags genpts -i [path] -c copy -chunked_post 0 http://localhost:1234/test.mkv

The "-fflags genpts" parameter fixes incorrectly ordered timestamps. Makes problematic videos
work without transcoding.

ffmpeg -re -i hd-video.mkv -quality realtime -minrate 1M -maxrate 1M -b:v 1M -threads 3 -chunked_post 0 http://localhost:1234/test.webm


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
#include <sys/timerfd.h>

#include "common.h"
#include "client.h"


int main(int argc, char** argv) {
	if (argc != 3) {
		fprintf(stderr, "usage: %s bind-addr port\n", argv[0]);
		return 1;
	}
	
	logger_setup(LOG_DEBUG);
	
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
	
	
	// Create a timer that wakes us every minute
	int timer = timerfd_create(CLOCK_REALTIME, TFD_NONBLOCK);
	if (timer == -1)
		perror("timerfd_create"), exit(1);
	
	struct itimerspec timer_setup = (struct itimerspec){
		.it_value    = { 10, 0 },
		.it_interval = { 10, 0 }
	};
	timerfd_settime(timer, 0, &timer_setup, NULL);
	
	
	// Setup HTTP server socket.
	// Use SO_REUSEADDR in case we have to restart the server with clients still connected.
	uint16_t port = atoi(argv[2]);
	struct sockaddr_in http_bind_addr = { AF_INET, htons(port), { INADDR_ANY }, {0} };
	inet_pton(AF_INET, argv[1], &http_bind_addr.sin_addr);
	
	int http_server_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
	
	int value = 1;
	if ( setsockopt(http_server_fd, SOL_SOCKET, SO_REUSEADDR, &value, sizeof(value)) == -1 )
		perror("setsockopt"), exit(1);
	if ( bind(http_server_fd, &http_bind_addr, sizeof(http_bind_addr)) == -1 )
		perror("bind"), exit(1);
	
	if ( listen(http_server_fd, 3) == -1 )
		perror("listen"), exit(1);
	
	char ip_addr_text[INET6_ADDRSTRLEN];
	inet_ntop(http_bind_addr.sin_family, &http_bind_addr.sin_addr, ip_addr_text, sizeof(ip_addr_text));
	info("[server] listening on %s:%d", ip_addr_text, ntohs(http_bind_addr.sin_port));
	
	
	// Setup stuff for the poll loop
	server_t server;
	memset(&server, 0, sizeof(server));
	server.clients = hash_of(client_t);
	server.streams = dict_of(stream_p);
	server.stream_delete_timeout_sec = 15 * 60;
	
	// Small helper used multiple times in the poll loop
	void disconnect_client(int client_fd, client_p client, hash_elem_t e) {
		shutdown(client_fd, SHUT_RDWR);
		close(client_fd);
		
		client_handler(client_fd, client, &server, CLIENT_CON_CLEANUP);
		hash_remove_elem(server.clients, e);
	}
	
	// Do the poll loop
	while (true) {
		size_t non_client_fds = 3;
		size_t pollfds_length = non_client_fds + server.clients->length;
		struct pollfd pollfds[pollfds_length];
		pollfds[0] = (struct pollfd){ signals, POLLIN, 0 };
		pollfds[1] = (struct pollfd){ http_server_fd,  POLLIN, 0 };
		pollfds[2] = (struct pollfd){ timer,  POLLIN, 0 };
		
		size_t i = non_client_fds;
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
				warn("[server] failed to consume signal from signalfd: %s", strerror(errno));
			
			// Break poll loop
			break;
		}
		
		// Check for incomming data from clients. For this to work the clients hash must not
		// be changed between the poll() call and here. Because of this we handle new connections
		// at the end.
		i = non_client_fds;
		for(hash_elem_t e = hash_start(server.clients); e != NULL; e = hash_next(server.clients, e), i++) {
			int client_fd = pollfds[i].fd;
			client_p client = hash_value_ptr(e);
			
			if ( pollfds[i].revents & POLLHUP ) {
				info("[client %d]: disconnected via POLLHUP", client_fd);
				disconnect_client(client_fd, client, e);
				continue;
			}
			
			// In case of an error we disconnect the client. Not perfect but this way we
			// at least will notice errors.
			if ( pollfds[i].revents & POLLERR ) {
				int error = 0;
				socklen_t error_len = sizeof(error);
				if ( getsockopt(client_fd, SOL_SOCKET, SO_ERROR, &error, &error_len) == 0 )
					warn("[client %d] disconnected because of socket error: %s", client_fd, strerror(error));
				else
					warn("[client %d] disconnected because of unknown socket error (failed to get error code with getsockopt(): %s)", client_fd, strerror(errno));
				
				disconnect_client(client_fd, client, e);
				continue;
			}
			
			if ( pollfds[i].revents & POLLIN ) {
				if ( client_handler(client_fd, client, &server, CLIENT_CON_READABLE) == -1 ) {
					info("[client %d] disconnected via client handler", client_fd);
					disconnect_client(client_fd, client, e);
				}
			}
			
			if ( pollfds[i].revents & POLLOUT ) {
				if ( client_handler(client_fd, client, &server, CLIENT_CON_WRITABLE) == -1 ) {
					info("[client %d] disconnected via client handler", client_fd);
					disconnect_client(client_fd, client, e);
				}
			}
		}
		
		if (pollfds[2].revents & POLLIN) {
			uint64_t expirations;
			ssize_t bytes_read = read(timer, &expirations, sizeof(expirations));
			
			// If the read failed we just try again on the next poll iteration
			if (bytes_read == sizeof(expirations)) {
				
				// Delete expired streams
				for(dict_elem_t e = dict_start(server.streams); e != NULL; e = dict_next(server.streams, e)) {
					stream_p stream = dict_value(e, stream_p);
					if (stream->last_disconnect_at != 0 && stream->last_disconnect_at + server.stream_delete_timeout_sec * 1000000LL < time_now()) {
						info("[stream %s] deleting stream, no new data arrived within timeout of %d seconds", dict_key(e), server.stream_delete_timeout_sec);
						
						// First disconnect all clients watching that stream. This also unrefs any remaining stream buffers.
						for(hash_elem_t ce = hash_start(server.clients); ce != NULL; ce = hash_next(server.clients, ce)) {
							int client_fd = hash_key(ce);
							client_p client = hash_value_ptr(ce);
							if (client->stream == stream) {
								info("[client %d] disconnected because stream was deleted", client_fd);
								disconnect_client(client_fd, client, ce);
							}
						}
						
						// Free stream stuff
						list_destroy(stream->stream_buffers);
						free(stream->header.ptr);
						fclose(stream->intro_stream);
						free(stream->intro_buffer.ptr);
						
						dict_remove_elem(server.streams, e);
						free(stream);
					}
				}
			}
		}
		
		// Check for new connections
		if (pollfds[1].revents & POLLIN) {
			int client_fd = accept4(http_server_fd, NULL, NULL, SOCK_NONBLOCK);
			if (client_fd == -1)
				perror("accept4"), exit(1);
			
			info("[client %d] connected", client_fd);
			client_p client = hash_put_ptr(server.clients, client_fd);
			memset(client, 0, sizeof(client_t));
			client_handler(client_fd, client, &server, 0);
		}
	}
	
	
	// Clean up time
	info("[server] cleaning up");
	close(http_server_fd);
	close(timer);
	close(signals);
	if ( sigprocmask(SIG_UNBLOCK, &signal_mask, NULL) == -1 )
		perror("sigprocmask"), exit(1);
	
	return 0;
}