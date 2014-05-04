#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>

#include "ebml_reader.h"

#define STATE_ELEM 1
#define STATE_DATA 2

static void init_reader(ebml_reader_p reader);


void ebml_reader_new(ebml_reader_p reader, int fd) {
	init_reader(reader);
	reader->fd = fd;
	reader->element_stack = array_with(5, element_frame_t); 
}

void ebml_reader_destroy(ebml_reader_p reader) {
	free(reader->buffer);
	array_destroy(reader->element_stack);
	init_reader(reader);
}

static void init_reader(ebml_reader_p reader) {
	*reader = (ebml_reader_t){
		.fd            = 0,
		.buffer        = NULL,
		.buffer_size   = 0,
		.buffer_pos    = 0,
		.capture_depth = 0,
		.data_size     = 0,
		.state = STATE_ELEM,
		.element_stack = NULL
	};
}


uint32_t ebml_read_element_id(void* buffer, size_t buffer_size, size_t* pos) {
	if (buffer_size < 1)
		return 0;
	
	uint8_t* byte_ptr = buffer;
	// __builtin_clz operates on 32 bits, but we just want 8 bits... so move the
	// 8 bits to the front (otherwise we get a very high number of leading zeros).
	int leading_zeros = __builtin_clz(*byte_ptr << 24);
	
	size_t octet_count = leading_zeros + 1;
	if (buffer_size < octet_count)
		return 0;
	
	uint32_t element_id = 0;
	for(size_t i = 0; i < octet_count; i++) {
		element_id <<= 8;
		element_id |= *byte_ptr;
		byte_ptr++;
	}
	
	*pos += byte_ptr - (uint8_t*)buffer;
	return element_id;
}

uint64_t ebml_read_data_size(void* buffer, size_t buffer_size, size_t* pos) {
	if (buffer_size < 1)
		return 0;
	
	uint8_t* byte_ptr = buffer;
	// __builtin_clz operates on 32 bits, but we just want 8 bits... so move the
	// 8 bits to the front (otherwise we get a very high number of leading zeros).
	int leading_zeros = __builtin_clz(*byte_ptr << 24);
	
	size_t octet_count = leading_zeros + 1;
	if (buffer_size < octet_count)
		return 0;
	
	uint64_t data_size = 0;
	
	size_t payload_bits_in_first_byte = 8 - octet_count;
	data_size = *byte_ptr & ~(1 << payload_bits_in_first_byte);
	byte_ptr++;
	
	for(size_t i = 1; i < octet_count; i++) {
		data_size <<= 8;
		data_size |= *byte_ptr;
		byte_ptr++;
	}
	
	// If all data bits are set to 1 we have an unknown size (return as -1)
	if (__builtin_popcountll(data_size) == octet_count * 7)
		data_size = -1;
	
	*pos += byte_ptr - (uint8_t*)buffer;
	return data_size;
}

/**
 * Reads an EBML element (element ID, size and the element content).
 * 
 * Returns an element ID of 0 if the buffer doesn't contain the entire element.
 */
ebml_elem_t ebml_read_element(void* buffer, size_t buffer_size, size_t* buffer_pos) {
	ebml_elem_t element = ebml_read_element_header(buffer, buffer_size, buffer_pos);
	if (element.id != 0) {
		if (*buffer_pos + element.header_size + element.data_size <= buffer_size)
			*buffer_pos += element.data_size;
		else
			element.id = 0;
	}
	return element;
}

/**
 * Reads an EBML element header (element ID and size).
 * 
 * Returns an element ID of 0 if the buffer doesn't contain the entire header.
 */
ebml_elem_t ebml_read_element_header(void* buffer, size_t buffer_size, size_t* buffer_pos) {
	ebml_elem_t element = { 0 };
	size_t pos = *buffer_pos;
	
	element.id = ebml_read_element_id(buffer + pos, buffer_size - pos, &pos);
	if (pos == *buffer_pos)
		return element;
	
	element.data_size = ebml_read_data_size(buffer + pos, buffer_size - pos, &pos);
	if (pos == *buffer_pos) {
		element.id = 0;
		return element;
	}
	
	element.data_ptr = buffer + pos;
	element.header_size = pos - *buffer_pos;
	*buffer_pos = pos;
	
	return element;
}

uint64_t ebml_read_uint(void* buffer, size_t buffer_size) {
	uint64_t value = 0;
	
	if (buffer_size > sizeof(value))
		return (uint64_t)-1LL;
	
	memcpy((void*)&value + sizeof(value) - buffer_size, buffer, buffer_size);
	return __builtin_bswap64(value);
}

int64_t ebml_read_int(void* buffer, size_t buffer_size) {
	int64_t value = 0;
	
	if (buffer_size > sizeof(value))
		return INT64_MIN;
	
	memcpy((void*)&value + sizeof(value) - buffer_size, buffer, buffer_size);
	// Convert the value from big endian to little endian
	value = __builtin_bswap64(value);
	// Next we need to sign extend the value to 64 bits.
	// Shift the value bytes to the front of the 64bit value. This way a sign
	// bit becomes the most significant bit.
	value = value << ((sizeof(value) - buffer_size) * 8);
	// Shift the value bytes down to where they belong. Since value is signed
	// the higher order bits are sign extended correctly.
	value = value >> ((sizeof(value) - buffer_size) * 8);
	
	return value;
}




bool ebml_reader_next_event(ebml_reader_p reader, ebml_reader_event_p event) {
	int bytes_available = 0;
	if ( ioctl(reader->fd, FIONREAD, &bytes_available) == -1 )
		return perror("ioctl(FIONREAD)"), false;
	
	if (bytes_available > 0) {
		reader->buffer = realloc(reader->buffer, reader->buffer_size + bytes_available);
		ssize_t bytes_read = read(reader->fd, reader->buffer + reader->buffer_size, bytes_available);
		if (bytes_read == -1)
			return perror("read()"), false;
		reader->buffer_size += bytes_read;
	}
	
	switch(reader->state) {
		case STATE_ELEM:
			{
				size_t bytes_consumed = 0;
				uint32_t element_id = ebml_read_element_id(reader->buffer + reader->buffer_pos, reader->buffer_size - reader->buffer_pos, &bytes_consumed);
				uint64_t data_size = ebml_read_data_size(reader->buffer + reader->buffer_pos + bytes_consumed, reader->buffer_size - reader->buffer_pos - bytes_consumed, &bytes_consumed);
				
				if (element_id != 0 && data_size != 0) {
					reader->state = STATE_DATA;
					reader->buffer_pos += bytes_consumed;
					reader->data_size = data_size;
					
					event->type = EBML_ELEMENT_START;
					event->id   = element_id;
					event->size = data_size;
					event->data = NULL;
					
					array_append(reader->element_stack, element_frame_t, ( (element_frame_t){ .id = element_id, .end_pos = 0 } ));
					
					return true;
				}
				
			}
			break;
		case STATE_DATA:
			{
				size_t bytes_in_buffer = reader->buffer_size - reader->buffer_pos;
				if (bytes_in_buffer < reader->data_size) {
					// No enough data left in buffer, maybe next time
					reader->data_size  -= bytes_in_buffer;
					reader->buffer_pos += bytes_in_buffer;
				} else {
					// All data in buffer, so consume it. We're at the end of the element.
					reader->buffer_pos += reader->data_size;
					reader->data_size = 0;
					reader->state = STATE_ELEM;
					
					element_frame_t current_element = array_elem(reader->element_stack, element_frame_t, reader->element_stack->length - 1);
					array_remove(reader->element_stack, reader->element_stack->length - 1);
					
					event->type = EBML_ELEMENT_END;
					event->id   = current_element.id;
					event->size = 0;
					event->data = NULL;
					
					return true;
				}
			}
			break;
	}
	
	// read element id
	// read element length
	// emit start event
	
	return false;
}

bool ebml_reader_enter(ebml_reader_p reader) {
	reader->state = STATE_ELEM;
	return true;
}