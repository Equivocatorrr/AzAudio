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
		int32_t leadingSamplesStart = -(int32_t)leadingFrames * channelCount;
		int32_t leadingSamplesEnd = 0;
		int32_t bodySamplesStart = leadingSamplesEnd;
		int32_t bodySamplesEnd = (int32_t)frames * channelCount;
		int32_t trailingSamplesStart = bodySamplesEnd;
		int32_t trailingSamplesEnd = (int32_t)(frames + trailingFrames) * channelCount;

		int32_t samplesOffsetBeforeDelay = (int32_t)frames * iteration * channelCount;
		int32_t samplesOffsetAfterDelay = ((int32_t)frames * iteration - (int32_t)delayFrames) * channelCount;

		for (int32_t i = leadingSamplesStart; i < trailingSamplesEnd; i++) {
			src.pSamples[i] = (float)(i + samplesOffsetBeforeDelay);
		}

		int err = azaSampleDelayProcess(&sampleDelay, &dst, &src, 0);
		if (err) {
			UT_SUBMIT_FAIL("azaSampleDelayProcess returned an error: %s", azaErrorString(err));
		}

		utBeginSubtest(azaTextFormat("Leading Frames (call #%d)", iteration));
		if (transitive) {
			for (int32_t i = leadingSamplesStart; i < leadingSamplesEnd; i++) {
				UT_EXPECT_EQUAL(UT_FAIL, dst.pSamples[i], 0.0f, "dst.pSamples[i] = %f, i = %i", dst.pSamples[i], i);
			}
		} else {
			for (int32_t i = leadingSamplesStart; i < leadingSamplesEnd; i++) {
				float expected = (float)(i + samplesOffsetBeforeDelay);
				UT_EXPECT_EQUAL(UT_FAIL, dst.pSamples[i], expected, "dst.pSamples[i] = %f, expected = %f, i = %i", dst.pSamples[i], expected, i);
			}
		}
		utEndSubtest();

		utBeginSubtest(azaTextFormat("Body Frames (call #%d)", iteration));
		for (int32_t i = bodySamplesStart; i < bodySamplesEnd; i++) {
			float expected = (float)AZA_MAX(0, i + samplesOffsetAfterDelay);
			UT_EXPECT_EQUAL(UT_FAIL, dst.pSamples[i], expected, "dst.pSamples[i] = %f, expected = %f, i = %i", dst.pSamples[i], expected, i);
		}
		utEndSubtest();

		utBeginSubtest(azaTextFormat("Trailing Frames (call #%d)", iteration));
		if (transitive) {
			for (int32_t i = trailingSamplesStart; i < trailingSamplesEnd; i++) {
				UT_EXPECT_EQUAL(UT_FAIL, dst.pSamples[i], 0.0f, "dst.pSamples[i] = %f, i = %i", dst.pSamples[i], i);
			}
		} else {
			for (int32_t i = trailingSamplesStart; i < trailingSamplesEnd; i++) {
				float expected = (float)(i + samplesOffsetBeforeDelay);
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
		ut_test_azaSampleDelay(6, 0, 0, 7, channelCount, false);
		utEndTest();
		utBeginTest(azaTextFormat("azaSampleDelay.c with delay < buffer size (transitive, %hhu channels)", channelCount));
		ut_test_azaSampleDelay(6, 1, 1, 3, channelCount, true);
		utEndTest();
		utBeginTest(azaTextFormat("azaSampleDelay.c with delay = buffer size (transitive, %hhu channels)", channelCount));
		ut_test_azaSampleDelay(6, 1, 1, 6, channelCount, true);
		utEndTest();
		utBeginTest(azaTextFormat("azaSampleDelay.c with delay > buffer size (transitive, %hhu channels)", channelCount));
		ut_test_azaSampleDelay(6, 1, 1, 7, channelCount, true);
		utEndTest();
	}
}