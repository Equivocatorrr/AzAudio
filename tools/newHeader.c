#define _CRT_SECURE_NO_WARNINGS

#include <stdio.h> // printf
#include <string.h> // strncat
#include <ctype.h> // toupper
#include <stdbool.h>
#include <assert.h>

#define MIN(a, b) ((a) < (b) ? (a) : (b))

void usage(const char *executableName) {
	printf(
		"Usage:\n"
		"%s path/to/file.h (--author \"Your Name\")? (--output-c)? (--replace)? (--replace-h)? (--replace-c)?"
	, executableName);
}

int main(int argc, char** argv) {
	char dstPath[256] = {0};
	char dstCPath[256] = {0};
	bool replaceH = false;
	bool replaceC = false;
	bool outputCFile = false;
	size_t dstPathExtStart = 0;
	char *author = "Bingle McJingle";

	for (int i = 1; i < argc; i++) {
		if (argv[i][0] == '-') {
			if (strcmp(argv[i], "--replace") == 0) {
				replaceH = true;
				replaceC = true;
			} else if (strcmp(argv[i], "--replace-h") == 0) {
				replaceH = true;
			} else if (strcmp(argv[i], "--replace-c") == 0) {
				replaceC = true;
			} else if (strcmp(argv[i], "--author") == 0) {
				if (argc+1 == i) {
					fprintf(stderr, "Expected another argument for `--author`, but there was none.\n");
					usage(argv[0]);
					return 0;
				}
				author = argv[++i];
			} else if (strcmp(argv[i], "--output-c") == 0) {
				outputCFile = true;
			} else {
				usage(argv[0]);
				return 0;
			}
		} else {
			if (dstPath[0]) {
				fprintf(stderr, "Unexpected argument \"%s\", as we already have a path\n", argv[i]);
				return 1;
			}
			size_t len = strlen(argv[i]);
			if (len >= sizeof(dstPath)) {
				fprintf(stderr, "dstPath would overflow, your paths are too powerful :(\n");
				return 1;
			}
			strncpy(dstPath, argv[i], sizeof(dstPath)-1);
			dstPathExtStart = len;
			while (dstPathExtStart > 0) {
				dstPathExtStart--;
				if (dstPath[dstPathExtStart] == '.') break;
			}
			if (!dstPathExtStart) {
				// Add extension automagically
				if (len+2 >= sizeof(dstPath)) {
					fprintf(stderr, "dstPath would overflow, your paths are too powerful :(\n");
					return 1;
				}
				strncat(dstPath, ".h", 2);
				dstPathExtStart = len;
			}
		}
	}

	if (!dstPath[0]) {
		usage(argv[0]);
		return 0;
	}

	if (outputCFile) {
		// Copy path to dstCPath and change the extension
		strncpy(dstCPath, dstPath, sizeof(dstCPath)-1);
		dstCPath[dstPathExtStart+1] = 'c'; // Should work for .h and .hpp into .c and .cpp
	}

	char tag[256] = "AZAUDIO_";
	char *start = dstPath;
	char *end = start;
	size_t fileNameStart = 0;
	while (*start) {
		if (*end == '/' || *end == '\\') {
		// 	strncat(tag, start, MIN(sizeof(tag)-1, end-start));
		// 	strncat(tag, "_", 1);
			start = end+1;
		} else if (*end == 0 || *end == '.') {
			fileNameStart = start-dstPath;
			strncat(tag, start, MIN(sizeof(tag)-1, end-start));
			strncat(tag, "_", 1);
			break;
		}
		end++;
	}
	strncat(tag, "H", 1);
	for (int i = 0; tag[i] != 0; i++) {
		tag[i] = toupper(tag[i]);
	}

	FILE *file;
	if (!replaceH) {
		file = fopen(dstPath, "r");
		if (file) {
			fclose(file);
			fprintf(stderr, "File \"%s\" already exists! Use `--replace-h` to overwrite it.\n", dstPath);
			return 1;
		}
	}
	if (outputCFile && !replaceC) {
		file = fopen(dstCPath, "r");
		if (file) {
			fclose(file);
			fprintf(stderr, "File \"%s\" already exists! Use `--replace-c` to overwrite it.\n", dstCPath);
			return 1;
		}
	}
	file = fopen(dstPath, "w");
	if (!file) {
		fprintf(stderr, "Failed to open \"%s\" for writing\n", dstPath);
		return 1;
	}

	fprintf(file,
"/*\n\
	File: %s\n\
	Author: %s\n\
*/\n\
\n\
#ifndef %s\n\
#define %s\n\
\n\
#ifdef __cplusplus\n\
extern \"C\" {\n\
#endif\n\
\n\
// Do ya thang\n\
\n\
#ifdef __cplusplus\n\
}\n\
#endif\n\
\n\
#endif // %s\n\
",
		&dstPath[fileNameStart],
		author,
		tag, tag, tag
	);

	fclose(file);

	if (outputCFile) {
		file = fopen(dstCPath, "w");
		if (!file) {
			fprintf(stderr, "Failed to open \"%s\" for writing\n", dstPath);
			return 1;
		}

		fprintf(file,
"/*\n\
	File: %s\n\
	Author: %s\n\
*/\n\
\n\
#include \"%s\"\n\
\n\
// Do ya thang, implementation style\n\
",
			&dstCPath[fileNameStart],
			author,
			&dstPath[fileNameStart]
		);

		fclose(file);
	}


	return 0;
}