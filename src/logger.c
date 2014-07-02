#include <stdarg.h>
#include <stdio.h>
#include <strings.h>
#include "logger.h"


int log_level = LOG_DEBUG;


void logger_setup(int level) {
	log_level = level;
}

void logger_message(int level, const char *file, int line, const char *func, const char *format, ...) {
	if (level < log_level)
		return;
	
	char *label = NULL;
	switch(level) {
		case LOG_DEBUG: label = "debug"; break;
		case LOG_WARN:  label = "warn";  break;
		case LOG_ERROR: label = "error"; break;
	}
	
	va_list args;
	va_start(args, format);
	
	if (label) {
		const char* filename = rindex(file, '/');
		filename = (filename) ? filename + 1 : file;
		fprintf(stderr, "[%s in %s:%d %s()]: ", label, filename, line, func);
	}
	vfprintf(stderr, format, args);
	fprintf(stderr, "\n");
	
	va_end(args);
}