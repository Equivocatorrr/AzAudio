/*
	File: memory_debugger.c
	Author: Philip Haynes
*/

#include "memory_debugger.h"
#include "backend/threads.h"

#include <stdio.h>
#include <sys/stat.h>

#include "AzAudio.h"
#include "math.h"

// Handle AZA_DA_... macros making these debug functions recursive
#undef aza_calloc
#undef aza_malloc
#undef aza_realloc
#undef aza_free
#define aza_calloc azaAllocator.fp_calloc
#define aza_malloc azaAllocator.fp_malloc
#define aza_realloc azaAllocator.fp_realloc
#define aza_free azaAllocator.fp_free

static azaMutex mutex;

typedef enum ActionKind {
	ACTION_KIND_CALLOC=0,
	ACTION_KIND_MALLOC,
	ACTION_KIND_REALLOC,
	ACTION_KIND_FREE,
} ActionKind;
static const char *ActionKindStrings[] = {
	"calloc",
	"malloc",
	"realloc",
	"free"
};

typedef struct SourceLocation {
	const char *filepath;
	int line;
	ActionKind kind;
} SourceLocation;

#define MEMORY_ENTRY_MAX_ACTIONS_COUNT 32

typedef struct MemoryEntry {
	void *base;
	void *block;
	size_t size;
	SourceLocation actions[MEMORY_ENTRY_MAX_ACTIONS_COUNT];
	uint32_t numActions;
	uint32_t numFrees;
	bool corruptedLeading;
	bool corruptedTrailing;
} MemoryEntry;

static void MemoryEntryAddAction(MemoryEntry *entry, SourceLocation action) {
	if (entry->numActions < MEMORY_ENTRY_MAX_ACTIONS_COUNT) {
		entry->actions[entry->numActions] = action;
		entry->numActions++;
	}
	if (action.kind == ACTION_KIND_FREE) {
		entry->numFrees++;
	}
}

static int compareMemoryEntry_block(const void *_lhs, const void *_rhs) {
	const MemoryEntry *lhs = _lhs;
	const MemoryEntry *rhs = _rhs;
	uintptr_t lhsBegin = (uintptr_t)lhs->block;
	uintptr_t rhsBegin = (uintptr_t)rhs->block;
	if (lhsBegin < rhsBegin) {
		return -1;
	} else if (lhsBegin == rhsBegin) {
		return 0;
	} else {
		return 1;
	}
}

static int compareMemoryEntry_file_line(const void *_lhs, const void *_rhs) {
	const MemoryEntry *lhs = _lhs;
	const MemoryEntry *rhs = _rhs;
	int dir = strcmp(lhs->actions[0].filepath, rhs->actions[0].filepath);
	if (dir) {
		return dir;
	}
	if (lhs->actions[0].line < rhs->actions[0].line) {
		return -1;
	} else if (lhs->actions[0].line == rhs->actions[0].line) {
		return 0;
	} else {
		return 1;
	}
}

typedef struct MemoryEntries {
	MemoryEntry *data;
	uint32_t count;
	uint32_t capacity;
} MemoryEntries;

static MemoryEntries entries;

static bool IsBadEntry(MemoryEntry *entry) {
	return entry->corruptedLeading || entry->corruptedTrailing || entry->numFrees != 1 || entry->actions[0].kind == ACTION_KIND_FREE;
}

static bool CheckMemoryEntry(MemoryEntry *entry) {
	uint8_t *check = entry->base;
	uint8_t *end = entry->block;
	while (check < end) {
		if (*check != 0xCD) {
			entry->corruptedLeading = true;
		}
		check++;
	}
	check = (uint8_t*)entry->block + entry->size;
	end = (uint8_t*)entry->block + (entry->size * 2);
	while (check < end) {
		if (*check != 0xCD) {
			entry->corruptedTrailing = true;
		}
		check++;
	}
	return IsBadEntry(entry);
}

static const char* FilenameFromFilepath(const char *filepath) {
	const char *it = filepath;
	while (*it) {
		if (*it == '\\' || *it == '/') {
			filepath = it + 1;
		}
		++it;
	}
	return filepath;
}

static void PrintBadEntries(FILE *file, bool listAllBadBlocks) {
	uint32_t numBadEntries = 0;
	for (uint32_t i = 0; i < entries.count; i++) {
		MemoryEntry *entry = &entries.data[i];
		if (CheckMemoryEntry(entry)) {
			numBadEntries++;
		}
	}
	if (numBadEntries == 0) {
		fprintf(file, "There are no detected memory errors :)\n");
		return;
	}
	fprintf(file, "There are %u detected memory errors:\n", numBadEntries);
	// Sort by file and line so we can print them in a convenient order
	qsort(entries.data, entries.count, sizeof(*entries.data), compareMemoryEntry_file_line);
	const char *filepath = "";
	int line = -1;
	for (uint32_t i = 0; i < entries.count; i++) {
		MemoryEntry *entry = &entries.data[i];
		if (!IsBadEntry(entry)) {
			// We didn't detect an error for this entry
			continue;
		}
		if (!listAllBadBlocks) {
			if (strcmp(entry->actions[0].filepath, filepath) != 0) {
				filepath = entry->actions[0].filepath;
				uint32_t numBadEntriesInFile = 0;
				for (uint32_t j = i; j < entries.count; j++) {
					if (IsBadEntry(&entries.data[j])) {
						numBadEntriesInFile++;
					}
					if (strcmp(entries.data[j].actions[0].filepath, filepath) != 0) {
						break;
					}
				}
				fprintf(file, "\t\"%s\" has %u errors:\n", FilenameFromFilepath(filepath), numBadEntriesInFile);
				line = -1;
			}
			if (entry->actions[0].line != line) {
				line = entry->actions[0].line;
				uint32_t numBadEntriesOnLine = 0;
				for (uint32_t j = i; j < entries.count; j++) {
					if (IsBadEntry(&entries.data[j])) {
						numBadEntriesOnLine++;
					}
					if (entries.data[j].actions[0].line != line) {
						break;
					}
				}
				fprintf(file, "\t\tLine %d has %u errors\n", line, numBadEntriesOnLine);
			}
		} else {
			fprintf(file, "\t0x%zx bytes", entry->size);
			if (entry->corruptedLeading) {
				fprintf(file, " \tcorrupted leading bytes");
			}
			if (entry->corruptedTrailing) {
				fprintf(file, " \tcorrupted trailing bytes");
			}
			if (entry->numFrees > 1) {
				fprintf(file, " \tfree'd %d times", entry->numFrees);
			}
			if (entry->numFrees == 0) {
				fprintf(file, " \tnever freed");
			}
			fprintf(file, " \tactions: [ ");
			for (uint32_t j = 0; j < entry->numActions; j++) {
				if (j > 0) {
					fprintf(file, ", ");
				}
				fprintf(file, "%s'd %s:%d", ActionKindStrings[(int)entry->actions[j].kind], FilenameFromFilepath(entry->actions[j].filepath), entry->actions[j].line);
			}
			fprintf(file, " ]\n");
		}
	}
	// Put them back into memory-block order, because we might keep going
	qsort(entries.data, entries.count, sizeof(*entries.data), compareMemoryEntry_block);
}

typedef struct MemoryStatistics {
	// Includes realloc calls without an existing block
	uint64_t numAllocs;
	// Excludes realloc calls without an existing block
	uint64_t numReallocs;
	uint64_t numFrees;
	uint64_t callsToCalloc;
	uint64_t callsToMalloc;
	uint64_t callsToRealloc;
	uint64_t callsToFree;
	uint64_t bytesInUse;
	uint64_t maxBytesInUse;
	// Total number of bytes allocated summed up
	// Includes worst-case realloc numbers as well
	uint64_t bytesChurnedWorstCase;
	// Total number of bytes allocated summed up
	// Includes best-case realloc numbers as well
	uint64_t bytesChurnedBestCase;
} MemoryStatistics;


static MemoryStatistics statistics;

static void PrintStatistics(FILE *file) {
	fprintf(file, "numAllocs: %llu\n", statistics.numAllocs);
	fprintf(file, "numReallocs: %llu\n", statistics.numReallocs);
	fprintf(file, "numFrees: %llu\n", statistics.numFrees);
	fprintf(file, "leaked: %llu\n", statistics.numAllocs - statistics.numFrees);
	fprintf(file, "callsToCalloc: %llu\n", statistics.callsToCalloc);
	fprintf(file, "callsToMalloc: %llu\n", statistics.callsToMalloc);
	fprintf(file, "callsToRealloc: %llu\n", statistics.callsToRealloc);
	fprintf(file, "callsToFree: %llu\n", statistics.callsToFree);
	fprintf(file, "bytesInUse: 0x%llx\n", statistics.bytesInUse);
	fprintf(file, "maxBytesInUse: 0x%llx\n", statistics.maxBytesInUse);
	fprintf(file, "bytesChurnedWorstCase: 0x%llx\n", statistics.bytesChurnedWorstCase);
	fprintf(file, "bytesChurnedBestCase: 0x%llx\n", statistics.bytesChurnedBestCase);
}

static size_t GetMemoryEntryInsertIndex(MemoryEntries *ent, MemoryEntry entry) {
	if (ent->count) {
		int64_t left = 0;
		int64_t right = ent->count-1;
		int64_t middle = 0;
		uintptr_t target = (uintptr_t)entry.block;
		while (left <= right) {
			middle = left + (right - left) / 2;
			uintptr_t test = (uintptr_t)ent->data[middle].block;
			if (target > test) {
				left = middle + 1;
			} else if (target < test) {
				right = middle - 1;
			} else {
				assert(false && "Entry already exists!");
				return middle;
			}
		}
		return left; // Found a gap
	} else {
		return 0;
	}
}

static MemoryEntry* GetMemoryEntry(MemoryEntries *ent, void *block) {
	MemoryEntry *result = NULL;
	if (ent->count) {
		int64_t left = 0;
		int64_t right = ent->count-1;
		uintptr_t target = (uintptr_t)block;
		while (left <= right) {
			int64_t middle = left + (right - left) / 2;
			uintptr_t test = (uintptr_t)ent->data[middle].block;
			if (target > test) {
				left = middle + 1;
			} else if (target < test) {
				right = middle - 1;
			} else {
				result = &ent->data[middle];
				break;
			}
		}
	}
	return result;
}

static void AddMemoryEntry(MemoryEntries *ent, MemoryEntry entry) {
	size_t index = GetMemoryEntryInsertIndex(ent, entry);
	AZA_DA_INSERT(*ent, index, entry, assert(false && "Memory Debugger Alloc Failed D:"));
}

static void RemoveMemoryEntry(MemoryEntries *ent, MemoryEntry *entry) {
	size_t index = entry - ent->data;
	assert(index < ent->count);
	AZA_DA_ERASE(*ent, index, 1);
}



void azaMemoryDebuggerInit() {
	azaMutexInit(&mutex);
	entries = (MemoryEntries) {0};
	statistics = (MemoryStatistics) {0};
}

void azaMemoryDebuggerDeinit() {
	azaMutexDeinit(&mutex);
	AZA_DA_DEINIT(entries);
}

void azaMemoryDebuggerReport(const char *filepath, bool console, bool listAllBadBlocks) {
	// Print statistics
	if (console) {
		PrintStatistics(stdout);
		PrintBadEntries(stdout, listAllBadBlocks);
	}
	if (filepath) {
		FILE *file = fopen(filepath, "w");
		if (file) {
			PrintStatistics(file);
			PrintBadEntries(file, listAllBadBlocks);
			fclose(file);
		} else {
			AZA_LOG_ERR("azaMemoryDebuggerReport: Unable to open file for writing \"%s\"\n", filepath);
		}
	}
}



static void* Allocate(size_t size, bool zero, SourceLocation action) {
	statistics.numAllocs++;
	statistics.bytesInUse += size;
	statistics.maxBytesInUse = AZA_MAX(statistics.bytesInUse, statistics.maxBytesInUse);
	statistics.bytesChurnedWorstCase += size;
	statistics.bytesChurnedBestCase += size;
	size_t alignedSize = aza_align(size, 8);
	size_t totalSize = alignedSize + size * 2;
	void *base = zero ? azaAllocator.fp_calloc(1, totalSize) : azaAllocator.fp_malloc(totalSize);
	memset(base, 0xCD, alignedSize);
	void *result = (char*)base + alignedSize;
	memset((char*)result + size, 0xCD, size);
	MemoryEntry entry = {
		.base = base,
		.block = result,
		.size = size,
		.actions = { action },
		.numActions = 1,
		.numFrees = 0,
	};
	AddMemoryEntry(&entries, entry);
	return result;
}

void* aza_calloc_debug(const char *filepath, int line, size_t count, size_t size) {
	azaMutexLock(&mutex);
	statistics.callsToCalloc++;
	void *result = Allocate(count * size, true, (SourceLocation) {
		.filepath = filepath,
		.line = line,
		.kind = ACTION_KIND_CALLOC
	});
	azaMutexUnlock(&mutex);
	return result;
}

void* aza_malloc_debug(const char *filepath, int line, size_t size) {
	azaMutexLock(&mutex);
	statistics.callsToMalloc++;
	void *result = Allocate(size, false, (SourceLocation) {
		.filepath = filepath,
		.line = line,
		.kind = ACTION_KIND_MALLOC
	});
	azaMutexUnlock(&mutex);
	return result;
}

void* aza_realloc_debug(const char *filepath, int line, void *block, size_t size) {
	void *result = block;
	azaMutexLock(&mutex);
	statistics.callsToRealloc++;
	if (block) {
		statistics.numReallocs++;
		MemoryEntry *entry = GetMemoryEntry(&entries, block);
		assert(entry && "Bad Realloc Block");
		MemoryEntryAddAction(entry, (SourceLocation) {
			.filepath = filepath,
			.line = line,
			.kind = ACTION_KIND_REALLOC,
		});
		if (size > entry->size) {
			entry->numFrees++; // Since this would be free'd normally, we just keep it around for full error checking.
			statistics.bytesInUse -= entry->size;
			statistics.bytesChurnedBestCase -= entry->size;
			statistics.numAllocs--; // Compensate for Allocate adding one
			result = Allocate(size, false, (SourceLocation) {
				.filepath = filepath,
				.line = line,
				.kind = ACTION_KIND_REALLOC,
			});
			memcpy(result, block, entry->size);
		}
	} else {
		result = Allocate(size, false, (SourceLocation) {
			.filepath = filepath,
			.line = line,
			.kind = ACTION_KIND_REALLOC,
		});
	}
	azaMutexUnlock(&mutex);
	return result;
}

void aza_free_debug(const char *filepath, int line, void *block) {
	azaMutexLock(&mutex);
	statistics.callsToFree++;
	statistics.numFrees++;
	MemoryEntry *entry = GetMemoryEntry(&entries, block);
	if (entry) {
		statistics.bytesInUse -= entry->size;
		MemoryEntryAddAction(entry, (SourceLocation) {
			.filepath = filepath,
			.line = line,
			.kind = ACTION_KIND_FREE,
		});
	} else {
		AddMemoryEntry(&entries, (MemoryEntry) {
			.block = block,
			.actions = {
				(SourceLocation) { .filepath = filepath, .line = line, .kind = ACTION_KIND_FREE },
			},
			.numActions = 1,
			.numFrees = 1,
		});
	}
	azaMutexUnlock(&mutex);
}

