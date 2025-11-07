/*
	File: aza_c_std.c
	Author: Philip Haynes
*/

#include "aza_c_std.h"
#include "math.h"

#include <ctype.h> // tolower
#include <threads.h>
#include <stdio.h>
#include <stdarg.h>



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

int azaTextCountLines(const char *text) {
	if (!text) return 0;
	int result = 1;
	while (*text) {
		if (*text == '\n') result++;
		text++;
	}
	return result;
}

static thread_local char azaTextFormatBuffer[2048] = {0};
const char* azaTextFormat(const char *fmt, ...) {
	va_list list;
	va_start(list, fmt);
	vsnprintf(azaTextFormatBuffer, sizeof(azaTextFormatBuffer), fmt, list);
	va_end(list);
	return azaTextFormatBuffer;
}

const char* azaTextFormat2(int substring, const char *fmt, ...) {
	assert(substring >= 0);
	assert(substring <= sizeof(azaTextFormatBuffer)/2);
	char *result = azaTextFormatBuffer;
	while (substring-- > 0) {
		while (*result++) {}
	}
	size_t offset = result - azaTextFormatBuffer;
	assert(offset < sizeof(azaTextFormatBuffer));
	va_list list;
	va_start(list, fmt);
	vsnprintf(result, sizeof(azaTextFormatBuffer) - offset, fmt, list);
	va_end(list);
	return result;
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