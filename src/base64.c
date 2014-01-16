#include "base64.h"

uint8_t base64_char_to_6bit(uint8_t base64_char) {
	if (base64_char >= 'A') {
		if (base64_char >= 'a')
			return base64_char - 'a' + 26;
		else
			return base64_char - 'A';
	} else {
		if (base64_char >= '0')
			return base64_char - '0' + 52;
		else if (base64_char == '+')
			return 62;
		else if (base64_char == '/')
			return 63;
	}
	
	// Stupid return value but otherwise we would overwrite bits of previous values
	return 0;
}

ssize_t base64_decode(const char* encoded_data, size_t encoded_data_size, char* decode_buffer, size_t decode_buffer_size) {
	// i = input index, o = output index
	size_t i = 0, o = 0;
	while ( i + 4 <= encoded_data_size && o + 3 <= decode_buffer_size ) {
		uint32_t buffer = 0;
		
		buffer |= base64_char_to_6bit(encoded_data[i++]) << 18;
		buffer |= base64_char_to_6bit(encoded_data[i++]) << 12;
		buffer |= base64_char_to_6bit(encoded_data[i++]) <<  6;
		buffer |= base64_char_to_6bit(encoded_data[i++]) <<  0;
		
		if ( encoded_data[i-1] != '=' ) {
			decode_buffer[o++] = (buffer & 0x00ff0000) >> 16;
			decode_buffer[o++] = (buffer & 0x0000ff00) >>  8;
			decode_buffer[o++] = (buffer & 0x000000ff) >>  0;
		} else if ( encoded_data[i-2] != '=' ) {
			decode_buffer[o++] = (buffer & 0x00ff0000) >> 16;
			decode_buffer[o++] = (buffer & 0x0000ff00) >>  8;
		} else {
			decode_buffer[o++] = (buffer & 0x00ff0000) >> 16;
		}
	}
	
	return o;
}