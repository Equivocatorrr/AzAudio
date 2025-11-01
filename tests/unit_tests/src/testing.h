/*
	File: testing.h
	Author: Philip Haynes
	Core testing framework for managing and reporting issues.
*/

#include <AzAudio/aza_c_std.h>

#ifndef UT_TESTING_H
#define UT_TESTING_H

enum utStatus {
	UT_SUCCESS=0,
	UT_INFO = 0,
	UT_WEAK = 1,
	UT_FAIL = 2,
	UT_ENUM_COUNT
};

extern const char *utReportKindString[UT_ENUM_COUNT];
extern const char *utTestResultString[UT_ENUM_COUNT];

// Results for one line in a test
typedef struct utReport_t {
	enum utStatus kind;
	uint32_t line;
	uint64_t order; // We get sorted by line first, and then order. Kind of a hack for a lack of a stable sort. This gets set by utSubmitReport
	char *explanation; // malloc the string and leak it to piss off the purists
	const char *subtest; // Don't bother setting this, utSubmitReport will do that (overwriting your thing anyway)
} utReport_t;

// Results for a single test
typedef struct utTest_t {
	const char *name;
	utReport_t *data;
	uint32_t count;
	uint32_t capacity;
	enum utStatus result;
} utTest_t;

// Results of all unit tests combined
typedef struct utAllTests_t {
	utTest_t *data;
	uint32_t count;
	uint32_t capacity;
} utAllTests_t;

extern utAllTests_t utAllTests;

extern char utSprintfBuffer[1024];

// Call once at the beginning of the test
void utBeginTest(const char *name);
// Call once at the end of the test
void utEndTest();

void utBeginSubtest(const char *name);
void utEndSubtest();

void utSubmitReport(utReport_t report);

// Call before reading reports. Sorts them by line number.
void utSortReports();

#define UT_MALLOC_SPRINTF(...) \
	int _ut_printed = snprintf(utSprintfBuffer, sizeof(utSprintfBuffer), __VA_ARGS__);\
	char *_ut_newBuffer = malloc(_ut_printed+1);\
	aza_strcpy(_ut_newBuffer, utSprintfBuffer, _ut_printed+1);

#define UT_SUBMIT(reportKind, ...) {\
	UT_MALLOC_SPRINTF(__VA_ARGS__)\
	utSubmitReport((utReport_t) {\
		.kind = (reportKind),\
		.line = __LINE__,\
		.explanation = _ut_newBuffer,\
	});\
}

#define UT_SUBMIT_FAIL(...) UT_SUBMIT(UT_FAIL, __VA_ARGS__)
#define UT_SUBMIT_WEAK(...) UT_SUBMIT(UT_WEAK, __VA_ARGS__)
#define UT_SUBMIT_INFO(...) UT_SUBMIT(UT_INFO, __VA_ARGS__)

#define UT_EXPECT_EQUAL(reportKind, value1, value2, extrafmt, ...)\
	if ((value1) != (value2)) {\
		UT_SUBMIT((reportKind), "Expected " #value1 " == " #value2 " \t" extrafmt, __VA_ARGS__);\
	}

#endif // UT_TESTING_H