#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <unistd.h>
#include <fcntl.h>

#include <sys/ioctl.h>
#include <linux/sockios.h>

#include <libavformat/avformat.h>
#include <libavutil/error.h>

#include "client.h"
#include "ebml_writer.h"
#include "base64.h"


static ssize_t local_buffer_required_size          (buffer_p local_buffer, client_p client, int client_fd);
static ssize_t local_buffer_recv                   (buffer_p local_buffer, client_p client, int client_fd);
static void    local_buffer_backup_to_client_buffer(buffer_p local_buffer, client_p client);
static ssize_t first_line_length                   (buffer_p local_buffer);

static void  http_request_handle_headline(client_p client, char* verb, char* resource, char* version);
static void  http_request_handle_header  (client_p client, char* name, char* value);
static void* http_request_dispatch       (client_p client, server_p server,
	void* enter_send_buffer_and_disconnect,
	void* enter_receive_stream,
	void* enter_send_stream
);

static void streamer_prepare_video_header(client_p client);


int client_handlers_init() {
	av_register_all();
	return 0;
}


int client_handler(int client_fd, client_p client, server_p server, int flags) {
	
	// A small macro used by many states. Allocates a local buffer using alloca() (thats
	// the reason it's a macro) and copies the client and reveive buffer into it.
#	define FILL_LOCAL_BUFFER(buffer) \
		buffer.size = local_buffer_required_size(&buffer, client, client_fd);  \
		buffer.ptr  = alloca(buffer.size);                                     \
		if ( local_buffer_recv(&buffer, client, client_fd) == -1 )             \
			goto free_client_buffer_and_disconnect;
	
	// Temporary variables used by several states
	buffer_t local_buffer = { 0 };
	ssize_t  line_length  = 0;
	
	// Jump to the current state
	if (client->state != NULL)
		goto *client->state;
	else
		goto enter_http_request_headline;
	
	
	
	// State parses first line of the HTTP request.
	// Client state used:
	//   client->buffer (to store incomplete rest of a line until next packet is received)
	
	enter_http_request_headline:
		client->flags |= CLIENT_POLL_FOR_READ;
		client->state = &&http_request_headline;
		goto return_to_server_to_poll_for_io;
		
	http_request_headline:
		if (flags & CLIENT_CON_CLEANUP)
			goto free_client_buffer_and_disconnect;
		FILL_LOCAL_BUFFER(local_buffer);
		
		if ( (line_length = first_line_length(&local_buffer)) != -1 ) {
			char verb[line_length], resource[line_length], version[line_length];
			
			int matched_items = sscanf(local_buffer.ptr, " %s %s %s", verb, resource, version);
			if (matched_items != 3) {
				// error on reading initial HTTP header line, disconnect client for now
				printf("client %d, http: error parsing HTTP headline: %.*s", client_fd, (int)line_length, local_buffer.ptr);
				goto free_client_buffer_and_disconnect;
			}
			
			http_request_handle_headline(client, verb, resource, version);
			goto leave_http_request_headline;
		}
		
		// No whole line in buffer, maybe next time we receive the missing data
		local_buffer_backup_to_client_buffer(&local_buffer, client);
		goto return_to_server_to_poll_for_io;
		
	leave_http_request_headline:
		client->flags |= CLIENT_POLL_FOR_READ;
		client->state = &&http_request_headers;
		goto http_request_headers_buffer_filled;
	
	
	
	// State parses all HTTP request headers.
	// Client state used:
	//   client->buffer (to store incomplete rest of a line until next packet is received)
	
	http_request_headers:
		if (flags & CLIENT_CON_CLEANUP)
			goto free_client_buffer_and_disconnect;
		FILL_LOCAL_BUFFER(local_buffer);
		
	http_request_headers_buffer_filled:
		while ( (line_length = first_line_length(&local_buffer)) != -1 ) {
			// See if we got an empty line that signals the end of the HTTP headers (last char is always '\n')
			if ( line_length == 1 || (line_length == 2 && local_buffer.ptr[0] == '\r') ) {
				// All headers read, consume the empty line and leave this state
				local_buffer.ptr  += line_length;
				local_buffer.size -= line_length;
				goto leave_http_request_headers;
			}
			
			// normal header line
			char name[line_length], value[line_length];
			int matched_items = sscanf(local_buffer.ptr, " %[^: \t\n\r] : %[^\r\n]", name, value);
			
			// Only process correct header lines, skip incorrect ones
			if (matched_items == 2) {
				http_request_handle_header(client, name, value);
			}
			
			local_buffer.ptr  += line_length;
			local_buffer.size -= line_length;
		}
		
		// No more whole lines in buffer left, store the rest for next time
		local_buffer_backup_to_client_buffer(&local_buffer, client);
		goto return_to_server_to_poll_for_io;
		
	leave_http_request_headers:
		// First off free the client buffer since it might have been allocated
		// to store incomplete lines.
		free(client->buffer.ptr);
		client->buffer.ptr  = NULL;
		client->buffer.size = 0;
		
		// Decide what to do with the request
		goto *http_request_dispatch(client, server,
			&&enter_send_buffer_and_disconnect,
			&&enter_receive_stream,
			&&enter_send_stream
		);
	
	
	
	// State to receive a video stream and store it in new stream buffers.
	// Client state used:
	//   client->stream (not freed by the client, contains the stream this client transmits to)
	
	enter_receive_stream:
		if (!client->stream) {
			client->stream = dict_put_ptr(server->streams, client->resource);
			memset(client->stream, 0, sizeof(stream_t));
			client->stream->stream_buffers = list_of(stream_buffer_t);
			
			AVFormatContext* demuxer = avformat_alloc_context();
			demuxer->flags |= AVFMT_FLAG_NONBLOCK;
			
			if ( fcntl(client_fd, F_SETFL, fcntl(client_fd, F_GETFL, NULL) | O_NONBLOCK) == -1 )
				perror("fcntl"), exit(1);

			
			char strbuf[512];
			snprintf(strbuf, sizeof(strbuf), "pipe:%d", client_fd);
			AVInputFormat* webm_fmt = av_find_input_format("webm");
			int error = avformat_open_input(&demuxer, strbuf, webm_fmt, NULL);
			if (error < 0)
				fprintf(stderr, "avformat_open_input(): %s\n", av_make_error_string(strbuf, sizeof(strbuf), error));
			
			client->stream->demuxer = demuxer;
			
			printf("new stream at %s\n", client->resource);
		} else {
			printf("continuing on stream %s\n", client->resource);
		}
		
		client->state = &&receive_stream;
		client->flags |= CLIENT_POLL_FOR_READ;
		
		// Process any data left in the local buffer, otherwise let the server poll for more
		if (local_buffer.size > 0)
			goto receive_stream_buffer_filled;
		else
			goto return_to_server_to_poll_for_io;
		
	receive_stream:
		if (flags & CLIENT_CON_CLEANUP)
			goto leave_receive_stream;
		
		/* av_read_frame reads fd directly as pipe, no need to read data into buffer
		// Read incomming data into local buffer
		int bytes_in_recv_buffer = 0;
		if ( ioctl(client_fd, SIOCINQ, &bytes_in_recv_buffer) == -1 )
			perror("ioctl(SIOCINQ)");
		
		local_buffer.size = bytes_in_recv_buffer;
		local_buffer.ptr  = alloca(local_buffer.size);
		ssize_t bytes_read = read(client_fd, local_buffer.ptr, bytes_in_recv_buffer);
		if (bytes_read < 1) {
			if (bytes_read == -1)
				perror("read");
			goto leave_receive_stream;
		}
		*/
		
	receive_stream_buffer_filled: {
		AVPacket packet;
		int ret = 0;
		
		while ( (ret = av_read_frame(client->stream->demuxer, &packet)) == 0 ) {
			// We got at least the first frame so the demuxer read the video header and knows
			// what streams are in the video. All the info we need to build our own initial
			// stream header.
			if (!client->stream->header.ptr)
				streamer_prepare_video_header(client);
			
			//printf("frame: stream %d, pts: %lu, dts: %lu, duration: %d, buf: %p\n", packet.stream_index, packet.pts, packet.dts, packet.duration, packet.buf);
			//if (packet.flags & AV_PKT_FLAG_KEY && packet.stream_index == 0)
			//	printf("keyframe: stream %d, pts: %lu, dts: %lu, duration: %d, buf: %p\n", packet.stream_index, packet.pts, packet.dts, packet.duration, packet.buf);
			
			av_free_packet(&packet);
		}
		
		if (ret == AVERROR(EAGAIN)) {
			printf("end of data for now\n");
			goto return_to_server_to_poll_for_io;
		} else if (ret != 0) {
			char strbuf[512];
			fprintf(stderr, "av_read_frame(): %s\n", av_make_error_string(strbuf, sizeof(strbuf), ret));
			goto return_to_server_to_poll_for_io;
		}
		
		
		// Create a new stream buffer with new data
		stream_buffer_p stream_buffer = list_append_ptr(client->stream->stream_buffers);
		memset(stream_buffer, 0, sizeof(stream_buffer_t));
		
		stream_buffer->ptr = malloc(200);
		FILE* f = fmemopen(stream_buffer->ptr, 200, "w");
		fprintf(f, "%x\r\n% 8zd bytes of data\n\r\n", 8 + 15, local_buffer.size);
		stream_buffer->size = ftell(f);
		fclose(f);
		
		printf("receive stream: received %zu bytes, put %zu into new stream buffer\n", local_buffer.size, stream_buffer->size);
		
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
				iteration_client->flags |= CLIENT_POLL_FOR_WRITE;
				iteration_client->flags &= ~CLIENT_STALLED;
				printf("streamer: unstalled client %d\n", (int)hash_key(e));
			}
		}
		
		goto return_to_server_to_poll_for_io;
	}
	
	leave_receive_stream:
		avformat_close_input(client->stream->demuxer);
		goto disconnect;
	
	
	
	// State that sends stream buffers to the viewers of a stream.
	// Client state used:
	//   client->stream (not freed by the client, stream the client watches)
	
	enter_send_stream: {
		client->state = &&send_stream;
		client->flags |= CLIENT_POLL_FOR_WRITE;
		client->flags &= ~CLIENT_POLL_FOR_READ;
		
		
		// Create a new stream buffer node for the initial HTTP response and
		// video header and wire them up as the first buffers the client sends,
		// followed by the latest buffer of the stream.
		list_node_p http_header_node = list_new_node(client->stream->stream_buffers);
		list_node_p video_header_node = list_new_node(client->stream->stream_buffers);
		
		http_header_node->prev = NULL;
		http_header_node->next = video_header_node;
		
		stream_buffer_p http_header_buffer = list_value_ptr(http_header_node);
		http_header_buffer->ptr = ""
			"HTTP/1.1 200 OK\r\n"
			"Server: smeb v1.0.0\r\n"
			"Transfer-Encoding: chunked\r\n"
			"Connection: close\r\n"
			"Content-Type: text/plain\r\n"
			"\r\n";
		http_header_buffer->size = strlen(http_header_buffer->ptr);
		http_header_buffer->refcount = 0;
		http_header_buffer->flags = STREAM_BUFFER_STATIC;
		
		
		video_header_node->prev = NULL;
		video_header_node->next = client->stream->stream_buffers->last;
		
		stream_buffer_p video_header_buffer = list_value_ptr(video_header_node);
		video_header_buffer->ptr = client->stream->header.ptr;
		video_header_buffer->size = client->stream->header.size;
		video_header_buffer->refcount = 0;
		video_header_buffer->flags = STREAM_BUFFER_STATIC;
		
		
		client->current_stream_buffer = http_header_node;
		client->buffer.ptr  = http_header_buffer->ptr;
		client->buffer.size = http_header_buffer->size;
	}
		
	send_stream:
		if (flags & CLIENT_CON_CLEANUP)
			goto disconnect;
		
		while(true) {
			// Write this buffer as far as possible
			while(client->buffer.size > 0) {
				ssize_t bytes_written = write(client_fd, client->buffer.ptr, client->buffer.size);
				if (bytes_written == -1) {
					if (errno == EAGAIN) {
						goto return_to_server_to_poll_for_io;
					} else {
						perror("write");
						goto disconnect;
					}
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
				client->flags &= ~CLIENT_POLL_FOR_WRITE;
				printf("client %d stalled\n", client_fd);
				goto return_to_server_to_poll_for_io;
			}
		}
		
	//leave_send_stream:
		// Nothing todo yet, in future: when stream is deleted we have to drop the client connection
	
	
	
	// State to delete a stream.
	// TODO: poll all clients to see if their connections are stuck. That is they disconnected while
	// the stream was stalled and therefore we didn't poll for read or write and were not notified of
	// them closing their connection.
	
	
	
	// State writes the client buffer to the connection and disconnects afterwards. If the buffer
	// was dynamically allocated store the original malloc pointer in client->buffer_to_free. It
	// will be freed once the buffer was send.
	// 
	// Client state used:
	//   client->buffer (store the buffer you want to send in there)
	//   client->buffer_to_free (this pointer will be passed to free() when the buffer was send)
	
	enter_send_buffer_and_disconnect:
		client->state = &&send_buffer_and_disconnect;
		client->flags |= CLIENT_POLL_FOR_WRITE;
		client->flags &= ~CLIENT_POLL_FOR_READ;
		goto return_to_server_to_poll_for_io;
		
	send_buffer_and_disconnect:
		while (client->buffer.size > 0) {
			ssize_t bytes_written = write(client_fd, client->buffer.ptr, client->buffer.size);
			if (bytes_written >= 0) {
				//printf("buffer: written %zd bytes\n", bytes_written);
				client->buffer.ptr  += bytes_written;
				client->buffer.size -= bytes_written;
			} else {
				if (errno == EAGAIN) {
					break;
				} else {
					perror("write");
					goto free_client_buffer_and_disconnect;
				}
			}
		}
		
		if (client->buffer.size == 0) {
			free(client->buffer_to_free);
			client->buffer_to_free = NULL;
			goto disconnect;
		}
		
		// Still some buffer data left to write for the next time
		goto return_to_server_to_poll_for_io;
	
	
	// Exit states, either to clean up a client or to return control to the server
	// so it can poll for a readable or writable connection.
	
	free_client_buffer_and_disconnect:
		free(client->buffer.ptr);
		client->buffer.ptr  = NULL;
		client->buffer.size = 0;
		
	disconnect:
		return -1;
	
	return_to_server_to_poll_for_io:
		return 0;
	
	
	// Cleanup macros used in the state machine
#	undef FILL_LOCAL_BUFFER
}


//
// Helper functions for different states.
// These are local functions because some of them need access to the labels of the state machine.
//

static void http_request_handle_headline(client_p client, char* verb, char* resource, char* version) {
	//printf("HTTP verb: %s resource: %s version: %s\n", verb, resource, version);
	
	client->resource = strdup(resource);
	if ( strcmp(verb, "POST") == 0 )
		client->flags |= CLIENT_IS_POST_REQUEST;
}

static void http_request_handle_header(client_p client, char* name, char* value) {
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
}

static void* http_request_dispatch(client_p client, server_p server,
		void* enter_send_buffer_and_disconnect,
		void* enter_receive_stream,
		void* enter_send_stream
) {
	printf("dispatching %s...\n", client->resource);
	client->stream = dict_get_ptr(server->streams, client->resource);
	
	if (client->flags & CLIENT_IS_POST_REQUEST) {
		return enter_receive_stream;
	} else if (client->stream) {
		return enter_send_stream;
	}
	
	client->buffer.ptr = ""
		"HTTP/1.0 404 Not Found\r\n"
		"Server: smeb v1.0.0\r\n"
		"Content-Type: text/plain\r\n"
		"\r\n"
		"Found nothing to serve to you. Sorry about that.\r\n";
	client->buffer.size = strlen(client->buffer.ptr);
	client->buffer_to_free = NULL;
	return enter_send_buffer_and_disconnect;
}


static void streamer_prepare_video_header(client_p client) {
	AVFormatContext* demuxer = client->stream->demuxer;
	
	FILE* f = open_memstream(&client->stream->header.ptr, &client->stream->header.size);
	off_t o1, o2, o3, o4;
	
	o1 = ebml_element_start(f, MKV_EBML);
		ebml_element_string(f, MKV_DocType, "webm");
	ebml_element_end(f, o1);
	
	// Should actually be an element with unknown size but the ebml viewer program
	// can't handle it. Therefore disabled for easier debugging.
	//ebml_element_start_unkown_data_size(f, MKV_Segment);
	ebml_element_start_unkown_data_size(f, MKV_Segment);
		o2 = ebml_element_start(f, MKV_Info);
			ebml_element_uint(f, MKV_TimecodeScale, 1000000);
			ebml_element_string(f, MKV_MuxingApp, "smeb v0.1");
			ebml_element_string(f, MKV_WritingApp, "smeb v0.1");
		ebml_element_end(f, o2);
		
		o2 = ebml_element_start(f, MKV_Tracks);
			
			printf("stream: found %d tracks:\n", demuxer->nb_streams);
			for(size_t i = 0; i < demuxer->nb_streams; i++) {
				AVStream* stream = demuxer->streams[i];
				
				o3 = ebml_element_start(f, MKV_TrackEntry);
					ebml_element_uint(f, MKV_TrackNumber, i);
					ebml_element_uint(f, MKV_TrackUID, i);
					ebml_element_uint(f, MKV_FlagLacing, 1);
					ebml_element_string(f, MKV_Language, "und");
					
					switch (stream->codec->codec_type) {
						case AVMEDIA_TYPE_VIDEO:
							printf("   video, w: %d, h: %d, sar: %d/%d, %dx%d\n",
								stream->codec->width, stream->codec->height, stream->sample_aspect_ratio.num, stream->sample_aspect_ratio.den,
								stream->codec->width * stream->sample_aspect_ratio.num / stream->sample_aspect_ratio.den, stream->codec->height);
							
							if (stream->codec->codec_id != AV_CODEC_ID_VP8)
								printf("  UNSUPPORTED CODEC!\n");
							
							ebml_element_uint(f, MKV_TrackType, MKV_TrackType_Video);
							ebml_element_string(f, MKV_CodecID, "V_VP8");
							
							o4 = ebml_element_start(f, MKV_Video);
								ebml_element_uint(f, MKV_PixelWidth, stream->codec->width);
								ebml_element_uint(f, MKV_PixelHeight, stream->codec->height);
								ebml_element_uint(f, MKV_DisplayWidth, stream->codec->width * stream->sample_aspect_ratio.num / stream->sample_aspect_ratio.den);
								ebml_element_uint(f, MKV_DisplayHeight, stream->codec->height);
								ebml_element_uint(f, MKV_DisplayUnit, MKV_DisplayUnit_DisplayAspectRatio);
							ebml_element_end(f, o4);
							
							break;
						case AVMEDIA_TYPE_AUDIO:
							printf("   audio, %d channels, sampel rate: %d, bits per sample: %d\n",
								stream->codec->channels, stream->codec->sample_rate, stream->codec->bits_per_coded_sample);
							
							if (stream->codec->codec_id != AV_CODEC_ID_VORBIS)
								printf("  UNSUPPORTED CODEC!\n");
							
							ebml_element_uint(f, MKV_TrackType, MKV_TrackType_Audio);
							ebml_element_string(f, MKV_CodecID, "A_VORBIS");
							
							o4 = ebml_element_start(f, MKV_Audio);
								ebml_element_float(f, MKV_SamplingFrequency, stream->codec->sample_rate);
								ebml_element_uint(f, MKV_Channels, stream->codec->channels);
								ebml_element_uint(f, MKV_BitDepth, stream->codec->bits_per_coded_sample);
							ebml_element_end(f, o4);
							
							break;
						default:
							break;
					}
				ebml_element_end(f, o3);
			}
			
		ebml_element_end(f, o2);
	fclose(f);
}


//
// Small helper functions for buffer management and parsing
//

static ssize_t local_buffer_required_size(buffer_p local_buffer, client_p client, int client_fd) {
	int bytes_in_recv_buffer = 0;
	if ( ioctl(client_fd, SIOCINQ, &bytes_in_recv_buffer) == -1 )
		return perror("ioctl(SIOCINQ)"), -1;
	return client->buffer.size + bytes_in_recv_buffer;
}

static ssize_t local_buffer_recv(buffer_p local_buffer, client_p client, int client_fd) {
	memcpy(local_buffer->ptr, client->buffer.ptr, client->buffer.size);
	ssize_t bytes_read = read(client_fd, local_buffer->ptr + client->buffer.size, local_buffer->size - client->buffer.size);
	if (bytes_read > 0) {
		local_buffer->size = client->buffer.size + bytes_read;
		client->buffer.size = 0;
		
		return local_buffer->size;
	} else if (bytes_read == 0) {
		return -1;
	} else {
		perror("read");
		return -1;
	}
}

static void local_buffer_backup_to_client_buffer(buffer_p local_buffer, client_p client) {
	// If there is an unprocessed rest store it in the client buffer
	if (local_buffer->size > 0) {
		//printf("http: %zu rest bytes in unfinished line\n", local_buffer->size);
		client->buffer.ptr = realloc(client->buffer.ptr, local_buffer->size);
		memcpy(client->buffer.ptr, local_buffer->ptr, local_buffer->size);
		client->buffer.size = local_buffer->size;
	}
}

static ssize_t first_line_length(buffer_p buffer) {
	void* line_end = memchr(buffer->ptr, '\n', buffer->size);
	if (line_end == NULL)
		return -1;
	// Regard the \n as part of the line (thats the `+ 1`). Makes things easier down the road.
	return (line_end - (void*)buffer->ptr) + 1;
}