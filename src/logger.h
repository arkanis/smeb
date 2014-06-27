#pragma once

/**
 * A simple logger which prefixes each message with the source code location.
 */

#define LOG_DEBUG 1
#define LOG_INFO  2
#define LOG_WARN  3
#define LOG_ERROR 4

void logger_setup(int level);
void logger_message(int level, const char *file, int line, const char *func, const char *format, ...);

#define debug(...) logger_message(LOG_DEBUG, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define info(...)  logger_message(LOG_INFO,  __FILE__, __LINE__, __func__, __VA_ARGS__)
#define warn(...)  logger_message(LOG_WARN,  __FILE__, __LINE__, __func__, __VA_ARGS__)
#define error(...) logger_message(LOG_ERROR, __FILE__, __LINE__, __func__, __VA_ARGS__)