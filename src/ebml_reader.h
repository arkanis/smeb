#pragma once

#include <stddef.h>
#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>
#include "matroska.h"


typedef struct {
	uint32_t id;
	void*    data_ptr;
	uint64_t data_size, header_size;
} ebml_elem_t, *ebml_elem_p;


uint32_t ebml_read_element_id(void* buffer, size_t buffer_size, size_t* pos);
uint64_t ebml_read_data_size(void* buffer, size_t buffer_size, size_t* pos);

ebml_elem_t ebml_read_element(void* buffer, size_t buffer_size, size_t* pos);
ebml_elem_t ebml_read_element_header(void* buffer, size_t buffer_size, size_t* pos);

uint64_t ebml_read_uint(void* buffer, size_t buffer_size);
int64_t ebml_read_int(void* buffer, size_t buffer_size);