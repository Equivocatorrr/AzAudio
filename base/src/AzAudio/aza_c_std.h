/*
	File: aza_c_std.h
	Author: Philip Haynes
	We include some c standard library headers, and define byte as well.
	Also lots of basic utility stuff is in here.
*/

#ifndef AZAUDIO_AZA_C_STD_H
#define AZAUDIO_AZA_C_STD_H

#include <stdbool.h> // bool
#include <stdint.h> // uint32_t et al
#include <stddef.h> // size_t
#include <assert.h>
#include <string.h> // memcpy et al
#include <stdalign.h> // alignas
#include <stdlib.h> // qsort

#ifdef __cplusplus
extern "C" {
#endif



// basic types



// Use signed char because it's considered distinct from char and unsigned char in the type system.
// Note that this means if you ever convert this type to a wider integer type, you'll get sign extension unless you cast to uint8_t first.
// This isn't really meant to be treated as an integer anyway, just a blob of bits to be reinterpreted or pad things out.
typedef signed char byte;
static_assert(sizeof(byte) == 1, "We're fucked");



// Allocators



typedef struct azaAllocatorCallbacks {
	// returns zero-initialized memory aligned to at least an 8-byte boundary
	void* (*fp_calloc)(size_t count, size_t size);
	// returns uninitialized memory aligned to at least an 8-byte boundary
	void* (*fp_malloc)(size_t size);
	// tries to resize a memory block in place if possible (returning the existing pointer), else copy it into a new block and return the new pointer
	void* (*fp_realloc)(void *memblock, size_t size);
	// frees a block of memory that had been previously returned from fp_calloc, fp_malloc, or fp_realloc
	void (*fp_free)(void *block);
} azaAllocatorCallbacks;
extern azaAllocatorCallbacks azaAllocator;

static inline void* aza_calloc(size_t count, size_t size) {
	return azaAllocator.fp_calloc(count, size);
}

static inline void* aza_malloc(size_t size) {
	return azaAllocator.fp_malloc(size);
}

static inline void* aza_realloc(void *memblock, size_t size) {
	return azaAllocator.fp_realloc(memblock, size);
}

static inline void aza_free(void *block) {
	azaAllocator.fp_free(block);
}



// Preprocessor stuff



// C and C++ diverged on this for some reason, so we get to do fun stuff like this.
#ifdef __cplusplus
	#define AZA_CLITERAL(s) s
#else
	#define AZA_CLITERAL(s) (s)
#endif

#if defined(__clang__)
	#define AZAUDIO_BUILT_WITH_CLANG 1
	#define AZAUDIO_BUILT_WITH_GCC 0
	#define AZAUDIO_BUILT_WITH_MSVC 0
#elif defined(__GNUG__) || defined(__GNUC__)
	#define AZAUDIO_BUILT_WITH_CLANG 0
	#define AZAUDIO_BUILT_WITH_GCC 1
	#define AZAUDIO_BUILT_WITH_MSVC 0
#elif defined(_MSC_VER)
	#define AZAUDIO_BUILT_WITH_CLANG 0
	#define AZAUDIO_BUILT_WITH_GCC 0
	#define AZAUDIO_BUILT_WITH_MSVC 1
#else
	#define AZAUDIO_BUILT_WITH_CLANG 0
	#define AZAUDIO_BUILT_WITH_GCC 0
	#define AZAUDIO_BUILT_WITH_MSVC 0
#endif

#if AZAUDIO_BUILT_WITH_CLANG
	#define AZA_FORCE_INLINE(...) inline __VA_ARGS__
#elif AZAUDIO_BUILT_WITH_GCC
	#define AZA_FORCE_INLINE(...) inline __VA_ARGS__ __attribute__((always_inline))
#elif AZAUDIO_BUILT_WITH_MSVC
	#define AZA_FORCE_INLINE(...) __forceinline __VA_ARGS__
#else
	#define AZA_FORCE_INLINE(...) inline __VA_ARGS__
#endif

#if AZAUDIO_BUILT_WITH_CLANG || AZAUDIO_BUILT_WITH_GCC
	#define AZA_LIKELY(x) (__builtin_expect(!!(x),1))
	#define AZA_UNLIKELY(x) (__builtin_expect(!!(x),0))
	// The above are useful for boolean conditions, but this can be used in switch statements too
	#define AZA_EXPECT(x, v) (__builtin_expect((x), (v)))
#else
	#define AZA_LIKELY(x) (x)
	#define AZA_UNLIKELY(x) (x)
	#define AZA_EXPECT(x, v) (x)
#endif

#define AZA_FUNCTION_NAME __func__



// String stuff



// Guarantees dst is null-terminated, and that no more than dstSize-1 bytes are copied. Returns src size.
size_t aza_strcpy(char *dst, const char *src, size_t dstSize);

// test can be shorter than string, and as long as the first strlen(test) characters match, this will return true.
bool aza_str_begins_with(const char *string, const char *test);

void aza_str_to_lower(char *dst, const char *src, size_t dstSize);



// alignment and buffers



// only works with power of 2 alignments. Also see aza_align_non_power_of_two
static inline size_t aza_align(size_t size, size_t alignment) {
	return (size + alignment-1) & ~(alignment-1);
}

static inline size_t aza_align_non_power_of_two(size_t size, size_t alignment) {
	if (size % alignment == 0) {
		return size;
	} else {
		return (size/alignment+1)*alignment;
	}
}

// Grows the size by 3/2 repeatedly until it's at least as big as minSize
size_t aza_grow(size_t size, size_t minSize, size_t alignment);

// aligns sizeStart to alignment and then adds sizeAdded to it
// This asserts that sizeAdded is already aligned to alignment
static inline size_t
azaAddSizeWithAlign(size_t sizeStart, size_t sizeAdded, size_t alignment) {
	assert(sizeAdded == aza_align(sizeAdded, alignment));
	return aza_align(sizeStart, alignment) + sizeAdded;
}

static inline char*
azaGetBufferOffset(char *buffer, size_t offset, size_t alignment) {
	return buffer + aza_align(offset, alignment);
}

// Returns the 32-bit signed integer representation of a 24-bit integer stored in the lower 24 bits of a u32. You don't have to worry about what's in the high 8 bits as they'll be masked out.
int32_t azaSignExtend24Bit(uint32_t value);



// Dynamic Arrays



#define AZA_DA_DECLARE(type, name)\
struct {\
	type *data;\
	uint32_t count;\
	uint32_t capacity;\
} name;

#define AZA_DA_RESERVE_COUNT(name, num, onAllocFail)\
if ((name).capacity < (num)) {\
	uint32_t aza_newCapacity = (uint32_t)aza_grow((name).capacity, (num), 8);\
	void *aza_newData = aza_realloc((name).data, aza_newCapacity * sizeof((name).data[0]));\
	if (!aza_newData) { onAllocFail; }\
	(name).data = aza_newData;\
	memset((name).data + (name).capacity, 0, sizeof((name).data[0]) * (aza_newCapacity - (name).capacity));\
	(name).capacity = aza_newCapacity;\
}

#define AZA_DA_RESERVE_ONE_AT_END(name, onAllocFail) AZA_DA_RESERVE_COUNT(name, (name).count+1, onAllocFail)

#define AZA_DA_APPEND(name, value, onAllocFail) {\
	AZA_DA_RESERVE_ONE_AT_END(name, onAllocFail);\
	(name).data[(name).count++] = value;\
}

#define AZA_DA_INSERT(name, index, value, onAllocFail) {\
	assert((uint32_t)(index) <= (name).count);\
	if ((name).count == (name).capacity) {\
		uint32_t aza_newCapacity = (uint32_t)aza_grow((name).capacity, (name).count+1, 8);\
		void *aza_newData = aza_realloc((name).data, aza_newCapacity * sizeof((name).data[0]));\
		if (!aza_newData) { onAllocFail; }\
		(name).data = aza_newData;\
		memset((name).data + (name).capacity, 0, sizeof((name).data[0]) * (aza_newCapacity - (name).capacity));\
		(name).capacity = aza_newCapacity;\
	}\
	memmove((name).data + ((index) + 1), (name).data + (index), sizeof((name).data[0]) * ((name).count - (index)));\
	(name).data[(uint32_t)(index)] = (value);\
	(name).count++;\
}

#define AZA_DA_ERASE(name, index, num) {\
	assert((uint32_t)(index) <= (name).count);\
	memmove((name).data + (index), (name).data + ((index) + 1), sizeof((name).data[0]) * ((name).count - (index)));\
	(name).count -= (num);\
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

#endif // AZAUDIO_AZA_C_STD_H