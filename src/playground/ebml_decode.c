#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include "../ebml_reader.h"

static ssize_t try_to_extract_mkv_header(void* buffer_ptr, size_t buffer_size);


int main(int argc, char** argv) {
	if (argc != 2) {
		fprintf(stderr, "usage: %s mkv-file\n", argv[0]);
		return 1;
	}
	
	int fd = open(argv[1], O_RDONLY);
	
	// Read into incremental buffer
	size_t buffer_size = 8;
	void*  buffer_ptr = malloc(buffer_size);
	size_t buffer_filled = 0;
	ssize_t bytes_read = 0, header_size = 0;
	
	while ( (bytes_read = read(fd, buffer_ptr + buffer_filled, buffer_size - buffer_filled)) > 0 ) {
		printf("%zu bytes read\n", bytes_read);
		buffer_filled += bytes_read;
		
		if ( (header_size = try_to_extract_mkv_header(buffer_ptr, buffer_filled)) > 0 ) {
			printf("got %zd bytes as header\n", header_size);
			break;
		}
		
		// No luck with getting the complete header, try to read more data
		if (buffer_filled == buffer_size) {
			buffer_size += 64 * 1024;
			buffer_ptr = realloc(buffer_ptr, buffer_size);
		}
	}
	
	if (bytes_read == -1) {
		perror("read");
		return 1;
	}
	
	close(fd);
}

static ssize_t try_to_extract_mkv_header(void* buffer_ptr, size_t buffer_size) {
	size_t buffer_pos = 0;
	
	uint32_t id = 0;
	do {
		size_t pos = 0;
		id = ebml_read_element_id(buffer_ptr + buffer_pos, buffer_size - buffer_pos, &pos);
		if (pos == 0) {
			printf("failed to read element id\n");
			return -1;
		}
		buffer_pos += pos;
		
		pos = 0;
		uint64_t size = ebml_read_data_size(buffer_ptr + buffer_pos, buffer_size - buffer_pos, &pos);
		if (pos == 0) {
			printf("failed to read data size\n");
			return -1;
		}
		buffer_pos += pos;
		
		if (id == 0x18538067) {
			printf("<Segment 0x%08lX bytes>, patching to unknown size, entering\n", size);
			// Add a leading 0 bit for each addidional size byte, then convert to big endian so
			// we can directly copy it over the old size.
			uint64_t unknown_size = __builtin_bswap64(0xffffffffffffffff >> (pos - 1));
			memcpy(buffer_ptr + buffer_pos - pos, &unknown_size, pos);
			continue;
		}
		
		if (buffer_pos + size > buffer_size) {
			printf("failed to read element data of <0x%08X %zu bytes>\n", id, size);
			return -1;
		}
		buffer_pos += size;
		
		printf("<0x%08X %zu bytes>\n", id, size);
	} while (id != 0x1654AE6B);
	
	return buffer_pos;
}