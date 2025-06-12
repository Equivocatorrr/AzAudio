/*
	File: main.c
	Author: singularity
	Program for profiling performance-critical functions.
*/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "AzAudio/AzAudio.h"
#include "AzAudio/backend/timer.h"

#define TEST_BUFFERS_FRAME_COUNT 1234
#define TEST_ITERATIONS 10000ull

void azaBufferDeinterlace_dynamic(azaBuffer dst, azaBuffer src);

void azaBufferDeinterlace2Ch_scalar(azaBuffer dst, azaBuffer src);
void azaBufferDeinterlace2Ch_sse(azaBuffer dst, azaBuffer src);
void azaBufferDeinterlace2Ch_avx2(azaBuffer dst, azaBuffer src);

void azaBufferDeinterlace3Ch_scalar(azaBuffer dst, azaBuffer src);
void azaBufferDeinterlace3Ch_sse(azaBuffer dst, azaBuffer src);
void azaBufferDeinterlace3Ch_sse4_1(azaBuffer dst, azaBuffer src);
void azaBufferDeinterlace3Ch_avx2(azaBuffer dst, azaBuffer src);

void azaBufferDeinterlace4Ch_scalar(azaBuffer dst, azaBuffer src);
void azaBufferDeinterlace4Ch_sse(azaBuffer dst, azaBuffer src);
void azaBufferDeinterlace4Ch_avx2(azaBuffer dst, azaBuffer src);

// For the purpose of testing the theoretical maximum throughput (this is by no means a realistic goal, but provides some context)
void azaBufferDeinterlace_memcpy(azaBuffer dst, azaBuffer src) {
	memcpy(dst.samples, src.samples, dst.frames * dst.channelLayout.count);
}

// Returns the average time per iteration in nanoseconds
int64_t TestDeinterlace(void(*fp_deinterlace)(azaBuffer,azaBuffer), uint8_t channelCount, const char *name) {
	azaBuffer interlaced, deinterlaced;
	azaBufferInit(&interlaced, TEST_BUFFERS_FRAME_COUNT, azaChannelLayoutStandardFromCount(channelCount));
	for (uint32_t i = 0; i < interlaced.frames; i++) {
		for (uint32_t c = 0; c < channelCount; c++) {
			interlaced.samples[i * interlaced.stride + c] = (float)(i + c * interlaced.frames);
		}
	}
	azaBufferInit(&deinterlaced, TEST_BUFFERS_FRAME_COUNT, azaChannelLayoutStandardFromCount(channelCount));

	int64_t start = azaGetTimestamp();

	for (uint32_t i = 0; i < TEST_ITERATIONS; i++) {
		fp_deinterlace(deinterlaced, interlaced);
	}

	int64_t end = azaGetTimestamp();
	int64_t nanoseconds = azaGetTimestampDeltaNanoseconds(end - start);

	bool error = false;
	for (uint32_t i = 0; i < deinterlaced.frames; i++) {
		for (uint32_t c = 0; c < channelCount; c++) {
			if (deinterlaced.samples[i + c * deinterlaced.frames] != (float)(i + c * deinterlaced.frames)) error = true;
		}
	}
	if (error) {
		printf("%s: \033[91mWRONGE\033[0m\n", name);
	} else {
		printf("%s: \033[92mCORRECTE\033[0m\n", name);
	}
	azaBufferDeinit(&interlaced);
	azaBufferDeinit(&deinterlaced);
	float gibps = 2.0f * (float)(((double)(TEST_ITERATIONS * TEST_BUFFERS_FRAME_COUNT * sizeof(float)) / (double)(1024 * 1024 * 1024)) * (double)channelCount / ((double)nanoseconds / 1000000000.0));
	float gbps = 2.0f * (float)(((double)(TEST_ITERATIONS * TEST_BUFFERS_FRAME_COUNT * sizeof(float)) / (double)(1000000000)) * (double)channelCount / ((double)nanoseconds / 1000000000.0));
	printf("took %6lld nanoseconds on average\nprocessed %f GiB/s (%f GB/s)\n\n", nanoseconds / TEST_ITERATIONS, gibps, gbps);
	return nanoseconds;
}

int main(int argumentCount, char** argumentValues) {
	int err = azaInit();
	if (err) {
		fprintf(stderr, "Failed to azaInit!\n");
		return 1;
	}

	{ // 2 channels
		printf("\n2 channel tests:\n\n");
		int64_t time_dynamic = TestDeinterlace(azaBufferDeinterlace_dynamic  , 2, " dynamic");
		int64_t time_scalar = TestDeinterlace(azaBufferDeinterlace2Ch_scalar, 2, "  scalar");
		int64_t time_sse = TestDeinterlace(azaBufferDeinterlace2Ch_sse      , 2, "     sse");
		int64_t time_avx2 = TestDeinterlace(azaBufferDeinterlace2Ch_avx2    , 2, "    avx2");
		int64_t time_main = TestDeinterlace(azaBufferDeinterlace            , 2, "main_api");
		TestDeinterlace(azaBufferDeinterlace_memcpy, 2, "  memcpy");
		printf("scalar was %.2f times speed of dynamic\n", (float)time_dynamic / (float)time_scalar);
		printf("sse    was %.2f times speed of scalar\n", (float)time_scalar / (float)time_sse);
		printf("avx2   was %.2f times speed of scalar\n", (float)time_scalar / (float)time_avx2);
		printf("avx2   was %.2f times speed of sse\n", (float)time_sse / (float)time_avx2);
		printf("main   was %.2f times speed of scalar\n", (float)time_scalar / (float)time_main);
		printf("main   was %.2f times speed of sse\n", (float)time_sse / (float)time_main);
	}
	{ // 3 channels
		printf("\n3 channel tests:\n\n");
		int64_t time_dynamic = TestDeinterlace(azaBufferDeinterlace_dynamic  , 3, " dynamic");
		int64_t time_scalar = TestDeinterlace(azaBufferDeinterlace3Ch_scalar, 3, "  scalar");
		int64_t time_sse = TestDeinterlace(azaBufferDeinterlace3Ch_sse      , 3, "     sse");
		int64_t time_sse4_1 = TestDeinterlace(azaBufferDeinterlace3Ch_sse4_1, 3, "  sse4.1");
		int64_t time_avx2 = TestDeinterlace(azaBufferDeinterlace3Ch_avx2    , 3, "    avx2");
		int64_t time_main = TestDeinterlace(azaBufferDeinterlace            , 3, "main_api");
		TestDeinterlace(azaBufferDeinterlace_memcpy, 3, "  memcpy");
		printf("scalar was %.2f times speed of dynamic\n", (float)time_dynamic / (float)time_scalar);
		printf("sse    was %.2f times speed of scalar\n", (float)time_scalar / (float)time_sse);
		printf("sse4.1 was %.2f times speed of scalar\n", (float)time_scalar / (float)time_sse4_1);
		printf("sse4.1 was %.2f times speed of sse\n", (float)time_sse / (float)time_sse4_1);
		printf("avx2   was %.2f times speed of scalar\n", (float)time_scalar / (float)time_avx2);
		printf("avx2   was %.2f times speed of sse\n", (float)time_sse / (float)time_avx2);
		printf("avx2   was %.2f times speed of sse4.1\n", (float)time_sse4_1 / (float)time_avx2);
		printf("main   was %.2f times speed of scalar\n", (float)time_scalar / (float)time_main);
		printf("main   was %.2f times speed of sse\n", (float)time_sse / (float)time_main);
	}
	{ // 4 channels
		printf("\n4 channel tests:\n\n");
		int64_t time_dynamic = TestDeinterlace(azaBufferDeinterlace_dynamic  , 4, " dynamic");
		int64_t time_scalar = TestDeinterlace(azaBufferDeinterlace4Ch_scalar, 4, "  scalar");
		int64_t time_sse = TestDeinterlace(azaBufferDeinterlace4Ch_sse      , 4, "     sse");
		int64_t time_avx2 = TestDeinterlace(azaBufferDeinterlace4Ch_avx2    , 4, "    avx2");
		int64_t time_main = TestDeinterlace(azaBufferDeinterlace            , 4, "main_api");
		TestDeinterlace(azaBufferDeinterlace_memcpy, 4, "  memcpy");
		printf("scalar was %.2f times speed of dynamic\n", (float)time_dynamic / (float)time_scalar);
		printf("sse    was %.2f times speed of scalar\n", (float)time_scalar / (float)time_sse);
		printf("avx2   was %.2f times speed of scalar\n", (float)time_scalar / (float)time_avx2);
		printf("avx2   was %.2f times speed of sse\n", (float)time_sse / (float)time_avx2);
		printf("main   was %.2f times speed of scalar\n", (float)time_scalar / (float)time_main);
		printf("main   was %.2f times speed of sse\n", (float)time_sse / (float)time_main);
	}

	azaDeinit();
	return 0;
}
