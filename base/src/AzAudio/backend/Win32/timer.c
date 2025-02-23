#include "../timer.h"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static int64_t frequency;
static int once = 0;

int64_t azaGetTimestamp() {
	LARGE_INTEGER result;
	QueryPerformanceCounter(&result);
	return result.QuadPart;
}

int64_t azaGetTimestampDeltaNanoseconds(int64_t delta) {
	if (!once) {
		LARGE_INTEGER weird;
		QueryPerformanceFrequency(&weird);
		frequency = weird.QuadPart;
		once = 1;
	}
	return delta * 1000000000 / frequency;
}