/*
	File: azaSampleDelay.c
	Author: Philip Haynes
	Testing the correctness of sample delay.
*/

#include "../testing.h"

#include <AzAudio/error.h>
#include <AzAudio/math.h>
#include <AzAudio/dsp/azaSampleDelay.h>

static void ut_test_azaSampleDelay(uint32_t frames, uint32_t leadingFrames, uint32_t trailingFrames, uint32_t delayFrames, uint8_t channelCount, bool transitive) {
	azaBuffer dst, src;
	azaBufferInit(&src, frames, leadingFrames, trailingFrames, (azaChannelLayout) { .count = channelCount });
	if (transitive) {
		azaBufferInit(&dst, frames, leadingFrames, trailingFrames, (azaChannelLayout) { .count = channelCount });
	} else {
		dst = azaBufferView(&src);
	}
	azaSampleDelay sampleDelay;
	azaSampleDelayInit(&sampleDelay, (azaSampleDelayConfig) {
		.delayFrames = delayFrames
	});

	int iterationsToDo = AZA_MAX(2, 1 + delayFrames / frames);
	for (int iteration = 0; iteration < iterationsToDo; iteration++) {
		for (int32_t i = -(int32_t)leadingFrames * channelCount; i < (int32_t)(frames + trailingFrames) * channelCount; i++) {
			src.pSamples[i] = (float)(i + (int32_t)frames * iteration * channelCount);
		}

		int err = azaSampleDelayProcess(&sampleDelay, &dst, &src, 0);
		if (err) {
			UT_SUBMIT_FAIL("azaSampleDelayProcess returned an error: %s", azaErrorString(err));
		}

		utBeginSubtest(azaTextFormat("Leading Frames (call #%d)", iteration));
		if (transitive) {
			for (int32_t i = -(int32_t)leadingFrames * channelCount; i < 0; i++) {
				UT_EXPECT_EQUAL(UT_FAIL, dst.pSamples[i], 0.0f, "dst.pSamples[i] = %f, i = %i", dst.pSamples[i], i);
			}
		} else {
			for (int32_t i = -(int32_t)leadingFrames * channelCount; i < 0; i++) {
				float expected = (float)(i + (int32_t)frames * iteration * channelCount);
				UT_EXPECT_EQUAL(UT_FAIL, dst.pSamples[i], expected, "dst.pSamples[i] = %f, expected = %f, i = %i", dst.pSamples[i], expected, i);
			}
		}
		utEndSubtest();

		utBeginSubtest(azaTextFormat("Body Frames (call #%d)", iteration));
		for (int32_t i = 0; i < (int32_t)frames * channelCount; i++) {
			float expected = (float)AZA_MAX(0, i + ((int32_t)frames * iteration  - (int32_t)delayFrames) * channelCount);
			UT_EXPECT_EQUAL(UT_FAIL, dst.pSamples[i], expected, "dst.pSamples[i] = %f, expected = %f, i = %i", dst.pSamples[i], expected, i);
		}
		utEndSubtest();

		utBeginSubtest(azaTextFormat("Trailing Frames (call #%d)", iteration));
		if (transitive) {
			for (int32_t i = -(int32_t)leadingFrames * channelCount; i < 0; i++) {
				UT_EXPECT_EQUAL(UT_FAIL, dst.pSamples[i], 0.0f, "dst.pSamples[i] = %f, i = %i", dst.pSamples[i], i);
			}
		} else {
			for (int32_t i = -(int32_t)leadingFrames * channelCount; i < 0; i++) {
				float expected = (float)(i + (int32_t)frames * iteration * channelCount);
				UT_EXPECT_EQUAL(UT_FAIL, dst.pSamples[i], expected, "dst.pSamples[i] = %f, expected = %f, i = %i", dst.pSamples[i], expected, i);
			}
		}
		utEndSubtest();
	}

	azaSampleDelayDeinit(&sampleDelay);
	azaBufferDeinit(&src, true);
	if (transitive) {
		azaBufferDeinit(&dst, true);
	}
}

void ut_run_azaSampleDelay() {
	for (uint8_t channelCount = 1; channelCount <= AZA_MAX_CHANNEL_POSITIONS; channelCount++) {
		utBeginTest(azaTextFormat("azaSampleDelay.c with delay < buffer size (intransitive, %hhu channels)", channelCount));
		ut_test_azaSampleDelay(6, 1, 1, 3, channelCount, false);
		utEndTest();
		utBeginTest(azaTextFormat("azaSampleDelay.c with delay = buffer size (intransitive, %hhu channels)", channelCount));
		ut_test_azaSampleDelay(6, 1, 1, 6, channelCount, false);
		utEndTest();
		utBeginTest(azaTextFormat("azaSampleDelay.c with delay > buffer size (intransitive, %hhu channels)", channelCount));
		ut_test_azaSampleDelay(6, 1, 1, 12, channelCount, false);
		utEndTest();
		utBeginTest(azaTextFormat("azaSampleDelay.c with delay < buffer size (transitive, %hhu channels)", channelCount));
		ut_test_azaSampleDelay(6, 1, 1, 3, channelCount, true);
		utEndTest();
		utBeginTest(azaTextFormat("azaSampleDelay.c with delay = buffer size (transitive, %hhu channels)", channelCount));
		ut_test_azaSampleDelay(6, 1, 1, 6, channelCount, true);
		utEndTest();
		utBeginTest(azaTextFormat("azaSampleDelay.c with delay > buffer size (transitive, %hhu channels)", channelCount));
		ut_test_azaSampleDelay(6, 1, 1, 12, channelCount, true);
		utEndTest();
	}
}