#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>

#include "../ebml_reader.h"
#include "../ebml_writer.h"
#include "../hash.h"


typedef void (*element_printer_t)(void* buffer_ptr, size_t buffer_size);
typedef struct {
	const char* name;
	bool is_master;
	element_printer_t printer;
} ebml_elem_info_t, *ebml_elem_info_p;


void print_int(void* buffer_ptr, size_t buffer_size) {
	int64_t value = ebml_read_int(buffer_ptr, buffer_size);
	printf("%ld", value);
}

void print_simple_block(void* buffer_ptr, size_t buffer_size) {
	size_t block_pos = 0;
	uint64_t track_number = ebml_read_data_size(buffer_ptr + block_pos, buffer_size - block_pos, &block_pos);
	
	if (track_number != 1)
		return;
	
	int16_t timecode = ebml_read_int(buffer_ptr + block_pos, 2);
	block_pos += 2;
	uint8_t flags = ebml_read_uint(buffer_ptr + block_pos, 1);
	block_pos += 1;
	
#	define MKV_FLAG_KEYFRAME    (0b10000000)
#	define MKV_FLAG_INVISIBLE   (0b00001000)
#	define MKV_FLAG_LACING      (0b00000110)
#	define MKV_FLAG_DISCARDABLE (0b00000001)
	
	printf("track: %lu, timecode: %-4d, flags:", track_number, timecode);
	if ( flags & MKV_FLAG_KEYFRAME )
		printf(" keyframe");
	if ( flags & MKV_FLAG_INVISIBLE )
		printf(" invisible");
	if ( flags & MKV_FLAG_DISCARDABLE )
		printf(" discardable");
	
	uint8_t lacing = (flags & MKV_FLAG_LACING) >> 1;
	switch(lacing) {
		//case 0: if (show_verbose) printf("no lacing"); break;
		case 1: printf("Xiph lacing");       break;
		case 2: printf("fixed-size lacing"); break;
		case 3: printf("EBML lacing");       break;
	}
}

int main(int argc, char** argv) {
	if (argc != 2)
		return fprintf(stderr, "usage: %s filename\n", argv[0]), 1;
	
	hash_p element_infos = hash_of(ebml_elem_info_t);
	hash_put(element_infos, MKV_EBML,    ebml_elem_info_t, ((ebml_elem_info_t){ .name = "EBML",    .is_master = true }));
	hash_put(element_infos, MKV_Segment, ebml_elem_info_t, ((ebml_elem_info_t){ .name = "Segment", .is_master = true }));
	hash_put(element_infos, MKV_Info,    ebml_elem_info_t, ((ebml_elem_info_t){ .name = "Info",    .is_master = true }));
	hash_put(element_infos, MKV_Cluster, ebml_elem_info_t, ((ebml_elem_info_t){ .name = "Cluster", .is_master = true }));
	hash_put(element_infos, MKV_Timecode,    ebml_elem_info_t, ((ebml_elem_info_t){ .name = "Timecode",    .is_master = false, .printer = print_int }));
	hash_put(element_infos, MKV_SimpleBlock, ebml_elem_info_t, ((ebml_elem_info_t){ .name = "SimpleBlock", .is_master = false, .printer = print_simple_block }));
	ebml_elem_info_t default_info = (ebml_elem_info_t){ .name = "unknown", .is_master = false };
	
	// Open the EBML file
	int fd = open(argv[1], O_RDONLY);
	if (fd == -1)
		return perror("open"), 1;
	
	struct stat sb;
	if ( fstat(fd, &sb) == -1 )
		return perror("fstat"), 1;
	size_t buffer_size = sb.st_size;
	
	void* buffer_ptr = mmap(NULL, buffer_size, PROT_READ, MAP_PRIVATE, fd, 0);
	if (buffer_ptr == MAP_FAILED)
		return perror("mmap"), 1;
	
	// Dump EBML elements
	size_t buffer_pos = 0, pos = 0, level = 0;
	size_t level_size[256];
	ebml_elem_t e;
	
	level_size[0] = buffer_size;
	
	while(true) {
		pos = 0;
		e = ebml_read_element_header(buffer_ptr + buffer_pos, level_size[level] - buffer_pos, &pos);
		if (pos == 0) {
			// No more stuff to read in the buffer
			if (level == 0) {
				// We're at the root level, EOF then
				break;
			} else {
				// Level end reached, up one level
				level--;
				continue;
			}
		}
		
		ebml_elem_info_p info = hash_get_ptr(element_infos, e.id);
		if (info) {
			printf("%*s<%s size: %-5zu ", (int)level*2, "", info->name, (size_t)e.data_size);
			if (info->printer) {
				info->printer(buffer_ptr + buffer_pos + e.header_size, e.data_size);
			}
			printf(">\n");
		} else {
			printf("%*s<%08X size: %zu>\n", (int)level*2, "", e.id, (size_t)e.data_size);
			info = &default_info;
		}
		
		if (e.data_size == (uint64_t)-1 || info->is_master) {
			buffer_pos += e.header_size;
			level++;
			level_size[level] = buffer_pos + e.data_size;
		} else {
			buffer_pos += e.header_size + e.data_size;
		}
	}
	
	// Cleanup time
	munmap(buffer_ptr, buffer_size);
	close(fd);
	
	return 0;
}