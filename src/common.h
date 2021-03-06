#pragma once

#include <stdint.h>
#include <stdio.h>
#include "timer.h"
#include "hash.h"
#include "list.h"
#include "logger.h"

// Simple buffer to handle memory blocks
typedef struct {
	char*  ptr;
	size_t size, filled;
} buffer_t, *buffer_p;


// A reference counted buffer for streaming data
typedef struct {
	char*    ptr;
	size_t   size;
	size_t   refcount;
	uint32_t flags;
	usec_t   timecode;
} stream_buffer_t, *stream_buffer_p;

// Don't free the stream buffers ptr when the refcount reaches 0. Used for
// static string data and data that belongs to the stream (e.g. the video
// header).
#define STREAM_BUFFER_DONT_FREE_CONTENT   (1 << 0)
// The stream buffers list node was created by the client itself and doesn't
// belong to the streams buffer list. So they must not be removed from that list.
#define STREAM_BUFFER_CLIENT_PRIVATE      (1 << 1)


// A video stream, one client sends the video, many others receive it
typedef struct {
	uint32_t viewer_count;
	list_p stream_buffers;
	buffer_t header;
	
	FILE* intro_stream;
	buffer_t intro_buffer;
	
	uint64_t prev_sources_offset;
	uint64_t last_observed_timecode;
	
	usec_t last_disconnect_at;
	dict_p params;
	char* name;
	
	usec_t latest_cluster_received_at;
	
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
	
	// A malloced() string containing the HTTP method and resource the client requested.
	char* method;
	char* resource;
	
	// The stream this client is connected to (either as streamer or as viewer)
	stream_p stream;
	
	// Pointer to the stream buffer node this client currently views
	list_node_p current_stream_buffer;
	
	// If this pointer is not NULL we have to write a pointer to the next received
	// cluster buffer of this stream there. It's necessary to wire up new clients
	// to the "main line" of cluster buffers. Otherwise they may miss one or more
	// clusters while the initial buffers are send to them.
	// This field is NULLed when a cluster is received (and the pointer was written)
	// or the client is stalled (in that case the buffer node we wanted to write the
	// pointer to has been freed and we would overwrite something totally unrelated).
	list_node_p* insert_next_received_cluster_buffer;
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
	
	int stream_delete_timeout_sec;
} server_t, *server_p;