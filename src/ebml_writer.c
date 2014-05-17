#include <string.h>
#include "ebml_writer.h"


//
// Functions to start and end elements that should contain other elements
//

/**
 * Writes the element ID and a 4 byte zero data size to reserve space for the proper data
 * size. The offset to this 4 byte space is returned.
 */
long ebml_element_start(FILE* file, uint32_t element_id) {
	ebml_write_element_id(file, element_id);
	long length_offset = ftell(file);
	ebml_write_data_size(file, 0, 4);
	return length_offset;
}

/**
 * Ends an element that was started with `ebml_element_start()`. For this it calculates
 * the number of bytes written since the elment has been started and writes them to the
 * 4 byte space reserved for the element size.
 */
void ebml_element_end(FILE* file, long offset) {
	long current_offset = ftell(file);
	
	fseek(file, offset, SEEK_SET);
	ebml_write_data_size(file, current_offset - offset - 4, 4);
	
	fseek(file, current_offset, SEEK_SET);
}

/**
 * Starts an element with unknown data size. Useful for streaming.
 */
void ebml_element_start_unkown_data_size(FILE* file, uint32_t element_id) {
	ebml_write_element_id(file, element_id);
	ebml_write_unkown_data_size(file);
}


//
// Scalar element functions
//

void ebml_element_uint(FILE* file, uint32_t element_id, uint64_t value) {
	size_t required_data_bytes = ebml_unencoded_uint_required_bytes(value);
	
	ebml_write_element_id(file, element_id);
	ebml_write_data_size(file, required_data_bytes, 0);
	
	value = __builtin_bswap64(value);
	uint8_t* value_ptr = (uint8_t*)&value;
	fwrite(value_ptr + sizeof(value) - required_data_bytes, required_data_bytes, 1, file);
}

void ebml_element_int(FILE* file, uint32_t element_id, int64_t value) {
	size_t required_data_bytes = ebml_unencoded_int_required_bytes(value);
	
	ebml_write_element_id(file, element_id);
	ebml_write_data_size(file, required_data_bytes, 0);
	
	value = __builtin_bswap64(value);
	uint8_t* value_ptr = (uint8_t*)&value;
	fwrite(value_ptr + sizeof(value) - required_data_bytes, required_data_bytes, 1, file);
}

void ebml_element_string(FILE* file, uint32_t element_id, const char* value) {
	size_t required_data_bytes = strlen(value);
	
	ebml_write_element_id(file, element_id);
	ebml_write_data_size(file, required_data_bytes, 0);
	
	fwrite(value, required_data_bytes, 1, file);
}

void ebml_element_float(FILE* file, uint32_t element_id, float value) {
	size_t required_data_bytes = sizeof(value);
	
	ebml_write_element_id(file, element_id);
	ebml_write_data_size(file, required_data_bytes, 0);
	
	uint32_t* float_ptr = (uint32_t*)&value;
	*float_ptr = __builtin_bswap32(*float_ptr);
	fwrite(&value, required_data_bytes, 1, file);
}

void ebml_element_double(FILE* file, uint32_t element_id, double value) {
	size_t required_data_bytes = sizeof(value);
	
	ebml_write_element_id(file, element_id);
	ebml_write_data_size(file, required_data_bytes, 0);
	
	uint64_t* double_ptr = (uint64_t*)&value;
	*double_ptr = __builtin_bswap64(*double_ptr);
	fwrite(&value, required_data_bytes, 1, file);
}


//
// Low level functions to write encoded values
//

/**
 * Writes an EBML ID with the appropiate number of bytes.
 * 
 * EBML IDs are already encoded variable length uints with up to 4 byte. When writing an ID we
 * just need to figure out many bytes we're supposed to write. However we always use uint32_t
 * (4 byte uints) in the API. Therefore we don't need to interprete the length descriptor but
 * can just skip all leading zero _bytes_ of the 32bit ID.
 * 
 * Like all EBML values the ID is written in big endian byte order.
 */
size_t ebml_write_element_id(FILE* file, uint32_t element_id) {
	int leading_zero_bits = __builtin_clz(element_id);
	int length_in_bytes = 4 - leading_zero_bits / 8;
	uint32_t big_endian_id = __builtin_bswap32(element_id);
	uint8_t* big_endian_id_ptr = (uint8_t*)&big_endian_id;
	
	return fwrite(big_endian_id_ptr + sizeof(uint32_t) - length_in_bytes, length_in_bytes, 1, file);
}

/**
 * Writes an EBML data size. Takes into account that bit patterns of all ones are
 * reserved for an unknown size. In this case the next larger representation is
 * used.
 * 
 * The number of bytes written can be specified in the `bytes` argument. When set
 * to `0` as few bytes as possible are written.
 */
size_t ebml_write_data_size(FILE* file, uint64_t value, size_t bytes) {
	if (bytes == 0)
		bytes = ebml_encoded_uint_required_bytes(value);
	// Length to large to be encoded
	if (bytes == 0)
		return 0;
	
	// We can store 7 bits per byte. If they are all 1s we have a reserved length
	// and need the next larger prepresentation.
	size_t one_count = __builtin_popcountll(value);
	if (one_count >= bytes * 7)
		bytes++;
	
	// Create the length descriptor (a 1 bit after sufficient 0 bits, all at the
	// right byte in the 64 bit value). Combine it with the value and store the
	// result in big endian byte order.
	uint64_t prefix = 1LL << (8 - bytes + 8 * (bytes - 1));
	value = __builtin_bswap64(value | prefix);
	
	// Finally write the required amount of bytes
	uint8_t* value_ptr = (uint8_t*)&value;
	return fwrite(value_ptr + 8 - bytes, bytes, 1, file);
}

/**
 * Writes an unknown data size. This is a data size field with all bits set to 1.
 */
size_t ebml_write_unkown_data_size(FILE* file) {
	uint8_t length = 0xff;
	return fwrite(&length, 1, sizeof(length), file);
}


//
// Utility functions
//

/**
 * Returns the number of bytes required to encode the uint value in EBML.
 * Returns 0 if the value is to large to be encoded.
 * 
 * Basic idea is to get the index of the first set bit (using the GCC __builtin_clz())
 * and caculate how many bytes we need to represent that number.
 * 
 * leading zeros: 64 -7 -7 -7 -7 -7 -7 -7 -7
 *                64 57 50 43 36 29 22 15  8
 * required bytes:  1  2  3  4  5  6  7  8
 * 
 * This calculates does the trick: 8 - (leading zeros - 8) / 7
 * 
 * 1. Move bits to origin:                                    leading zeros - 8
 * 2. Scale down to index of 7 bit group the first bit is in: ... / 7
 * 3. Calculate the number of bytes needed:                   8 - ...
 *    If the byte is in group 0 we need 8 bytes, if it's in
 *    group 7 we need 1 byte.
 */
size_t ebml_encoded_uint_required_bytes(uint64_t value) {
	// Use 1 byte to encode zero
	if (value == 0)
		return 1;
	
	int leading_zeros = __builtin_clzll(value);
	if (leading_zeros < 8) {
		fprintf(stderr, "EBML: Value 0x%016lx to large to encode as uint!\n", value);
		return 0;
	}
	
	return 8 - (leading_zeros - 8) / 7;
}

/**
 * Returns the number of bytes required to encode the int value in EBML.
 * Returns 0 if the value is out of the encodable range.
 * 
 * Same logic as ebml_encoded_uint_required_bytes(). We take negative
 * numbers into account by counting the leading sign bits instead of zeros.
 */
size_t ebml_encoded_int_required_bytes(int64_t value) {
	//int leading_sign_bits = __builtin_clrsbll(value);
	int leading_sign_bits = __builtin_clzll( (value >= 0) ? value : ~value ) - 1;
	
	if (leading_sign_bits < 8) {
		fprintf(stderr, "EBML: Value %ld out of encodable int range!\n", value);
		return 0;
	}
	
	return 8 - (leading_sign_bits - 8) / 7;
}

size_t ebml_unencoded_uint_required_bytes(uint64_t value) {
	int leading_zeros = __builtin_clzll(value);
	int value_bits = 64 - leading_zeros;
	int value_bytes = (value_bits - 1) / 8 + 1;
	return value_bytes;
}

size_t ebml_unencoded_int_required_bytes(int64_t value) {
	//int leading_sign_bits = __builtin_clrsbll(value);
	int leading_sign_bits = __builtin_clzll( (value >= 0) ? value : ~value ) - 1;
	int value_bits = 64 - leading_sign_bits;
	int value_bytes = (value_bits - 1) / 8 + 1;
	return value_bytes;
}