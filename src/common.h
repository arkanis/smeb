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

#define STREAM_BUFFER_STATIC    (1 << 0)
#define STREAM_BUFFER_KEYFRAME  (1 << 1)


// A video stream, one client sends the video, many others receive it
typedef struct {
	uint32_t viewer_count;
	list_p stream_buffers;
	
	// For later
	//buffer_t snapshot_image, stalled_frame;
	//char* snapshot_mime_type;
} stream_t, *stream_p;


// Per client stuff
typedef struct {
	void* state;
	
	// Flags to remember parts of the client state. Mostly used by the client
	// functions to keep track of what has already been done. See CLIENT_* constants.
	uint32_t flags;
	
	// Buffer that points to stuff to receive or send. Sometimes also used to store
	// partial stuff.
	buffer_t buffer;
	// A pointer to be freed when the buffer has been processed, e.g. a pointer to
	// the originally malloced block that was used as buffer.
	void* buffer_to_free;
	
	// A malloced() string containing the resource the client requested.
	char* resource;
	
	// The stream this client is connected to (either as streamer or as viewer)
	stream_p stream;
	
	// Pointer to the stream buffer node this client currently views
	list_node_p current_stream_buffer;
} client_t, *client_p;

#define CLIENT_POLL_FOR_READ       (1 << 0)
#define CLIENT_POLL_FOR_WRITE      (1 << 1)

#define CLIENT_IS_POST_REQUEST     (1 << 2)
#define CLIENT_IS_AUTHORIZED       (1 << 3)

#define CLIENT_STALLED             (1 << 4)


// Server stuff that others need to interact with
typedef struct {
	// List of all connected clients
	hash_p clients;
	// List of all streams
	dict_p streams;
} server_t, *server_p;