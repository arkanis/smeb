#pragma once

#include <stdint.h>
#include <sys/types.h>

uint8_t base64_char_to_6bit(uint8_t base64_char);
ssize_t base64_decode(const char* encoded_data, size_t encoded_data_size, char* decode_buffer, size_t decode_buffer_size);