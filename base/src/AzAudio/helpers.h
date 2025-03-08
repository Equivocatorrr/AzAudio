/*
	File: helpers.h
	Author: Philip Haynes
	Just some utility functions. Not to be included in headers.
*/

#ifndef AZAUDIO_HELPERS_H
#define AZAUDIO_HELPERS_H

#include <assert.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

size_t aza_align(size_t size, size_t alignment);

size_t aza_align_non_power_of_two(size_t size, size_t alignment);

// Grows the size by 3/2 repeatedly until it's at least as big as minSize
size_t aza_grow(size_t size, size_t minSize, size_t alignment);

#define AZA_MAX(a, b) ((a) > (b) ? (a) : (b))

#define AZA_MIN(a, b) ((a) < (b) ? (a) : (b))

#define AZA_CLAMP(a, min, max) AZA_MAX(min, AZA_MIN(max, a))

// Returns the 32-bit signed integer representation of a 24-bit integer stored in the lower 24 bits of a u32. You don't have to worry about what's in the high 8 bits as they'll be masked out.
int32_t azaSignExtend24Bit(uint32_t value);

#ifndef AZA_LIKELY
#ifdef __GNUC__
#define AZA_LIKELY(x) (__builtin_expect(!!(x),1))
#define AZA_UNLIKELY(x) (__builtin_expect(!!(x),0))
#else
#define AZA_LIKELY(x) (x)
#define AZA_UNLIKELY(x) (x)
#endif
#endif

void azaStrToLower(char *dst, size_t dstSize, const char *src);

// aligns sizeStart to alignment and then adds sizeAdded to it
// This assumes that sizeAdded is already aligned to alignment
static inline size_t
azaAddSizeWithAlign(size_t sizeStart, size_t sizeAdded, size_t alignment) {
	assert(sizeAdded == aza_align(sizeAdded, alignment));
	return aza_align(sizeStart, alignment) + sizeAdded;
}

static inline char*
azaGetBufferOffset(char *buffer, size_t offset, size_t alignment) {
	return buffer + aza_align(offset, alignment);
}

#define AZA_DA_DECLARE(type, name)\
struct {\
	type *data;\
	uint32_t count;\
	uint32_t capacity;\
} name;

#define AZA_DA_APPEND(type, name, value, onAllocFail) {\
	if ((name).count == (name).capacity) {\
		uint32_t aza_newCapacity = (uint32_t)aza_grow((name).capacity, (name).count+1, 8);\
		type *aza_newData = aza_calloc(aza_newCapacity, sizeof(type));\
		if (!aza_newData) { onAllocFail; }\
		(name).capacity = aza_newCapacity;\
		memcpy(aza_newData, (name).data, (name).count * sizeof(type));\
		aza_free((name).data);\
		(name).data = aza_newData;\
	}\
	(name).data[(name).count++] = value;\
}

#define AZA_DA_INSERT(type, name, index, value, onAllocFail) {\
	assert((uint32_t)(index) >= 0);\
	assert((uint32_t)(index) <= (name).count);\
	if ((name).count == (name).capacity) {\
		uint32_t aza_newCapacity = (uint32_t)aza_grow((name).capacity, (name).count+1, 8);\
		type *aza_newData = aza_calloc(aza_newCapacity, sizeof(type));\
		if (!aza_newData) { onAllocFail; }\
		(name).capacity = aza_newCapacity;\
		for (uint32_t aza_i = 0; aza_i < (uint32_t)(index); aza_i++) {\
			aza_newData[aza_i] = (name).data[aza_i];\
		}\
		aza_newData[(index)] = (value);\
		for (uint32_t aza_i = (index); aza_i < (name).count; aza_i++) {\
			aza_newData[aza_i+1] = (name).data[aza_i];\
		}\
		aza_free((name).data);\
		(name).data = aza_newData;\
	} else {\
		for (uint32_t aza_i = (name).count; aza_i > (uint32_t)(index); aza_i--) {\
			(name).data[aza_i] = (name).data[aza_i-1];\
		}\
		(name).data[(index)] = (value);\
	}\
	(name).count++;\
}

#define AZA_DA_ERASE(name, index, num) {\
	for (int64_t aza_i = (index); aza_i < (int64_t)(name).count - (num); aza_i++) {\
		(name).data[aza_i] = (name).data[aza_i + (num)];\
	}\
	(name).count--;\
}

#define AZA_DA_DEINIT(name) {\
	if ((name).data) {\
		aza_free((name).data);\
		(name).data = NULL;\
	}\
	(name).count = 0;\
	(name).capacity = 0;\
}

#ifdef __cplusplus
}
#endif

#endif // AZAUDIO_HELPERS_H