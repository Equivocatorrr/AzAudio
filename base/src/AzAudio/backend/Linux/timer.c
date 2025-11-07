#include "../../timer.h"

#include <time.h>

int64_t azaGetTimestamp() {
	struct timespec result;
	clock_gettime(CLOCK_REALTIME, &result);
	return (int64_t)result.tv_sec * 1000000000 + (int64_t)result.tv_nsec;
}

int64_t azaGetTimestampDeltaNanoseconds(int64_t delta) {
	return delta;
}