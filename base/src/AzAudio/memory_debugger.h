/*
	File: memory_debugger.h
	Author: Philip Haynes
	Memory allocation and tracking strategy that makes debugging the heap possible.
*/

#ifndef AZAUDIO_MEMORY_DEBUGGER_H
#define AZAUDIO_MEMORY_DEBUGGER_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void azaMemoryDebuggerInit();
void azaMemoryDebuggerDeinit();

// filepath can be NULL, in which case we only output to the console
void azaMemoryDebuggerReport(const char *filepath, bool console, bool listAllBadBlocks);

void* aza_calloc_debug(const char *filepath, int line, size_t count, size_t size);
void* aza_malloc_debug(const char *filepath, int line, size_t size);
void* aza_realloc_debug(const char *filepath, int line, void *block, size_t size);
void aza_free_debug(const char *filepath, int line, void *block);

#ifdef __cplusplus
}
#endif

#endif // AZAUDIO_MEMORY_DEBUGGER_H
