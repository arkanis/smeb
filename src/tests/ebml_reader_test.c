// Required for open_memstream
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "testing.h"
#include "../ebml_reader.h"
#include "../ebml_writer.h"

const char* test_file_name = "ebml_reader_test.mkv";


void test_new_and_destroy() {
	int fd = open(test_file_name, O_RDONLY | O_NONBLOCK);
	ebml_reader_t reader;
	ebml_reader_new(&reader, fd);
	
	ebml_reader_destroy(&reader);
	close(fd);
}

void test_read_element_id_with_full_buffer() {
	uint32_t id = 0;
	size_t   pos = 0;
	
	uint8_t id_ebml[] = { 0x1A, 0x45, 0xDF, 0xA3, 0, 0, 0, 0 };
	id = ebml_read_element_id(id_ebml, sizeof(id_ebml), &pos);
	check(id  == 0x1A45DFA3);
	check_int(pos, 4);
	
	uint8_t id_timecode_scale[] = { 0x2A, 0xD7, 0xB1, 0, 0, 0, 0 };
	pos = 0;
	id = ebml_read_element_id(id_timecode_scale, sizeof(id_timecode_scale), &pos);
	check(id  == 0x2AD7B1);
	check_int(pos, 3);
	
	uint8_t id_ebml_version[] = { 0x42, 0x86, 0, 0, 0, 0 };
	pos = 0;
	id = ebml_read_element_id(id_ebml_version, sizeof(id_ebml_version), &pos);
	check(id  == 0x4286);
	check_int(pos, 2);
	
	uint8_t id_void[] = { 0xEC, 0, 0, 0, 0 };
	pos = 0;
	id = ebml_read_element_id(id_void, sizeof(id_void), &pos);
	check(id  == 0xEC);
	check_int(pos, 1);
}

void test_read_element_id_error_cases() {
	uint32_t id = 0;
	size_t   pos = 0;
	uint8_t id_ebml[] = { 0x1A, 0x45, 0xDF, 0xA3, 0, 0, 0, 0 };
	
	id = ebml_read_element_id(NULL, 0, &pos);
	check_int(id,  0);
	check_int(pos, 0);
	
	id = ebml_read_element_id(id_ebml, 1, &pos);
	check_int(id,  0);
	check_int(pos, 0);
	
	id = ebml_read_element_id(id_ebml, 3, &pos);
	check_int(id,  0);
	check_int(pos, 0);
	
	id = ebml_read_element_id(id_ebml, 4, &pos);
	check_int(id,  0x1A45DFA3);
	check_int(pos, 4);
}

void test_read_data_size_with_full_buffer() {
	uint64_t samples[] = {
		1, 126,
		16382,
		2097150,
		268435454,
		34359738366,
		4398046511102,
		562949953421310,
		72057594037927934
	};
	
	char* buffer = NULL;
	size_t buffer_size = 0, pos = 0;
	
	FILE* f = open_memstream(&buffer, &buffer_size);
	for(size_t i = 0; i < sizeof(samples) / sizeof(samples[0]); i++)
		ebml_write_data_size(f, samples[i], 0);
	fclose(f);
	
	for(size_t i = 0; i < sizeof(samples) / sizeof(samples[0]); i++) {
		uint64_t value = ebml_read_data_size(buffer + pos, buffer_size - pos, &pos);
		check_msg(value == samples[i], "got %llu, expected %llu\n", value, samples[i]);
	}
	
	check_int(pos, buffer_size);
}

void test_read_data_size_error_cases() {
	uint8_t buffer[] = { 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE };
	uint64_t value = 0;
	size_t pos = 0;
	
	for(size_t i = 0; i < 8; i++) {
		value = ebml_read_data_size(&buffer, 0, &pos);
		check_int(value, 0);
		check_int(pos,   0);
	}
	
	value = ebml_read_data_size(&buffer, 8, &pos);
	check_int(value, 0x00FFFFFFFFFFFFFE);
	check_int(pos,   8);
}

void test_read_data_size_unknown_sizes() {
	uint8_t buffer[] = {
		0xFF,
		0x7F, 0xFF,
		0x3F, 0xFF, 0xFF,
		0x1F, 0xFF, 0xFF, 0xFF,
		0x0F, 0xFF, 0xFF, 0xFF, 0xFF,
		0x07, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
		0x03, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
		0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
	};
	size_t buffer_size = sizeof(buffer), pos = 0;
	
	for(size_t i = 0; i < 8; i++) {
		uint64_t value = ebml_read_data_size(buffer + pos, buffer_size - pos, &pos);
		check_msg((int64_t)value == -1, "got %lld, expected %d\n", value, -1);
	}
	
	check_int(pos, buffer_size);
}

void test_next_event_root_level() {
	int fd = open(test_file_name, O_RDONLY);
	ebml_reader_t reader;
	ebml_reader_new(&reader, fd);
	
	ebml_reader_event_t event;
	
	check( ebml_reader_next_event(&reader, &event) == true );
	check_int(event.type, EBML_ELEMENT_START);
	check(event.id   == MKV_EBML);
	check(event.size == 7);
	
	check( ebml_reader_next_event(&reader, &event) == true );
	check_int(event.type, EBML_ELEMENT_END);
	check(event.id   == MKV_EBML);
	check(event.size == 0);
	check(event.data == NULL);
	
	check( ebml_reader_next_event(&reader, &event) == true );
	check_int(event.type, EBML_ELEMENT_START);
	check(event.id   == MKV_Segment);
	check(event.size == -1);
	
	ebml_reader_destroy(&reader);
	close(fd);
}

void test_next_event_nested_levels() {
	int fd = open(test_file_name, O_RDONLY);
	ebml_reader_t reader;
	ebml_reader_new(&reader, fd);
	
	ebml_reader_event_t event;
	
	check( ebml_reader_next_event(&reader, &event) == true );
	check_int(event.type, EBML_ELEMENT_START);
	check(event.id   == MKV_EBML);
	check(event.size == 7);
	
	check( ebml_reader_enter(&reader) == true );
	
	check( ebml_reader_next_event(&reader, &event) == true );
	check_int(event.type, EBML_ELEMENT_START);
	check(event.id   == MKV_DocType);
	check(event.size == 4);
	
	check( ebml_reader_next_event(&reader, &event) == true );
	check_int(event.type, EBML_ELEMENT_END);
	check(event.id   == MKV_DocType);
	check(event.size == 0);
	
	check( ebml_reader_next_event(&reader, &event) == true );
	check_int(event.type, EBML_ELEMENT_END);
	check(event.id   == MKV_EBML);
	check(event.size == 0);
	
	ebml_reader_destroy(&reader);
	close(fd);
}


static void write_test_file(const char* filename) {
	FILE* f = fopen(filename, "wb");
	
	off_t o1, o2, o3, o4;
	
	o1 = ebml_element_start(f, MKV_EBML);
		ebml_element_string(f, MKV_DocType, "webm");
	ebml_element_end(f, o1);
	
	ebml_element_start_unkown_data_size(f, MKV_Segment);
	//o1 = ebml_element_start(f, MKV_Segment);
		o2 = ebml_element_start(f, MKV_Info);
			ebml_element_uint(f, MKV_TimecodeScale, 1000000);
			ebml_element_string(f, MKV_MuxingApp, "smeb v0.1");
			ebml_element_string(f, MKV_WritingApp, "smeb v0.1");
		ebml_element_end(f, o2);
		
		o2 = ebml_element_start(f, MKV_Tracks);
			// Video track
			o3 = ebml_element_start(f, MKV_TrackEntry);
				ebml_element_uint(f, MKV_TrackNumber, 1);
				ebml_element_uint(f, MKV_TrackUID, 1);
				ebml_element_uint(f, MKV_TrackType, MKV_TrackType_Video);
				
				ebml_element_string(f, MKV_CodecID, "V_VP8");
				ebml_element_uint(f, MKV_FlagLacing, 1);
				ebml_element_string(f, MKV_Language, "und");
				
				o4 = ebml_element_start(f, MKV_Video);
					ebml_element_uint(f, MKV_PixelWidth, 720);
					ebml_element_uint(f, MKV_PixelHeight, 576);
					ebml_element_uint(f, MKV_DisplayWidth, 768);
					ebml_element_uint(f, MKV_DisplayHeight, 576);
					ebml_element_uint(f, MKV_DisplayUnit, MKV_DisplayUnit_DisplayAspectRatio);
				ebml_element_end(f, o4);
				
			ebml_element_end(f, o3);
			
			// Audio track
			o3 = ebml_element_start(f, MKV_TrackEntry);
				ebml_element_uint(f, MKV_TrackNumber, 1);
				ebml_element_uint(f, MKV_TrackUID, 1);
				ebml_element_uint(f, MKV_TrackType, MKV_TrackType_Video);
				
				ebml_element_string(f, MKV_CodecID, "A_VORBIS");
				ebml_element_uint(f, MKV_FlagLacing, 1);
				ebml_element_string(f, MKV_Language, "ger");
				
				o4 = ebml_element_start(f, MKV_Audio);
					ebml_element_float(f, MKV_SamplingFrequency, 48000);
					ebml_element_uint(f, MKV_Channels, 2);
					ebml_element_uint(f, MKV_BitDepth, 16);
				ebml_element_end(f, o4);
				
			ebml_element_end(f, o3);
			
		ebml_element_end(f, o2);
		
	//ebml_element_end(f, o1);
	
	fclose(f);
}

int main() {
	write_test_file(test_file_name);
	
	run(test_new_and_destroy);
	run(test_read_element_id_with_full_buffer);
	run(test_read_element_id_error_cases);
	run(test_read_data_size_with_full_buffer);
	run(test_read_data_size_error_cases);
	run(test_read_data_size_unknown_sizes);
	run(test_next_event_root_level);
	run(test_next_event_nested_levels);
	
	unlink(test_file_name);
	return show_report();
}