/*
	File: helpers.c
	Author: Philip Haynes
*/

#include "helpers.h"
#include "math.h"

#include <assert.h>
#include <ctype.h>


size_t aza_grow(size_t startSize, size_t minSize, size_t alignment) {
	assert(alignment > 0);
	startSize = AZA_MAX(startSize, alignment);
	while (startSize < minSize) {
		startSize = aza_align(startSize * 3 / 2, alignment);
	}
	return startSize;
}

float aza_db_to_ampf(float db) {
	return powf(10.0f, db/20.0f);
}

float aza_amp_to_dbf(float amp) {
	if (amp < 0.0f) amp = 0.0f;
	return log10f(amp)*20.0f;
}

size_t aza_align(size_t size, size_t alignment) {
	return (size + alignment-1) & ~(alignment-1);
}

size_t aza_align_non_power_of_two(size_t size, size_t alignment) {
	if (size % alignment == 0) {
		return size;
	} else {
		return (size/alignment+1)*alignment;
	}
}

bool aza_str_begins_with(const char *string, const char *test) {
	while (*string != 0) {
		if (*test == 0) {
			return true;
		}
		if (*string++ != *test++) {
			return false;
		}
	}
	return false;
}

int32_t azaSignExtend24Bit(uint32_t value) {
	uint32_t signBit = 1 << 23;
	return (int32_t)(((value & 0xffffff) ^ signBit) - signBit);
}

void azaStrToLower(char *dst, size_t dstSize, const char *src) {
	for (int i = 0; i < dstSize; i++) {
		dst[i] = tolower(src[i]);
		if (src[i] == '\0') break;
	}
}