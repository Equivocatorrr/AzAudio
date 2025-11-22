/*
	File: backtrace.c
	Author: Philip Haynes
*/

#include "../backtrace.h"
#include "../threads.h"

#include <stdio.h>

#include <windows.h>
#include <winnt.h>
#include <imagehlp.h>
#include <malloc.h>
#include <psapi.h>
#include <stdbool.h>

void* _GetImageBasePointer(HANDLE process, DWORD pid) {
	HMODULE module;
	DWORD dummy;
	if (!EnumProcessModules(process, &module, sizeof(HMODULE), &dummy)) {
		fprintf(stderr, "Failed to EnumProcessModules: %lu\n", GetLastError());
		return NULL;
	}
	MODULEINFO moduleInfo;
	if (!GetModuleInformation(process, module, &moduleInfo, sizeof(MODULEINFO))) {
		fprintf(stderr, "Failed to GetModuleInformation: %lu\n", GetLastError());
		return NULL;
	}
	return moduleInfo.lpBaseOfDll;
}

STACKFRAME64 _GetStackFrame(const CONTEXT *context) {
	STACKFRAME64 stackFrame = {0};
	stackFrame.AddrPC.Offset = context->Rip;
	stackFrame.AddrPC.Mode = AddrModeFlat;
	stackFrame.AddrStack.Offset = context->Rsp;
	stackFrame.AddrStack.Mode = AddrModeFlat;
	stackFrame.AddrFrame.Offset = context->Rbp;
	stackFrame.AddrFrame.Mode = AddrModeFlat;
	return stackFrame;
}

void _PrintSymbolName(char *buffer, DWORD bufferSize, HANDLE hProcess, DWORD64 offset) {
	IMAGEHLP_SYMBOL64 *symbol;
	size_t sizeofSymbol = sizeof(IMAGEHLP_SYMBOL64) + 1024;
	symbol = (IMAGEHLP_SYMBOL64*)alloca(sizeofSymbol);
	memset(symbol, 0, sizeofSymbol);
	symbol->SizeOfStruct = sizeof(IMAGEHLP_SYMBOL64);
	symbol->MaxNameLength = 1024;
	DWORD64 displacement;
	if (!SymGetSymFromAddr64(hProcess, offset, &displacement, symbol)) {
		snprintf(buffer, bufferSize, "<symbol name error %lu>", GetLastError());
	} else {
		UnDecorateSymbolName(symbol->Name, buffer, bufferSize, UNDNAME_COMPLETE);
	}
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

static HANDLE bt_hProcess = NULL;
static IMAGE_NT_HEADERS *bt_ntHeaders = NULL;
static azaMutex bt_mutex;

void azaBacktraceInit() {
	azaMutexInit(&bt_mutex);
	bt_hProcess = GetCurrentProcess();
	DWORD pid = GetCurrentProcessId();
	void *imageBasePointer = _GetImageBasePointer(bt_hProcess, pid);
	bt_ntHeaders = ImageNtHeader(imageBasePointer);
	if (!SymInitialize(bt_hProcess, NULL, true)) {
		assert(false);
		return;
	}
	SymSetOptions(SymGetOptions() | SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);
}

void azaBacktraceDeinit() {
	SymCleanup(bt_hProcess);
	azaMutexDeinit(&bt_mutex);
}

azaBacktrace azaBacktraceGet(int skipFrames) {
	skipFrames++; // Because we want to skip azaBacktraceGet
	azaBacktrace result = { 0 };
	assert(bt_hProcess != NULL);
	assert(bt_ntHeaders != NULL);
	azaMutexLock(&bt_mutex);
	HANDLE hThread = GetCurrentThread();
	CONTEXT context;
	RtlCaptureContext(&context);

	STACKFRAME64 stackFrame = _GetStackFrame(&context);
	IMAGEHLP_LINE64 line = {0};
	line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);

	while (StackWalk64(bt_ntHeaders->FileHeader.Machine, bt_hProcess, hThread, &stackFrame, &context, NULL, SymFunctionTableAccess64, SymGetModuleBase64, NULL)) {
		if (skipFrames) {
			skipFrames--;
			continue;
		}
		if (result.count >= AZA_BACKTRACE_MAX_FRAMES) {
			break;
		}
		azaBacktraceFrame *frame = &result.frames[result.count++];
		if (stackFrame.AddrPC.Offset != 0) {
			_PrintSymbolName(frame->functionName, sizeof(frame->functionName), bt_hProcess, stackFrame.AddrPC.Offset);
			DWORD displacement;
			if (SymGetLineFromAddr64(bt_hProcess, stackFrame.AddrPC.Offset, &displacement, &line)) {
				strncat(frame->functionName, "[", sizeof(frame->functionName));
				strncat(frame->functionName, FilenameFromFilepath(line.FileName), sizeof(frame->functionName));
				strncat(frame->functionName, ":", sizeof(frame->functionName));
				char lineNumber[16];
				snprintf(lineNumber, sizeof(lineNumber), "%lu", line.LineNumber);
				strncat(frame->functionName, lineNumber, sizeof(frame->functionName));
				strncat(frame->functionName, "]", sizeof(frame->functionName));
			}
			if (aza_str_begins_with(frame->functionName, "invoke_main") || aza_str_begins_with(frame->functionName, "register_onexit_function")) {
				// Don't include windows runtime frames
				result.count--; // We've already gone 1 too far
				break;
			}
		} else {
			break;
		}
	}
	azaMutexUnlock(&bt_mutex);
	return result;
}


