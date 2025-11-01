/*
	File: main.c
	Author: singularity
	Program for testing individual components.
*/

#include <stdio.h>

#include <AzAudio/AzAudio.h>

#include "testing.h"
#include "vt_strings.h"

const char *utTestResultVTColor[UT_ENUM_COUNT] = {
	VT_FG_GREEN,
	VT_FG_YELLOW,
	VT_FG_RED,
};

const char *utReportKindVTColor[UT_ENUM_COUNT] = {
	VT_FG_BLUE,
	VT_FG_YELLOW,
	VT_FG_RED,
};



// All the tests



void runAllTests() {
	void ut_run_azaBufferResize();
	ut_run_azaBufferResize();
}



void usage(const char *executableName) {
	printf(
		"Usage:\n"
		"%s                      Run all of the unit tests, reporting only FAIL and WEAK\n"
		"%s --help               Display this help\n"
		"%s --print-reports      Run all of the unit tests and print all of the reports\n"
	, executableName, executableName, executableName);
}

int main(int argumentCount, char** argumentValues) {
	bool printReports = false;
	if (argumentCount >= 2) {
		if (strcmp(argumentValues[1], "--help") == 0) {
			usage(argumentValues[0]);
			return 0;
		} else if (strcmp(argumentValues[1], "--print-reports") == 0) {
			printReports = true;
		}
	}

	azaLogLevel = AZA_LOG_LEVEL_ERROR;

	int err = azaInit();
	if (err) {
		return 1;
	}

	runAllTests();

	utSortReports();

	uint32_t numResultKinds[UT_ENUM_COUNT] = {0};
	const uint32_t numTests = utAllTests.count;
	for (uint32_t testIndex = 0; testIndex < utAllTests.count; testIndex++) {
		utTest_t *test = &utAllTests.data[testIndex];
		assert(test->result < UT_ENUM_COUNT);
		numResultKinds[test->result]++;
		if (printReports) {
			printf("\"%s\"\n   Result: %s%s" VT_RESET "\n", test->name, utTestResultVTColor[test->result], utTestResultString[test->result]);
			uint32_t currentLine = 0;
			for (uint32_t reportIndex = 0; reportIndex < test->count; reportIndex++) {
				utReport_t *report = &test->data[reportIndex];
				if (report->line > currentLine) {
					if (report->subtest) {
						printf("   Subtest \"%s\" on line %u:\n", report->subtest, report->line);
					} else {
						printf("   On line %u:\n", report->line);
					}
					currentLine = report->line;
				}
				printf("      %s%s: %s" VT_RESET "\n", utReportKindVTColor[report->kind], utReportKindString[report->kind], report->explanation);
			}
		}
	}

	printf("Out of %u tests, %u succeeded, %u were weak, and %u failed.\n", numTests, numResultKinds[UT_SUCCESS], numResultKinds[UT_WEAK], numResultKinds[UT_FAIL]);

	if (numResultKinds[UT_WEAK]) {
		printf(VT_SPAN(VT_FG_YELLOW, "WEAKs:") "\n");
		for (uint32_t testIndex = 0; testIndex < utAllTests.count; testIndex++) {
			utTest_t *test = &utAllTests.data[testIndex];
			if (test->result != UT_WEAK) continue;
			printf("\t%s\n", test->name);
		}
	}

	if (numResultKinds[UT_FAIL]) {
		printf(VT_SPAN(VT_FG_RED, "FAILs:") "\n");
		for (uint32_t testIndex = 0; testIndex < utAllTests.count; testIndex++) {
			utTest_t *test = &utAllTests.data[testIndex];
			if (test->result != UT_FAIL) continue;
			printf("\t%s\n", test->name);
		}
	}

	azaDeinit();
	return 0;
}
