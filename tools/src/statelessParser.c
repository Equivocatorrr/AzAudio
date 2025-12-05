/*
	File: statelessParser.c
	Author: Philip Haynes
	Simple character-based stateless parser generator.
*/

#define _CRT_SECURE_NO_WARNINGS // getting real sick of your shit

#include <assert.h>
#include <ctype.h>
#include <malloc.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

typedef struct Trie {
	struct Trie *pNext[256]; // pNext[0] being non-null means this node is the last node in a valid word
	int count; // How many values in the pNext array have values (excluding index 0 as it's a special case)
} Trie;

bool charIsBreak(char c) {
	return c == 0 || c == ' ' || c == '\n' || c == '\t';
}

size_t wordlen(const char *word) {
	size_t len = 0;
	while (!charIsBreak(word[len])) {
		len++;
	}
	return len;
}

void reverseWord(char *word) {
	size_t count = wordlen(word);
	for (size_t i = 0; i < count/2; i++) {
		char c = word[i];
		word[i] = word[count-i-1];
		word[count-i-1] = c;
	}
}

void AddToTrie(Trie *trie, const char *str) {
	while (true) {
		if (charIsBreak(*str)) {
			trie->pNext[0] = (Trie*)1;
			break;
		}
		uint8_t index = *str;
		if (!trie->pNext[index]) {
			trie->pNext[index] = calloc(1, sizeof(Trie));
			trie->count++;
		}
		trie = trie->pNext[index];
		str++;
	}
}

char* fread_word(FILE *file) {
	static size_t bufferCount = 0;
	static size_t bufferOffset = 0;
	static char buffer[2048];
	static const size_t bufferCapacity = sizeof(buffer)/2;
	if (bufferCount < bufferCapacity) {
		if (bufferCount && bufferOffset) {
			memmove(buffer, buffer + bufferOffset, bufferCount);
			bufferOffset = 0;
		}
		size_t num_read = fread(buffer + bufferCount, 1, bufferCapacity, file);
		bufferCount += num_read;
	}
	while (bufferCount && charIsBreak(buffer[bufferOffset])) {
		bufferOffset++;
		bufferCount--;
	}
	if (bufferCount == 0) {
		return NULL;
	}
	char *result = buffer + bufferOffset;
	while (bufferCount && !charIsBreak(buffer[bufferOffset])) {
		buffer[bufferOffset] = tolower(buffer[bufferOffset]);
		bufferOffset++;
		bufferCount--;
	}
	return result;
}

const char* pathRemoveCommon(const char *path, const char *comparison) {
	const char *result = path;
	while (*path == *comparison) {
		if (*path == '/') {
			result = path+1;
		}
		path++;
		comparison++;
	}
	return result;
}

const char* pathGetFilename(const char *path) {
	const char *result = path;
	while (*path) {
		if (*path == '/') {
			result = path+1;
		}
		path++;
	}
	return result;
}

void pathToForwardSlashes(char *path) {
	while (*path) {
		if (*path == '\\') {
			*path = '/';
		}
		path++;
	}
}

const char* charString(int c) {
	assert(c < 256);
	static char buffer[16];
	uint8_t code = c;
	if (code >= 128) {
		snprintf(buffer, sizeof(buffer), "(char)%hhu", code);
	} else {
		switch (c) {
			case '\a': return "'\\a'";
			case '\b': return "'\\b'";
			case '\f': return "'\\f'";
			case '\n': return "'\\n'";
			case '\r': return "'\\r'";
			case '\t': return "'\\t'";
			case '\v': return "'\\v'";
			case '\\': return "'\\\\'";
			case '\'': return "'\\''";
			case '\"': return "'\"'";
			case '\?': return "'\\?'";
			default: {
				snprintf(buffer, sizeof(buffer), "'%c'", (int)c);
				break;
			}
		}
	}
	return buffer;
}

void WriteTrieNode(FILE *file, Trie *trie, int depth, int numTabs, bool reverse) {
	char *tabs = alloca(numTabs+1);
	for (int i = 0; i < numTabs; i++) {
		tabs[i] = '\t';
	}
	tabs[numTabs] = 0;
	char metric[32];
	char offset[32];
	if (reverse) {
		snprintf(metric, sizeof(metric), "-%d >= minRange", depth+1);
		snprintf(offset, sizeof(offset), "-%d", depth+1);
	} else {
		snprintf(metric, sizeof(metric), "%d < maxRange", depth);
		snprintf(offset, sizeof(offset), "+%d", depth);
	}
	if (trie->pNext[0]) {
		fprintf(file, "%sif (!(%s) || !charIsAlpha(*(text%s))) {\n", tabs, metric, offset);
		fprintf(file, "%s\treturn true;\n", tabs);
		fprintf(file, "%s}\n", tabs);
	}
	if (trie->count) {
		fprintf(file, "%sif (%s) {\n", tabs, metric);
			if (trie->count == 1) {
				for (int i = 1; i < 256; i++) {
					if (trie->pNext[i]) {
						fprintf(file, "%s\tif (tolower(*(text%s)) == %s) {\n", tabs, offset, charString(i));
						WriteTrieNode(file, trie->pNext[i], depth+1, numTabs+2, reverse);
						fprintf(file, "%s\t}\n", tabs);
						break;
					}
				}
			} else {
				fprintf(file, "%s\tswitch(tolower(*(text%s))) {\n", tabs, offset);
				for (int i = 1; i < 256; i++) {
					if (trie->pNext[i]) {
						fprintf(file, "%s\t\tcase %s:\n", tabs, charString(i));
							WriteTrieNode(file, trie->pNext[i], depth+1, numTabs+3, reverse);
						fprintf(file, "%s\t\t\tbreak;\n", tabs);
					}
				}
				fprintf(file, "%s\t\tdefault: break;\n", tabs);
				fprintf(file, "%s\t}\n", tabs);
			}
		fprintf(file, "%s}\n", tabs);
	}
}

void usage(const char *name) {
	printf("Usage: %s --(forward|reverse) /path/to/list -o /path/to/output.c functionName\n", name);
}

int main(int argc, char *argv[]) {
	if (argc < 6 || strcmp(argv[3], "-o") != 0 || (strcmp(argv[1], "--forward") != 0 && strcmp(argv[1], "--reverse") != 0)) {
		usage(argv[0]);
		return 0;
	}
	bool reverse = strcmp(argv[1], "--reverse") == 0;
	char *input = argv[2];
	char *output = argv[4];
	const char *functionName = argv[5];
	FILE *fileInput = fopen(input, "r");
	if (!fileInput) {
		fprintf(stderr, "Failed to open \"%s\" for reading: %s", input, strerror(errno));
		return 1;
	}
	FILE *fileOutput = fopen(output, "w");
	if (!fileOutput) {
		fprintf(stderr, "Failed to open \"%s\" for writing: %s", output, strerror(errno));
		fclose(fileInput);
		return 1;
	}
	pathToForwardSlashes(argv[0]);
	pathToForwardSlashes(input);
	pathToForwardSlashes(output);

	Trie trie = {0};
	// Construct the trie from the input
	for (char *word = fread_word(fileInput); word != NULL; word = fread_word(fileInput)) {
		if (reverse) {
			reverseWord(word);
		}
		AddToTrie(&trie, word);
	}
	fclose(fileInput);

	fprintf(fileOutput,
"/*\n\
	File: %s\n\
	This file was generated from \"%s\" by \"%s\" and should not be directly edited or checked into source control.\n\
*/\n\
\n\
#include <stdbool.h>\n\
#include <ctype.h> // tolower\n\
\n\
#ifndef AZAUDIO_CHAR_IS_ALPHA_FUNCTION\n\
#define AZAUDIO_CHAR_IS_ALPHA_FUNCTION\n\
static bool charIsAlpha(char c) {\n\
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');\n\
}\n\
#endif // AZAUDIO_CHAR_IS_ALPHA_FUNCTION\n\
\n\
bool %s(const char *text, int minRange, int maxRange) {\n\
",
	pathGetFilename(output), pathRemoveCommon(input, output), argv[0], functionName);

	// Use trie to construct parser
	WriteTrieNode(fileOutput, &trie, 0, 1, reverse);

	fprintf(fileOutput, 	"\
	return false;\n\
}\n\
"
	);

	fclose(fileOutput);
	return 0;
}
