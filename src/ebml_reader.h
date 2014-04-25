#pragma once

#include <stddef.h>
#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>
#include "array.h"


typedef struct {
	int fd;
	void*  buffer;
	size_t buffer_size;
	size_t capture_depth;
	int state;
	size_t buffer_pos;
	
	uint64_t data_size;
	uint32_t current_element_id;
	ssize_t  current_data_size;
	array_p  element_stack;
} ebml_reader_t, *ebml_reader_p;

typedef struct {
	uint32_t id;
	ssize_t  end_pos;
} element_frame_t, *element_frame_p;

typedef struct {
	int type;
	uint64_t id;
	// -1 is unknown length
	ssize_t size;
	void* data;
} ebml_reader_event_t, *ebml_reader_event_p;

#define EBML_ELEMENT_START 1
#define EBML_ELEMENT_END   2
#define EBML_EOF           3


void ebml_reader_new(ebml_reader_p reader, int fd);
void ebml_reader_destroy(ebml_reader_p reader);
bool ebml_reader_next_event(ebml_reader_p reader, ebml_reader_event_p event);
bool ebml_reader_enter(ebml_reader_p reader);

uint32_t ebml_read_element_id(void* buffer, size_t buffer_size, size_t* pos);
uint64_t ebml_read_data_size(void* buffer, size_t buffer_size, size_t* pos);

/*
element_start(id, length);
	return 0; // do nothing
	return 1; // capture content
element_end(id, size_t data_size, void* data);
	// data_size == 0 and data == NULL when not captured

void test() {
	ebml_reader_t reader = ebml_reader_new(fd);
	
	ebml_reader_event_t event;
	while (ebml_reader_next_event(reader, &event)) {
		switch(event.type) {
			case EBML_ELEMENT_START:
				printf("start %u, %zu bytes\n", event.id, event.size);
				if (event.id == MKV_TrackEntry)
					ebml_reader_start_capture(reader);
				break;
			case EBML_ELEMENT_END:
				printf("end %u, %p, %zu bytes\n", event.id, event.data, event.size);
				switch(event.id) {
					case MKV_TimecodeScale:
						printf("TimecodeScale: %lu\n", ebml_read_uint(event.data, event.size));
						break;
					case MKV_MuxingApp:
						printf("MuxingApp: %.*s\n", event.size, event.data);
						break;
					case MKV_TrackEntry:
						ebml_reader_stop_capture(reader);
						{
							uint64_t track_number = 0;
							char* codec_id = NULL;
							size_t codec_id_size = 0;
							void* elem = NULL;
							size_t elem_size = 0;
							
							ebml_read(event.data, event.size, (ebml_read_t[]){
								(ebml_read_t){ MKV_TrackNumber, EBML_UINT,   &track_number, NULL },
								(ebml_read_t){ MKV_CodecID,     EBML_INT,    &codec_id, NULL },
								(ebml_read_t){ MKV_CodecID,     EBML_STRING, &codec_id, &codec_id_size },
								(ebml_read_t){ MKV_CodecID,     EBML_FLOAT,  &codec_id, NULL },
								(ebml_read_t){ MKV_CodecID,     EBML_DOUBLE, &codec_id, NULL },
								(ebml_read_t){ MKV_CodecID,     EBML_ELEMENT, &codec_id, &element_size },
								(ebml_read_t){ 0 }
							});
						}
						break;
				}
				break;
			case EBML_EOF:
				break;
		}
	}
	
	ebml_reader_destroy(reader);
}
*/