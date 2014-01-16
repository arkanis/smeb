// for accept4()
#define _GNU_SOURCE
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/ioctl.h>
#include <linux/sockios.h>
#include <arpa/inet.h>
#include <poll.h>
#include <signal.h>
#include <sys/signalfd.h>

#include "hash.h"
#include "base64.h"


typedef struct {
	char*  ptr;
	size_t size;
} buffer_t, *buffer_p;


typedef struct client_s client_t, *client_p;
typedef int (*client_func_t)(int client_fd, client_p client);

struct client_s {
	// If these function pointers are set we poll() for a readable or writeable
	// connection. If data can be read or written the function pointers are called.
	client_func_t read_func;
	client_func_t write_func;
	
	// Client state variables, used by the functions to keep track of what has
	// already been done.
	uint8_t initial_http_line_read;
	uint8_t http_headers_read;
	uint8_t is_post_request;
	uint8_t is_authorized;
	
	// Scratch space to receive, send or store partial stuff
	buffer_t buffer;
};

// Static buffer containing the "if all else fails" HTTP response
buffer_t emergency_message;


int client_write_buffer_and_disconnect(int client_fd, client_p client) {
	ssize_t bytes_written = 0;
	while (client->buffer.size > 0) {
		bytes_written = write(client_fd, client->buffer.ptr, client->buffer.size);
		if (bytes_written > 0) {
			client->buffer.ptr += bytes_written;
			client->buffer.size -= bytes_written;
		} else {
			break;
		}
	}
	
	if (bytes_written == -1 && errno != EAGAIN) {
		perror("write");
		return 0;
	}
	
	if (client->buffer.size == 0) {
		return 0;
	}
	
	return 1;
}

int client_read_incomming_stream(int client_fd, client_p client) {
	client->buffer = client->buffer;
	
	int bytes_in_recv_buffer = 0;
	if ( ioctl(client_fd, SIOCINQ, &bytes_in_recv_buffer) == -1 )
		perror("ioctl(SIOCINQ)"), exit(1);
	
	void* local_buffer_ptr = alloca(bytes_in_recv_buffer);
	ssize_t bytes_read = read(client_fd, local_buffer_ptr, bytes_in_recv_buffer);
	printf("%zd stream bytes\n", bytes_read);
	
	return bytes_read;
}

int http_handle_initial_header_line(client_p client, char* verb, char* resource) {
	printf("HTTP verb: %s resource: %s\n", verb, resource);
	
	if ( strcmp(verb, "POST") == 0 )
		client->is_post_request = true;
	
	return 1;
}

int http_handle_header(client_p client, char* name, char* value) {
	printf("HTTP header, %s: %s\n", name, value);
	
	if ( strcmp(name, "Authorization") ) {
		int b64_start = 0, b64_end = 0;
		sscanf(value, " Basic %n%*s%n", &b64_start, &b64_end);
		if (b64_start > 0 && b64_end > b64_start) {
			size_t b64_length = b64_end - b64_start;
			char decoded[b64_length];
			/*ssize_t decoded_length =*/ base64_decode(value + b64_start, b64_length, decoded, b64_length);
			
			client->is_authorized = true;
		}
		
		
	}
	
	return 1;
}

int http_handle_request(client_p client) {
	if (client->is_post_request) {
		client->read_func = client_read_incomming_stream;
		client->write_func = NULL;
	} else {
		client->read_func = NULL;
		client->write_func = client_write_buffer_and_disconnect;
		client->buffer = emergency_message;
	}
	
	return 1;
}


int client_read_http_headers(int client_fd, client_p client) {
	int bytes_in_recv_buffer = 0;
	if ( ioctl(client_fd, SIOCINQ, &bytes_in_recv_buffer) == -1 )
		perror("ioctl(SIOCINQ)"), exit(1);
	
	// Create a local buffer large enough to hold the old buffered data
	// and all the stuff in the receive buffer. Then fill it with the old
	// data and read the new stuff.
	buffer_t local_buffer;
	local_buffer.size = client->buffer.size + bytes_in_recv_buffer;
	local_buffer.ptr = alloca(local_buffer.size);
	memcpy(local_buffer.ptr, client->buffer.ptr, client->buffer.size);
	
	ssize_t bytes_read = read(client_fd, local_buffer.ptr + client->buffer.size, bytes_in_recv_buffer);
	if (bytes_read < 1)
		return bytes_read;
	
	client->buffer.size = 0;
	printf("client %d, %zu bytes: %.*s", client_fd, local_buffer.size, (int)local_buffer.size, local_buffer.ptr);
	
	// Iterate over all lines where we have the terminating line break.
	// We only look for \n and ignore the \r. Not perfectly to spec but
	// more robust.
	char* line_end = NULL;
	while( (line_end = memchr(local_buffer.ptr, '\n', local_buffer.size)) != NULL ) {
		// Regard the \n as part of the line (thats the `+ 1`). Makes things easier down the road.
		size_t line_length = (line_end - local_buffer.ptr) + 1;
		
		printf("line: %.*s", (int)line_length, local_buffer.ptr);
		
		if ( ! client->initial_http_line_read ) {
			char verb[line_length], path[line_length];
			int matched_items = sscanf(local_buffer.ptr, " %s %s", verb, path);
			if (matched_items != 2) {
				// error on reading initial HTTP header line, disconnect client for now
				return 0;
			}
			
			client->initial_http_line_read = true;
			http_handle_initial_header_line(client, verb, path);
		} if (local_buffer.ptr[0] == '\r' || local_buffer.ptr[0] == '\n') {
			// Got a blank line, end of headers
			client->http_headers_read = true;
			
			// Clean up any remaining buffer stuff
			free(client->buffer.ptr);
			client->buffer.ptr = NULL;
			
			return http_handle_request(client);
		} else {
			// normal header line
			char name[line_length], value[line_length];
			int matched_items = sscanf(local_buffer.ptr, " %[^: \t\n\r] : %[^\r\n]", name, value);
			
			// Only process correct header lines, skip incorrect ones
			if (matched_items == 2) {
				http_handle_header(client, name, value);
			}
		}
		
		
		// Continue with the next line
		local_buffer.ptr += line_length;
		local_buffer.size -= line_length;
	}
	
	// If there is an unprocessed rest store it in the client buffer
	if (local_buffer.size > 0) {
		printf("rest: %zu bytes\n", local_buffer.size);
		client->buffer.ptr = realloc(client->buffer.ptr, local_buffer.size);
		memcpy(client->buffer.ptr, local_buffer.ptr, local_buffer.size);
		client->buffer.size = local_buffer.size;
	}
	
	return bytes_read;
}


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
	
	
	// Setup static stuff
	emergency_message.ptr = ""
		"HTTP/1.0 500 OK\r\n"
		"Content-Type: text/plain\r\n"
		"\r\n"
		"Internal server error. Sorry about that.\r\n";
	emergency_message.size = strlen(emergency_message.ptr);
	
	// Setup HTTP server socket
	struct sockaddr_in http_bind_addr = { AF_INET, htons(1234), { INADDR_ANY }, {0} };
	
	int http_server_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
	
	if ( bind(http_server_fd, &http_bind_addr, sizeof(http_bind_addr)) == -1 )
		perror("bind"), exit(1);
	
	if ( listen(http_server_fd, 3) == -1 )
		perror("listen"), exit(1);
	
	
	// Do the poll loop
	hash_p clients = hash_of(client_t);
	while (true) {
		size_t pollfds_length = 2 + clients->length;
		struct pollfd pollfds[pollfds_length];
		pollfds[0] = (struct pollfd){ signals, POLLIN, 0 };
		pollfds[1] = (struct pollfd){ http_server_fd,  POLLIN, 0 };
		
		size_t i = 2;
		for(hash_elem_t e = hash_start(clients); e != NULL; e = hash_next(clients, e)) {
			int client_fd = hash_key(e);
			client_p client = hash_value_ptr(e);
			
			short events = 0;
			if (client->read_func)
				events |= POLLIN;
			if (client->write_func)
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
		for(hash_elem_t e = hash_start(clients); e != NULL; e = hash_next(clients, e)) {
			int client_fd = hash_key(e);
			client_p client = hash_value_ptr(e);
			
			if ( pollfds[i].revents & POLLIN && client->read_func) {
				ssize_t bytes_read = client->read_func(pollfds[i].fd, client);
				if (bytes_read == 0) {
					printf("client %d disconnected\n", client_fd);
					
					shutdown(pollfds[i].fd, SHUT_RDWR);
					close(pollfds[i].fd);
					
					hash_remove_elem(clients, e);
				} else if (bytes_read == -1) {
					perror("read"), exit(1);
				}
			}
			
			if ( pollfds[i].revents & POLLOUT && client->write_func) {
				int result = client->write_func(pollfds[i].fd, client);
				if (result == 0) {
					shutdown(pollfds[i].fd, SHUT_RDWR);
					close(pollfds[i].fd);
					
					hash_remove_elem(clients, e);
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
			hash_put(clients, client, client_t, c);
			
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