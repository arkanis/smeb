#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <unistd.h>

#include <sys/ioctl.h>
#include <linux/sockios.h>

#include "common.h"
#include "client.h"
#include "base64.h"


//
// Prototypes
//

int http_request_read(int client_fd, client_p client, server_p server);
int http_request_handle_headline(client_p client, char* verb, char* resource, char* version);
int http_request_handle_header(client_p client, char* name, char* value);
int http_request_dispatch(int client_fd, client_p client, server_p server);
int http_request_cleanup(int client_fd, client_p client, server_p server);

int client_buffer_write(int client_fd, client_p client, server_p server);
int client_buffer_free(int client_fd, client_p client, server_p server);

int viewer_init_stream_buffer(int client_fd, client_p client, server_p server);
int viewer_write(int client_fd, client_p client, server_p server);

int streamer_receive(int client_fd, client_p client, server_p server);


//
// Public stuff for other source files
//

client_state_t client_start_state = {
	.enter = NULL,
	.read  = http_request_read,
	.write = NULL,
	.leave = http_request_cleanup,
};

/**
 * Changes client state with proper cleanup of the old state and proper initialization
 * of the new state.
 * 
 * If the new state is NULL we just clean up the old state. Useful to destroy a client
 * when it's no longer needed.
 */
void client_change_state(int client_fd, client_p client, server_p server, client_state_p state) {
	if (client->leave_state)
		client->leave_state(client_fd, client, server);
	
	if (state) {
		if (state->enter)
			state->enter(client_fd, client, server);
		
		client->read = state->read;
		client->write = state->write;
		client->leave_state = state->leave;
	}
}


//
// State to parse and dispatch HTTP responses
//

int http_request_read(int client_fd, client_p client, server_p server) {
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
	if (bytes_read < 1) {
		perror("read");
		return 0;
	}
	
	// Reset the client buffer size since we have all its data in our local buffer
	client->buffer.size = 0;
	//printf("client %d, http: %zu bytes: %.*s", client_fd, local_buffer.size, (int)local_buffer.size, local_buffer.ptr);
	
	// Iterate over all lines where we have the terminating line break.
	// We only look for \n and ignore the \r. Not perfectly to spec but
	// more robust.
	char* line_end = NULL;
	while( (line_end = memchr(local_buffer.ptr, '\n', local_buffer.size)) != NULL ) {
		// Regard the \n as part of the line (thats the `+ 1`). Makes things easier down the road.
		size_t line_length = (line_end - local_buffer.ptr) + 1;
		
		//printf("client %d, http: line: %.*s", client_fd, (int)line_length, local_buffer.ptr);
		
		if ( ! (client->flags & CLIENT_HTTP_HEADLINE_READ) ) {
			char verb[line_length], resource[line_length], version[line_length];
			int matched_items = sscanf(local_buffer.ptr, " %s %s %s", verb, resource, version);
			if (matched_items != 3) {
				// error on reading initial HTTP header line, disconnect client for now
				printf("client %d, http: error parsing HTTP headline: %.*s", client_fd, (int)line_length, local_buffer.ptr);
				return 0;
			}
			
			client->flags |= CLIENT_HTTP_HEADLINE_READ;
			http_request_handle_headline(client, verb, resource, version);
		} if (local_buffer.ptr[0] == '\r' || local_buffer.ptr[0] == '\n') {
			// Got a blank line, end of headers
			client->flags |= CLIENT_HTTP_HEADERS_READ;
			
			// Clean up any remaining buffer stuff
			free(client->buffer.ptr);
			client->buffer.ptr = NULL;
			
			return http_request_dispatch(client_fd, client, server);
		} else {
			// normal header line
			char name[line_length], value[line_length];
			int matched_items = sscanf(local_buffer.ptr, " %[^: \t\n\r] : %[^\r\n]", name, value);
			
			// Only process correct header lines, skip incorrect ones
			if (matched_items == 2) {
				http_request_handle_header(client, name, value);
			}
		}
		
		// Continue with the next line
		local_buffer.ptr += line_length;
		local_buffer.size -= line_length;
	}
	
	// If there is an unprocessed rest store it in the client buffer
	if (local_buffer.size > 0) {
		//printf("client %d, http: %zu rest bytes in unfinished line\n", client_fd, local_buffer.size);
		client->buffer.ptr = realloc(client->buffer.ptr, local_buffer.size);
		memcpy(client->buffer.ptr, local_buffer.ptr, local_buffer.size);
		client->buffer.size = local_buffer.size;
	}
	
	return 1;
}

int http_request_handle_headline(client_p client, char* verb, char* resource, char* version) {
	//printf("HTTP verb: %s resource: %s version: %s\n", verb, resource, version);
	
	client->resource = strdup(resource);
	if ( strcmp(verb, "POST") == 0 )
		client->flags |= CLIENT_IS_POST_REQUEST;
	
	return 1;
}

int http_request_handle_header(client_p client, char* name, char* value) {
	//printf("HTTP header, %s: %s\n", name, value);
	/*
	if ( strcmp(name, "Authorization") ) {
		int b64_start = 0, b64_end = 0;
		sscanf(value, " Basic %n%*s%n", &b64_start, &b64_end);
		if (b64_start > 0 && b64_end > b64_start) {
			size_t b64_length = b64_end - b64_start;
			char decoded[b64_length];
			ssize_t decoded_length = base64_decode(value + b64_start, b64_length, decoded, b64_length);
			
			client->flags |= CLIENT_IS_AUTHORIZED;
		}
		
		
	}
	*/
	return 1;
}

int http_request_dispatch(int client_fd, client_p client, server_p server) {
	client->stream = dict_get_ptr(server->streams, client->resource);
	
	if (client->flags & CLIENT_IS_POST_REQUEST) {
		if (!client->stream) {
			client->stream = dict_put_ptr(server->streams, client->resource);
			*client->stream = (stream_t){
				.viewer_count = 0,
				.stream_buffers = list_of(stream_buffer_t)
			};
			printf("new stream at %s\n", client->resource);
		} else {
			printf("continuing on stream %s\n", client->resource);
		}
		
		// TODO: http_request_read() might store an initial part of the request
		// payload in it's buffer. To fix that streamer_receive() needs to reuse
		// the client buffer instead of freeing it.
		client_state_t state = (client_state_t){
			.enter = NULL,
			.read  = streamer_receive,
			.write = NULL,
			.leave = NULL
		};
		client_change_state(client_fd, client, server, &state);
	} else if (client->stream) {
		printf("viewing stream %s\n", client->resource);
		
		client->buffer.ptr = ""
			"HTTP/1.1 200 OK\r\n"
			"Server: smeb v1.0.0\r\n"
			"Transfer-Encoding: chunked\r\n"
			"Connection: close\r\n"
			"Content-Type: text/plain\r\n"
			"\r\n";
		client->buffer.size = strlen(client->buffer.ptr);
		
		client_state_t state = (client_state_t){
			.enter = NULL,
			.read  = NULL,
			.write = client_buffer_write,
			.leave = NULL  // we write a static buffer, so don't free it
		};
		client->next_state = (client_state_t){
			.enter = viewer_init_stream_buffer,
			.read  = NULL,
			.write = viewer_write,
			.leave = NULL
		};
		client_change_state(client_fd, client, server, &state);
	} else {
		client->buffer.ptr = ""
			"HTTP/1.0 404 Not Found\r\n"
			"Server: smeb v1.0.0\r\n"
			"Content-Type: text/plain\r\n"
			"\r\n"
			"Found nothing to serve to you. Sorry about that.\r\n";
		client->buffer.size = strlen(client->buffer.ptr);
		
		client_state_t state = (client_state_t){
			.enter = NULL,
			.read  = NULL,
			.write = client_buffer_write,
			.leave = NULL  // we write a static buffer, so don't free it
		};
		client_change_state(client_fd, client, server, &state);
	}
	
	return 1;
}

int http_request_cleanup(int client_fd, client_p client, server_p server) {
	// Free stuff may be malloced by http_read_headers(). Otherwise they're already NULL.
	free(client->resource);
	client->resource = NULL;
	
	return 1;
}


//
// Generic buffer writing state
//

/**
 * Small helper client write function. Writes the contents of the client buffer
 * and continues with the `next_write_func` of the client or disconnects if it's NULL.
 */
int client_buffer_write(int client_fd, client_p client, server_p server) {
	ssize_t bytes_written = 0;
	while (client->buffer.size > 0) {
		bytes_written = write(client_fd, client->buffer.ptr, client->buffer.size);
		//printf("buffer: written %zd bytes\n", bytes_written);
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
		if (client->next_state.enter || client->next_state.read || client->next_state.write || client->next_state.leave) {
			client_change_state(client_fd, client, server, &client->next_state);
			return client->write(client_fd, client, server);
		} else {
			return 0;
		}
	}
	
	// Still some buffer data left to write for the next time
	return 1;
}

/**
 * Comanion helper for client_buffer_write(). Use this as leave function if client_buffer_write()
 * should write an dynamically allocated buffer.
 */
int client_buffer_free(int client_fd, client_p client, server_p server) {
	free(client->buffer.ptr);
	client->buffer.ptr = NULL;
	client->buffer.size = 0;
	return 1;
}


//
// Viewer state
//

int viewer_init_stream_buffer(int client_fd, client_p client, server_p server) {
	client->current_stream_buffer = client->stream->stream_buffers->last;
	if (client->current_stream_buffer) {
		stream_buffer_p stream_buffer = list_value_ptr(client->current_stream_buffer);
		client->buffer.ptr = stream_buffer->ptr;
		client->buffer.size = stream_buffer->size;
		printf("client %d next buffer on init\n", client_fd);
	} else {
		client->flags |= CLIENT_STALLED;
		printf("client %d stalled on init\n", client_fd);
	}
	
	return 1;
}

int viewer_write(int client_fd, client_p client, server_p server) {
	while(true) {
		// Write this buffer as far as possible
		while(client->buffer.size > 0) {
			ssize_t bytes_written = write(client_fd, client->buffer.ptr, client->buffer.size);
			if (bytes_written == -1) {
				if (errno == EAGAIN)
					return 1;
				else
					return perror("write"), 0;
			}
			
			client->buffer.ptr += bytes_written;
			client->buffer.size -= bytes_written;
		}
		
		// We finished writing this buffer (otherwise we would've returned on an EAGAIN).
		// Wire up the next buffer.
		client->current_stream_buffer = client->current_stream_buffer->next;
		if (client->current_stream_buffer) {
			stream_buffer_p stream_buffer = list_value_ptr(client->current_stream_buffer);
			client->buffer.ptr = stream_buffer->ptr;
			client->buffer.size = stream_buffer->size;
			printf("client %d next buffer\n", client_fd);
		} else {
			client->flags |= CLIENT_STALLED;
			printf("client %d stalled\n", client_fd);
			break;
		}
	}
	
	return 1;
	
	/*
	// Client is new or is the first client to view the stream (in which case the stream buffer
	// list is empty until the next buffer is received).
	if (!client->current_stream_buffer)
		client->current_stream_buffer = client->stream->stream_buffers->first;
	
	if (!client->current_stream_buffer)
		return 1;
	
	while (client->buffer.size > 0 || client->current_stream_buffer->next) {
		// Write this buffer as far as possible
		while(client->buffer.size > 0) {
			ssize_t bytes_written = write(client_fd, client->buffer.ptr, client->buffer.size);
			if (bytes_written == -1) {
				if (errno == EAGAIN)
					break;
				else
					return perror("write"), 0;
			}
			
			client->buffer.ptr += bytes_written;
			client->buffer.size -= bytes_written;
		}
		
		// Wire up the next buffer if we finished this one
		if (client->buffer.size == 0) {
			list_node_p finished_buffer_node = client->current_stream_buffer;
			stream_buffer_p finished_buffer = list_value_ptr(finished_buffer_node);
			
			client->current_stream_buffer = client->current_stream_buffer->next;
			stream_buffer_p next_buffer = list_value_ptr(client->current_stream_buffer);
			client->buffer.ptr = next_buffer->ptr;
			client->buffer.size = next_buffer->size;
			
			finished_buffer->refcount--;
			if (finished_buffer->refcount == 0) {
				printf("freeing stream buffer\n");
				list_remove(client->stream->stream_buffers, finished_buffer_node);
			}
		}
	}
	
	return 1;
	*/
}


//
// Streamer state
//

int streamer_receive(int client_fd, client_p client, server_p server) {
	int bytes_in_recv_buffer = 0;
	if ( ioctl(client_fd, SIOCINQ, &bytes_in_recv_buffer) == -1 )
		perror("ioctl(SIOCINQ)"), exit(1);
	
	stream_buffer_p stream_buffer = list_append_ptr(client->stream->stream_buffers);
	stream_buffer->refcount = client->stream->viewer_count;
	stream_buffer->flags = 0;
	
	stream_buffer->ptr = malloc(200);
	FILE* f = fmemopen(stream_buffer->ptr, 200, "w");
	fprintf(f, "%x\r\n% 8d bytes of data\n\r\n", 8 + 15, bytes_in_recv_buffer);
	stream_buffer->size = ftell(f);
	fclose(f);
	
	void* discard_buffer = alloca(bytes_in_recv_buffer);
	ssize_t bytes_read = read(client_fd, discard_buffer, bytes_in_recv_buffer);
	
	printf("received %zu stream bytes, discarded %zd bytes\n", stream_buffer->size, bytes_read);
	
	
	
	/*
	// Calculate size for HTTP chunked encoding encapsulation
	int data_size_bits = sizeof(bytes_in_recv_buffer) * 8 - __builtin_clrsb(bytes_in_recv_buffer);
	// The "+ 3" makes sure the integer devision rounds up, so 1 bit results in 1 byte instead of 0.25 = 0 bytes.
	int required_hex_digits = (data_size_bits + 3) / 4;
	size_t len_of_crlf = 2;
	size_t http_encapsulated_size = required_hex_digits + len_of_crlf + bytes_in_recv_buffer + len_of_crlf;
	
	stream_buffer->ptr = malloc(http_encapsulated_size);
	snprintf(stream_buffer->ptr, http_encapsulated_size, "%zx\r\n", http_encapsulated_size);
	ssize_t bytes_read = read(client_fd, stream_buffer->ptr + required_hex_digits + len_of_crlf, bytes_in_recv_buffer);
	if (bytes_read < 1) {
		list_remove_last(client->stream->stream_buffers);
		
		if (bytes_read == -1)
			perror("read");
		
		return 0;
	}
	
	stream_buffer->ptr[http_encapsulated_size - 2] = '\r';
	stream_buffer->ptr[http_encapsulated_size - 1] = '\n';
	stream_buffer->size = http_encapsulated_size;
	printf("received %zd stream bytes (%zu bytes encapsulated)\n", bytes_read, http_encapsulated_size);
	*/
	
	// Update all clients of this stream that already ran out of data
	for(hash_elem_t e = hash_start(server->clients); e != NULL; e = hash_next(server->clients, e)) {
		client_p iteration_client = hash_value_ptr(e);
		if (iteration_client->stream == client->stream && iteration_client->flags & CLIENT_STALLED) {
			iteration_client->current_stream_buffer = client->stream->stream_buffers->last;
			iteration_client->buffer.ptr = stream_buffer->ptr;
			iteration_client->buffer.size = stream_buffer->size;
			iteration_client->flags &= ~CLIENT_STALLED;
			printf("streamer: unstalled client %d\n", (int)hash_key(e));
		}
	}
	
	return 1;
}