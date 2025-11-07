/*
	File: azaBufferResize.c
	Author: Philip Haynes
	Testing the correctness of buffer resizing.
*/

#include "../testing.h"

#include <AzAudio/dsp/azaBuffer.h>

void ut_run_azaBufferResize() {
	{
		utBeginTest("azaBufferResize.c Shrink Leading Frames");
		const uint32_t frames = 5;
		const uint32_t framesLeadingStart = 10;
		const uint32_t framesLeadingEnd = 5;
		const uint32_t framesTrailing = 5;

		azaBuffer buffer;
		azaBufferInit(&buffer, frames, framesLeadingStart, framesTrailing, azaChannelLayoutMono());

		for (int32_t i = -(int32_t)framesLeadingStart; i < (int32_t)(frames + framesTrailing); i++) {
			buffer.pSamples[i] = (float)i;
		}

		azaBufferResize(&buffer, frames, framesLeadingEnd, framesTrailing, azaChannelLayoutMono());

		utBeginSubtest("Leading Frames");
		for (int32_t i = -(int32_t)framesLeadingEnd; i < 0; i++) {
			UT_EXPECT_EQUAL(UT_FAIL, buffer.pSamples[i], (float)i, "buffer.pSamples[i] = %f, i = %i", buffer.pSamples[i], i);
		}
		utEndSubtest();

		utBeginSubtest("Body Frames");
		for (int32_t i = 0; i < (int32_t)frames; i++) {
			UT_EXPECT_EQUAL(UT_FAIL, buffer.pSamples[i], (float)i, "buffer.pSamples[i] = %f, i = %i", buffer.pSamples[i], i);
		}
		utEndSubtest();

		utBeginSubtest("Trailing Frames");
		for (int32_t i = (int32_t)frames; i < (int32_t)(frames + framesTrailing); i++) {
			UT_EXPECT_EQUAL(UT_FAIL, buffer.pSamples[i], (float)i, "buffer.pSamples[i] = %f, i = %i", buffer.pSamples[i], i);
		}
		utEndSubtest();

		azaBufferDeinit(&buffer, true);

		utEndTest();
	}
	{
		utBeginTest("azaBufferResize.c Shrink Trailing Frames");
		const uint32_t frames = 5;
		const uint32_t framesLeading = 5;
		const uint32_t framesTrailingStart = 10;
		const uint32_t framesTrailingEnd = 5;

		azaBuffer buffer;
		azaBufferInit(&buffer, frames, framesLeading, framesTrailingStart, azaChannelLayoutMono());

		for (int32_t i = -(int32_t)framesLeading; i < (int32_t)(frames + framesTrailingStart); i++) {
			buffer.pSamples[i] = (float)i;
		}

		azaBufferResize(&buffer, frames, framesLeading, framesTrailingEnd, azaChannelLayoutMono());

		utBeginSubtest("Leading Frames");
		for (int32_t i = -(int32_t)framesLeading; i < 0; i++) {
			UT_EXPECT_EQUAL(UT_FAIL, buffer.pSamples[i], (float)i, "buffer.pSamples[i] = %f, i = %i", buffer.pSamples[i], i);
		}
		utEndSubtest();

		utBeginSubtest("Body Frames");
		for (int32_t i = 0; i < (int32_t)frames; i++) {
			UT_EXPECT_EQUAL(UT_FAIL, buffer.pSamples[i], (float)i, "buffer.pSamples[i] = %f, i = %i", buffer.pSamples[i], i);
		}
		utEndSubtest();

		utBeginSubtest("Trailing Frames");
		for (int32_t i = (int32_t)frames; i < (int32_t)(frames + framesTrailingEnd); i++) {
			UT_EXPECT_EQUAL(UT_FAIL, buffer.pSamples[i], (float)i, "buffer.pSamples[i] = %f, i = %i", buffer.pSamples[i], i);
		}
		utEndSubtest();

		azaBufferDeinit(&buffer, true);

		utEndTest();
	}
	{
		utBeginTest("azaBufferResize.c Shrink Body Frames");
		const uint32_t framesStart = 10;
		const uint32_t framesEnd = 5;
		const uint32_t framesLeading = 5;
		const uint32_t framesTrailing = 5;

		azaBuffer buffer;
		azaBufferInit(&buffer, framesStart, framesLeading, framesTrailing, azaChannelLayoutMono());

		for (int32_t i = -(int32_t)framesLeading; i < (int32_t)(framesStart + framesTrailing); i++) {
			buffer.pSamples[i] = (float)i;
		}

		azaBufferResize(&buffer, framesEnd, framesLeading, framesTrailing, azaChannelLayoutMono());

		utBeginSubtest("Leading Frames");
		for (int32_t i = -(int32_t)framesLeading; i < 0; i++) {
			UT_EXPECT_EQUAL(UT_FAIL, buffer.pSamples[i], (float)i, "buffer.pSamples[i] = %f, i = %i", buffer.pSamples[i], i);
		}
		utEndSubtest();

		utBeginSubtest("Body Frames");
		for (int32_t i = 0; i < (int32_t)framesEnd; i++) {
			UT_EXPECT_EQUAL(UT_FAIL, buffer.pSamples[i], (float)i, "buffer.pSamples[i] = %f, i = %i", buffer.pSamples[i], i);
		}
		utEndSubtest();

		utBeginSubtest("Trailing Frames");
		for (int32_t i = (int32_t)framesEnd; i < (int32_t)(framesEnd + framesTrailing); i++) {
			UT_EXPECT_EQUAL(UT_FAIL, buffer.pSamples[i], (float)i, "buffer.pSamples[i] = %f, i = %i", buffer.pSamples[i], i);
		}
		utEndSubtest();

		azaBufferDeinit(&buffer, true);

		utEndTest();
	}
	{
		utBeginTest("azaBufferResize.c Grow Leading Frames");
		const uint32_t frames = 5;
		const uint32_t framesLeadingStart = 5;
		const uint32_t framesLeadingEnd = 10;
		const uint32_t framesTrailing = 5;

		azaBuffer buffer;
		azaBufferInit(&buffer, frames, framesLeadingStart, framesTrailing, azaChannelLayoutMono());

		for (int32_t i = -(int32_t)framesLeadingStart; i < (int32_t)(frames + framesTrailing); i++) {
			buffer.pSamples[i] = (float)i;
		}

		azaBufferResize(&buffer, frames, framesLeadingEnd, framesTrailing, azaChannelLayoutMono());

		utBeginSubtest("Leading Frames");
		for (int32_t i = -(int32_t)framesLeadingEnd; i < -(int32_t)framesLeadingStart; i++) {
			UT_EXPECT_EQUAL(UT_FAIL, buffer.pSamples[i], 0.0f, "buffer.pSamples[i] = %f, i = %i", buffer.pSamples[i], i);
		}
		for (int32_t i = -(int32_t)framesLeadingStart; i < 0; i++) {
			UT_EXPECT_EQUAL(UT_FAIL, buffer.pSamples[i], (float)i, "buffer.pSamples[i] = %f, i = %i", buffer.pSamples[i], i);
		}
		utEndSubtest();

		utBeginSubtest("Body Frames");
		for (int32_t i = 0; i < (int32_t)frames; i++) {
			UT_EXPECT_EQUAL(UT_FAIL, buffer.pSamples[i], (float)i, "buffer.pSamples[i] = %f, i = %i", buffer.pSamples[i], i);
		}
		utEndSubtest();

		utBeginSubtest("Trailing Frames");
		for (int32_t i = (int32_t)frames; i < (int32_t)(frames + framesTrailing); i++) {
			UT_EXPECT_EQUAL(UT_FAIL, buffer.pSamples[i], (float)i, "buffer.pSamples[i] = %f, i = %i", buffer.pSamples[i], i);
		}
		utEndSubtest();

		azaBufferDeinit(&buffer, true);

		utEndTest();
	}
	{
		utBeginTest("azaBufferResize.c Grow Trailing Frames");
		const uint32_t frames = 5;
		const uint32_t framesLeading = 5;
		const uint32_t framesTrailingStart = 5;
		const uint32_t framesTrailingEnd = 10;

		azaBuffer buffer;
		azaBufferInit(&buffer, frames, framesLeading, framesTrailingStart, azaChannelLayoutMono());

		for (int32_t i = -(int32_t)framesLeading; i < (int32_t)(frames + framesTrailingStart); i++) {
			buffer.pSamples[i] = (float)i;
		}

		azaBufferResize(&buffer, frames, framesLeading, framesTrailingEnd, azaChannelLayoutMono());

		utBeginSubtest("Leading Frames");
		for (int32_t i = -(int32_t)framesLeading; i < 0; i++) {
			UT_EXPECT_EQUAL(UT_FAIL, buffer.pSamples[i], (float)i, "buffer.pSamples[i] = %f, i = %i", buffer.pSamples[i], i);
		}
		utEndSubtest();

		utBeginSubtest("Body Frames");
		for (int32_t i = 0; i < (int32_t)frames; i++) {
			UT_EXPECT_EQUAL(UT_FAIL, buffer.pSamples[i], (float)i, "buffer.pSamples[i] = %f, i = %i", buffer.pSamples[i], i);
		}
		utEndSubtest();

		utBeginSubtest("Trailing Frames");
		for (int32_t i = (int32_t)frames; i < (int32_t)(frames + framesTrailingStart); i++) {
			UT_EXPECT_EQUAL(UT_FAIL, buffer.pSamples[i], (float)i, "buffer.pSamples[i] = %f, i = %i", buffer.pSamples[i], i);
		}
		for (int32_t i = (int32_t)(frames + framesTrailingStart); i < (int32_t)(frames + framesTrailingEnd); i++) {
			UT_EXPECT_EQUAL(UT_FAIL, buffer.pSamples[i], 0.0f, "buffer.pSamples[i] = %f, i = %i", buffer.pSamples[i], i);
		}
		utEndSubtest();

		azaBufferDeinit(&buffer, true);

		utEndTest();
	}
	{
		utBeginTest("azaBufferResize.c Grow Body Frames");
		const uint32_t framesStart = 5;
		const uint32_t framesEnd = 10;
		const uint32_t framesLeading = 5;
		const uint32_t framesTrailing = 5;

		azaBuffer buffer;
		azaBufferInit(&buffer, framesStart, framesLeading, framesTrailing, azaChannelLayoutMono());

		for (int32_t i = -(int32_t)framesLeading; i < (int32_t)(framesStart + framesTrailing); i++) {
			buffer.pSamples[i] = (float)i;
		}

		azaBufferResize(&buffer, framesEnd, framesLeading, framesTrailing, azaChannelLayoutMono());

		utBeginSubtest("Leading Frames");
		for (int32_t i = -(int32_t)framesLeading; i < 0; i++) {
			UT_EXPECT_EQUAL(UT_FAIL, buffer.pSamples[i], (float)i, "buffer.pSamples[i] = %f, i = %i", buffer.pSamples[i], i);
		}
		utEndSubtest();

		utBeginSubtest("Body Frames");
		for (int32_t i = 0; i < (int32_t)framesEnd; i++) {
			UT_EXPECT_EQUAL(UT_FAIL, buffer.pSamples[i], (float)i, "buffer.pSamples[i] = %f, i = %i", buffer.pSamples[i], i);
		}
		utEndSubtest();

		utBeginSubtest("Trailing Frames");
		for (int32_t i = (int32_t)framesEnd; i < (int32_t)(framesEnd + framesTrailing); i++) {
			UT_EXPECT_EQUAL(UT_FAIL, buffer.pSamples[i], 0.0f, "buffer.pSamples[i] = %f, i = %i", buffer.pSamples[i], i);
		}
		utEndSubtest();

		azaBufferDeinit(&buffer, true);

		utEndTest();
	}

}