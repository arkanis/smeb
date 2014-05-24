#pragma once

#include <sys/time.h>
#include <stddef.h>
#include <stdint.h>
//typedef struct timeval timeval_t, *timeval_p;
typedef int64_t usec_t, *usec_p;


static inline usec_t timeval_to_usec(struct timeval time) {
	return time.tv_sec * 1000000L + time.tv_usec;
}

static inline struct timeval usec_to_timeval(usec_t time) {
	return (struct timeval){
		.tv_sec  = time / 1000000L,
		.tv_usec = time % 1000000L
	};
}

static inline usec_t time_now() {
	struct timeval now;
	gettimeofday(&now, NULL);
	return timeval_to_usec(now);
}

static inline double time_mark_ms(usec_p mark) {
	usec_t now = time_now();
	double elapsed = (now - *mark) / 1000.0;
	*mark = now;
	return elapsed;
}

/**
 * Returns the time elapsed since `start` in milliseconds.
 */
/*
static inline double time_since(timeval_t start){
	timeval_t now;
	gettimeofday(&now, NULL);
	return ( (now.tv_sec - start.tv_sec) + (now.tv_usec - start.tv_usec) / 1000000.0 ) * 1000;
}
*/
/**
 * Returns the time elapsed since `mark` in milliseconds and sets `mark` to the current time.
 * Meant to be used to measure continuous operations, e.g. time to calculate frames.
 */
/*
static inline double time_mark(timeval_p mark){
	timeval_t now;
	gettimeofday(&now, NULL);
	double elapsed = (now.tv_sec - mark->tv_sec) + (now.tv_usec - mark->tv_usec) / 1000000.0;
	*mark = now;
	return elapsed * 1000;
}
*/