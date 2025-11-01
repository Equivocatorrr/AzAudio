/*
	File: aza_c_std.c
	Author: Philip Haynes
*/

#include "aza_c_std.h"
#include "math.h"

#include <ctype.h> // tolower



// Allocators



azaAllocatorCallbacks azaAllocator = {
	calloc,
	malloc,
	realloc,
	free,
};



// String stuff



size_t aza_strcpy(char *dst, const char *src, size_t dstSize) {
	size_t srcSize = strlen(src);
	size_t toCopy = AZA_MIN(srcSize, dstSize-1);
	memcpy(dst, src, toCopy);
	dst[toCopy] = 0;
	return srcSize;
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

void aza_str_to_lower(char *dst, const char *src, size_t dstSize) {
	for (int i = 0; i < dstSize; i++) {
		dst[i] = tolower(src[i]);
		if (src[i] == '\0') break;
	}
}



// alignment and buffers



size_t aza_grow(size_t startSize, size_t minSize, size_t alignment) {
	assert(alignment > 0);
	startSize = AZA_MAX(startSize, alignment);
	while (startSize < minSize) {
		startSize = aza_align(startSize * 3 / 2, alignment);
	}
	return startSize;
}

int32_t azaSignExtend24Bit(uint32_t value) {
	uint32_t signBit = 1 << 23;
	return (int32_t)(((value & 0xffffff) ^ signBit) - signBit);
}