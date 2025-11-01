/*
	File: testing.c
	Author: Philip Haynes
*/

#include "testing.h"

const char *utTestResultString[UT_ENUM_COUNT] = {
	"SUCCESS",
	"WEAK",
	"FAIL",
};

const char *utReportKindString[UT_ENUM_COUNT] = {
	"INFO",
	"WEAK",
	"FAIL",
};

utAllTests_t utAllTests = {0};

char utSprintfBuffer[1024];

static utTest_t *currentTest = NULL;
static const char *currentSubtestName = NULL;

void utBeginTest(const char *name) {
	AZA_DA_APPEND(utAllTests, (utTest_t){0}, assert(false && "allocfail"));
	currentTest = &utAllTests.data[utAllTests.count-1];
	currentTest->name = name;
	currentSubtestName = NULL;
}

void utEndTest() {
	currentTest = NULL;
}

void utBeginSubtest(const char *name) {
	currentSubtestName = name;
}

void utEndSubtest() {
	currentSubtestName = NULL;
}

static uint64_t order = 0;

void utSubmitReport(utReport_t report) {
	assert(currentTest && "You forgot to call utBeginTest, didn't you?");
	report.subtest = currentSubtestName;
	report.order = order++;
	if ((uint32_t)report.kind > (uint32_t)currentTest->result) {
		currentTest->result = report.kind;
	}
	AZA_DA_APPEND(*currentTest, report, assert(false && "allocfail"));
}

static int utReportCompare(const void *_lhs, const void *_rhs) {
	const utReport_t *lhs = _lhs;
	const utReport_t *rhs = _rhs;
	if (lhs->line < rhs->line) {
		return -1;
	} else if (lhs->line > rhs->line) {
		return 1;
	} else {
		return lhs->order < rhs->order ? -1 : 1;
	}
}

void utSortReports() {
	for (uint32_t i = 0; i < utAllTests.count; i++) {
		utTest_t *results = &utAllTests.data[i];
		qsort(results->data, results->count, sizeof(*results->data), utReportCompare);
	}
}