#pragma once

#include <stdio.h>
#include <stdint.h>
#include "matroska.h"


// Functions to start and end elements that should contain other elements
long ebml_element_start                 (FILE* file, uint32_t element_id);
void ebml_element_end                   (FILE* file, long offset);
void ebml_element_start_unkown_data_size(FILE* file, uint32_t element_id);

// Scalar element functions
void ebml_element_uint  (FILE* file, uint32_t element_id, uint64_t    value);
void ebml_element_int   (FILE* file, uint32_t element_id, int64_t     value);
void ebml_element_string(FILE* file, uint32_t element_id, const char* value);
void ebml_element_float (FILE* file, uint32_t element_id, float       value);
void ebml_element_double(FILE* file, uint32_t element_id, double      value);


// Low level functions to write encoded values
size_t ebml_write_element_id(FILE* file, uint32_t element_id);
size_t ebml_write_data_size(FILE* file, uint64_t value, size_t bytes);
size_t ebml_write_unkown_data_size(FILE* file);

// Utility functions
size_t ebml_encoded_uint_required_bytes(uint64_t value);
size_t ebml_encoded_int_required_bytes(int64_t value);
size_t ebml_unencoded_uint_required_bytes(uint64_t value);
size_t ebml_unencoded_int_required_bytes(int64_t value);