#pragma once

#include <stdint.h>
#include "hash.h"
#include "list.h"

// Simple buffer to handle memory blocks
typedef struct {
	char*  ptr;
	size_t size;
} buffer_t, *buffer_p;


// A reference counted buffer for streaming data
typedef struct {
	char*    ptr;
	size_t   size;
	size_t   refcount;
	uint32_t flags;
} stream_buffer_t, *stream_buffer_p;

#define STREAM_BUFFER_KEYFRAME  (1 << 0)


// A video stream, one client sends the video, many others receive it
typedef struct {
	uint32_t viewer_count;
	list_p stream_buffers;
	
	// For later
	//buffer_t snapshot_image, stalled_frame;
	//char* snapshot_mime_type;
} stream_t, *stream_p;


// Function pointers making up a client state
typedef struct client_s client_t, *client_p;
typedef struct server_s server_t, *server_p;
typedef int (*client_func_t)(int client_fd, client_p client, server_p server);

typedef struct {
	client_func_t enter, read, write, leave;
} client_state_t, *client_state_p;


// Per client stuff
struct client_s {
	// Called when data can be read on the client socket. If it's NULL we don't
	// even poll for readable data.
	client_func_t read;
	// Called when data can be written to the client socket and the client is not
	// stalled (CLIENT_STALLED flag not set). We need the stalled flag because a
	// connection is pretty much always writable (free space in send buffer). Just
	// polling for a writable connection would result in 100% CPU load because the
	// write function is called all the time only to send nothing.
	client_func_t write;
	// Called before the function pointers are switched to a new state. This
	// function should clean up and free any temporary state the old read and write
	// functions created.
	client_func_t leave_state;
	
	// Flags to remember parts of the client state. Mostly used by the client
	// functions to keep track of what has already been done. See CLIENT_* constants.
	uint32_t flags;
	
	// Buffer that points to stuff to receive or send. Sometimes also used to store
	// partial stuff.
	buffer_t buffer;
	// Next state for the client_write_buffer() helper function
	client_state_t next_state;
	
	// A malloced() string containing the resource the client requested.
	char* resource;
	
	// The stream this client is connected to (either as streamer or as viewer)
	stream_p stream;
	
	// Pointer to the stream buffer node this client currently views
	list_node_p current_stream_buffer;
};

#define CLIENT_STALLED             (1 << 0)
#define CLIENT_HTTP_HEADLINE_READ  (1 << 1)
#define CLIENT_HTTP_HEADERS_READ   (1 << 2)
#define CLIENT_IS_POST_REQUEST     (1 << 3)
#define CLIENT_IS_AUTHORIZED       (1 << 4)



// Server stuff that others need to interact with
struct server_s {
	// List of all connected clients
	hash_p clients;
	// List of all streams
	dict_p streams;
};