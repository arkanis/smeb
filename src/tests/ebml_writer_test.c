// Required for open_memstream
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>

#include "testing.h"
#include "../ebml_writer.h"


void test_ebml_write_element_id() {
	uint8_t expected[4] = {0};
	
	char* buffer_ptr = NULL;
	size_t buffer_size = 0;
	FILE* buffer = open_memstream(&buffer_ptr, &buffer_size);
	
	ebml_write_element_id(buffer, 0xAE);
	fflush(buffer);
	expected[0] = 0xAE;
	check_int( memcmp(buffer_ptr, expected, 1), 0 );
	rewind(buffer);
	
	ebml_write_element_id(buffer, 0x4282);
	fflush(buffer);
	expected[0] = 0x42;
	expected[1] = 0x82;
	check_int( memcmp(buffer_ptr, expected, 2), 0 );
	rewind(buffer);
	
	ebml_write_element_id(buffer, 0x2AD7B1);
	fflush(buffer);
	expected[0] = 0x2A;
	expected[1] = 0xD7;
	expected[2] = 0xB1;
	check_int( memcmp(buffer_ptr, expected, 3), 0 );
	rewind(buffer);
	
	ebml_write_element_id(buffer, 0x1A45DFA3);
	fflush(buffer);
	expected[0] = 0x1A;
	expected[1] = 0x45;
	expected[2] = 0xDF;
	expected[3] = 0xA3;
	check_int( memcmp(buffer_ptr, expected, 4), 0 );
	rewind(buffer);
	
	fclose(buffer);
	free(buffer_ptr);
}

void test_ebml_uint_required_bytes() {
	check_int( ebml_uint_required_bytes(0), 1 );
	check_int( ebml_uint_required_bytes(1), 1 );
	
	check_int( ebml_uint_required_bytes(      0x0F), 1 );
	check_int( ebml_uint_required_bytes(      0x7E), 1 );  // last valid 1 byte length
	check_int( ebml_uint_required_bytes(      0x7F), 1 );  // last valid 1 byte ID
	check_int( ebml_uint_required_bytes(    0x0FFF), 2 );
	check_int( ebml_uint_required_bytes(    0x3FFE), 2 );  // last valid 2 byte length
	check_int( ebml_uint_required_bytes(    0x3FFF), 2 );  // last valid 2 byte ID
	check_int( ebml_uint_required_bytes(  0x0FFFFF), 3 );
	check_int( ebml_uint_required_bytes(  0x1FFFFE), 3 );  // last valid 3 byte length
	check_int( ebml_uint_required_bytes(  0x1FFFFF), 3 );  // last valid 3 byte ID
	check_int( ebml_uint_required_bytes(0x00FFFFFF), 4 );
	check_int( ebml_uint_required_bytes(0x0FFFFFFE), 4 );  // last valid 4 byte length
	check_int( ebml_uint_required_bytes(0x0FFFFFFF), 4 );  // last valid 4 byte ID
	
	check_int( ebml_uint_required_bytes(      0x00FFFFFFFF), 5 );
	check_int( ebml_uint_required_bytes(      0x07FFFFFFFE), 5 );  // last valid 5 byte length
	check_int( ebml_uint_required_bytes(      0x07FFFFFFFF), 5 );  // last valid 5 byte ID
	check_int( ebml_uint_required_bytes(    0x00FFFFFFFFFF), 6 );
	check_int( ebml_uint_required_bytes(    0x03FFFFFFFFFE), 6 );  // last valid 6 byte length
	check_int( ebml_uint_required_bytes(    0x03FFFFFFFFFF), 6 );  // last valid 6 byte ID
	check_int( ebml_uint_required_bytes(  0x00FFFFFFFFFFFF), 7 );
	check_int( ebml_uint_required_bytes(  0x01FFFFFFFFFFFE), 7 );  // last valid 7 byte length
	check_int( ebml_uint_required_bytes(  0x01FFFFFFFFFFFF), 7 );  // last valid 7 byte ID
	check_int( ebml_uint_required_bytes(0x000FFFFFFFFFFFFF), 8 );
	check_int( ebml_uint_required_bytes(0x00FFFFFFFFFFFFFE), 8 );  // last valid 8 byte length
	check_int( ebml_uint_required_bytes(0x00FFFFFFFFFFFFFF), 8 );  // last valid 8 byte ID
}

void test_ebml_int_required_bytes() {
	check_int( ebml_int_required_bytes(0), 1 );
	check_int( ebml_int_required_bytes(1), 1 );
	check_int( ebml_int_required_bytes(-1), 1 );
	
	check_int( ebml_int_required_bytes(       -64), 1 );  // smallest 7 bit value
	check_int( ebml_int_required_bytes(        63), 1 );  // largest 7 bit value
	check_int( ebml_int_required_bytes(       -65), 2 );
	check_int( ebml_int_required_bytes(        64), 2 );
	check_int( ebml_int_required_bytes(     -8192), 2 );  // smallest 14 bit value
	check_int( ebml_int_required_bytes(      8191), 2 );  // largest 14 bit value
	check_int( ebml_int_required_bytes(     -8193), 3 );
	check_int( ebml_int_required_bytes(      8192), 3 );
	check_int( ebml_int_required_bytes(  -1048576), 3 );  // smallest 21 bit value
	check_int( ebml_int_required_bytes(   1048575), 3 );  // largest 21 bit value
	check_int( ebml_int_required_bytes(  -1048577), 4 );
	check_int( ebml_int_required_bytes(   1048576), 4 );
	check_int( ebml_int_required_bytes(-134217728), 4 );  // smallest 28 bit value
	check_int( ebml_int_required_bytes( 134217727), 4 );  // largest 28 bit value
	check_int( ebml_int_required_bytes(-134217729), 5 );
	check_int( ebml_int_required_bytes( 134217728), 5 );
	
	check_int( ebml_int_required_bytes(      -17179869184), 5 );  // smallest 35 bit value
	check_int( ebml_int_required_bytes(       17179869183), 5 );  // largest 35 bit value
	check_int( ebml_int_required_bytes(      -17179869185), 6 );
	check_int( ebml_int_required_bytes(       17179869184), 6 );
	check_int( ebml_int_required_bytes(    -2199023255552), 6 );  // smallest 42 bit value
	check_int( ebml_int_required_bytes(     2199023255551), 6 );  // largest 42 bit value
	check_int( ebml_int_required_bytes(    -2199023255553), 7 );
	check_int( ebml_int_required_bytes(     2199023255552), 7 );
	check_int( ebml_int_required_bytes(  -281474976710656), 7 );  // smallest 49 bit value
	check_int( ebml_int_required_bytes(   281474976710655), 7 );  // largest 49 bit value
	check_int( ebml_int_required_bytes(  -281474976710657), 8 );
	check_int( ebml_int_required_bytes(   281474976710656), 8 );
	check_int( ebml_int_required_bytes(-36028797018963968), 8 );  // smallest 56 bit value
	check_int( ebml_int_required_bytes( 36028797018963967), 8 );  // largest 56 bit value
}

void test_ebml_write_data_size_automatic_bytes() {
	uint8_t expected[8] = {0};
	
	char* buffer_ptr = NULL;
	size_t buffer_size = 0;
	FILE* buffer = open_memstream(&buffer_ptr, &buffer_size);
	
	// 1 byte
	ebml_write_data_size(buffer, 1, 0);
	fflush(buffer);
	check_int( buffer_size, 1 );
	expected[0] = 0x81;
	check_int( memcmp(buffer_ptr, expected, buffer_size), 0 );
	rewind(buffer);
	
	// 2 byte
	ebml_write_data_size(buffer, 0x7F, 0);
	fflush(buffer);
	check_int( buffer_size, 2 );
	expected[0] = 0x40;
	expected[1] = 0x7F;
	check_int( memcmp(buffer_ptr, expected, buffer_size), 0 );
	rewind(buffer);
	
	ebml_write_data_size(buffer, 4096, 0);
	fflush(buffer);
	check_int( buffer_size, 2 );
	expected[0] = 0x50;
	expected[1] = 0x00;
	check_int( memcmp(buffer_ptr, expected, buffer_size), 0 );
	rewind(buffer);
	
	// 3 byte
	ebml_write_data_size(buffer, 1000000, 0);
	fflush(buffer);
	check_int( buffer_size, 3 );
	expected[0] = 0x2F;
	expected[1] = 0x42;
	expected[2] = 0x40;
	check_int( memcmp(buffer_ptr, expected, buffer_size), 0 );
	rewind(buffer);
	
	// 4 byte
	ebml_write_data_size(buffer, 0x0A2F4240, 0);
	fflush(buffer);
	check_int( buffer_size, 4 );
	expected[0] = 0x1A;
	expected[1] = 0x2F;
	expected[2] = 0x42;
	expected[3] = 0x40;
	check_int( memcmp(buffer_ptr, expected, buffer_size), 0 );
	rewind(buffer);
	
	// 8 byte
	ebml_write_data_size(buffer, 0x00C000001A2F4240, 0);
	fflush(buffer);
	check_int( buffer_size, 8 );
	expected[0] = 0x01;
	expected[1] = 0xC0;
	expected[2] = 0x00;
	expected[3] = 0x00;
	expected[4] = 0x1A;
	expected[5] = 0x2F;
	expected[6] = 0x42;
	expected[7] = 0x40;
	check_int( memcmp(buffer_ptr, expected, buffer_size), 0 );
	rewind(buffer);
	
	fclose(buffer);
	free(buffer_ptr);
}

void test_ebml_write_data_size_fixed_bytes() {
	uint8_t expected[8] = {0};
	
	char* buffer_ptr = NULL;
	size_t buffer_size = 0;
	FILE* buffer = open_memstream(&buffer_ptr, &buffer_size);
	
	// 4 byte
	ebml_write_data_size(buffer, 5, 4);
	fflush(buffer);
	check_int( buffer_size, 4 );
	expected[0] = 0x10;
	expected[1] = 0x00;
	expected[2] = 0x00;
	expected[3] = 0x05;
	check_int( memcmp(buffer_ptr, expected, buffer_size), 0 );
	rewind(buffer);
	
	// 8 bytes
	ebml_write_data_size(buffer, 5, 8);
	fflush(buffer);
	check_int( buffer_size, 8 );
	expected[0] = 0x01;
	expected[1] = 0x00;
	expected[2] = 0x00;
	expected[3] = 0x00;
	expected[4] = 0x00;
	expected[5] = 0x00;
	expected[6] = 0x00;
	expected[7] = 0x05;
	check_int( memcmp(buffer_ptr, expected, buffer_size), 0 );
	rewind(buffer);
	
	fclose(buffer);
	free(buffer_ptr);
}

void test_ebml_write_unkown_data_size() {
	uint8_t expected[8] = {0};
	
	char* buffer_ptr = NULL;
	size_t buffer_size = 0;
	FILE* buffer = open_memstream(&buffer_ptr, &buffer_size);
	
	ebml_write_unkown_data_size(buffer);
	fflush(buffer);
	check_int( buffer_size, 1 );
	expected[0] = 0xff;
	check_int( memcmp(buffer_ptr, expected, buffer_size), 0 );
	rewind(buffer);
	
	fclose(buffer);
	free(buffer_ptr);
}

void test_ebml_element_start_and_end() {
	char* buffer_ptr = NULL;
	size_t buffer_size = 0;
	FILE* buffer = open_memstream(&buffer_ptr, &buffer_size);
	
	off_t offset = ebml_element_start(buffer, MKV_EBML);
	int64_t dummy_data = -1;
	fwrite(&dummy_data, sizeof(dummy_data), 1, buffer);
	ebml_element_end(buffer, offset);
	
	fflush(buffer);
	check_int( buffer_size, 4+4+8 );
	uint8_t expected[16] = {
		0x1A, 0x45, 0xDF, 0xA3,                         // element ID
		0x10, 0x00, 0x00, 0x08,                         // encoded data size
		0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff  // dummy data
	};
	check_int( memcmp(buffer_ptr, expected, buffer_size), 0 );
	rewind(buffer);
	
	fclose(buffer);
	free(buffer_ptr);
}

void test_ebml_element_start_unkown_data_size() {
	char* buffer_ptr = NULL;
	size_t buffer_size = 0;
	FILE* buffer = open_memstream(&buffer_ptr, &buffer_size);
	
	ebml_element_start_unkown_data_size(buffer, MKV_Segment);
	int64_t dummy_data = -1;
	fwrite(&dummy_data, sizeof(dummy_data), 1, buffer);
	
	fflush(buffer);
	check_int( buffer_size, 4+1+8 );
	uint8_t expected[13] = {
		0x18, 0x53, 0x80, 0x67,                         // element ID
		0xff,                                           // encoded data size
		0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff  // dummy data
	};
	check_int( memcmp(buffer_ptr, expected, buffer_size), 0 );
	rewind(buffer);
	
	fclose(buffer);
	free(buffer_ptr);
}

void test_ebml_element_uint() {
	char* buffer_ptr = NULL;
	size_t buffer_size = 0;
	FILE* buffer = open_memstream(&buffer_ptr, &buffer_size);
	
	ebml_element_uint(buffer, MKV_TrackNumber, 17);
	
	fflush(buffer);
	check_int( buffer_size, 1+1+1 );
	uint8_t expected[3] = {
		0xD7,  // element ID
		0x81,  // encoded data size
		0x11   // dummy data
	};
	check_int( memcmp(buffer_ptr, expected, buffer_size), 0 );
	rewind(buffer);
	
	fclose(buffer);
	free(buffer_ptr);
}

void test_ebml_element_int() {
	char* buffer_ptr = NULL;
	size_t buffer_size = 0;
	FILE* buffer = open_memstream(&buffer_ptr, &buffer_size);
	
	ebml_element_int(buffer, MKV_TrackNumber, -17);
	
	fflush(buffer);
	check_int( buffer_size, 1+1+1 );
	uint8_t expected[3] = {
		0xD7,  // element ID
		0x81,  // encoded data size
		0xEF   // dummy data
	};
	check_int( memcmp(buffer_ptr, expected, buffer_size), 0 );
	rewind(buffer);
	
	fclose(buffer);
	free(buffer_ptr);
}

void test_ebml_element_string() {
	char* buffer_ptr = NULL;
	size_t buffer_size = 0;
	FILE* buffer = open_memstream(&buffer_ptr, &buffer_size);
	
	ebml_element_string(buffer, MKV_DocType, "webm");
	
	fflush(buffer);
	check_int( buffer_size, 2+1+4 );
	uint8_t expected[7] = {
		0x42, 0x82,         // element ID
		0x84,               // encoded data size
		'w', 'e', 'b', 'm'  // dummy data
	};
	check_int( memcmp(buffer_ptr, expected, buffer_size), 0 );
	rewind(buffer);
	
	fclose(buffer);
	free(buffer_ptr);
}

void test_ebml_element_float() {
	char* buffer_ptr = NULL;
	size_t buffer_size = 0;
	FILE* buffer = open_memstream(&buffer_ptr, &buffer_size);
	
	ebml_element_float(buffer, MKV_Duration, 12.375);
	
	fflush(buffer);
	check_int( buffer_size, 2+1+4 );
	uint8_t expected[7] = {
		0x44, 0x89,             // element ID
		0x84,                   // encoded data size
		0x41, 0x46, 0x00, 0x00  // 12.375 as big endian 32 bit float
	};
	check_int( memcmp(buffer_ptr, expected, buffer_size), 0 );
	rewind(buffer);
	
	fclose(buffer);
	free(buffer_ptr);
}

void test_ebml_element_double() {
	char* buffer_ptr = NULL;
	size_t buffer_size = 0;
	FILE* buffer = open_memstream(&buffer_ptr, &buffer_size);
	
	ebml_element_double(buffer, MKV_Duration, 12.375);
	
	fflush(buffer);
	check_int( buffer_size, 2+1+8 );
	uint8_t expected[11] = {
		0x44, 0x89,                                     // element ID
		0x88,                                           // encoded data size
		0x40, 0x28, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00  // 12.375 as big endian 64 bit double
	};
	check_int( memcmp(buffer_ptr, expected, buffer_size), 0 );
	rewind(buffer);
	
	fclose(buffer);
	free(buffer_ptr);
}


int main() {
	run(test_ebml_write_element_id);
	run(test_ebml_uint_required_bytes);
	run(test_ebml_int_required_bytes);
	run(test_ebml_write_data_size_automatic_bytes);
	run(test_ebml_write_data_size_fixed_bytes);
	run(test_ebml_write_unkown_data_size);
	run(test_ebml_element_start_and_end);
	run(test_ebml_element_start_unkown_data_size);
	run(test_ebml_element_uint);
	run(test_ebml_element_int);
	run(test_ebml_element_string);
	run(test_ebml_element_float);
	run(test_ebml_element_double);
	
	return show_report();
}