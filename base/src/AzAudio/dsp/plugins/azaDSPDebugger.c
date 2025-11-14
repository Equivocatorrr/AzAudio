/*
	File: azaDSPDebugger.c
	Author: Philip Haynes
*/

#include "azaDSPDebugger.h"

#include "../../AzAudio.h"
#include "../../error.h"
#include "../azaKernel.h"

#include "../../gui/gui.h"

void azaDSPDebuggerInit(azaDSPDebugger *data, azaDSPDebuggerConfig config) {
	data->header = azaDSPDebuggerHeader;
	data->config = config;
}

void azaDSPDebuggerDeinit(azaDSPDebugger *data) {
	// We good :)
}

void azaDSPDebuggerReset(azaDSPDebugger *data) {
}

void azaDSPDebuggerResetChannels(azaDSPDebugger *data, uint32_t firstChannel, uint32_t channelCount) {
}

azaDSPDebugger* azaMakeDSPDebugger(azaDSPDebuggerConfig config) {
	azaDSPDebugger *result = aza_calloc(1, sizeof(azaDSPDebugger));
	if (result) {
		 azaDSPDebuggerInit(result, config);
	}
	return result;
}

void azaFreeDSPDebugger(void *dsp) {
	azaDSPDebuggerDeinit(dsp);
	aza_free(dsp);
}

azaDSP* azaMakeDefaultDSPDebugger() {
	return (azaDSP*)azaMakeDSPDebugger((azaDSPDebuggerConfig) {
		.specsToReport = (azaDSPSpecs) {
			.latencyFrames = 0,
			.leadingFrames = 0,
			.trailingFrames = 0,
		}
	});
}

int azaDSPDebuggerProcess(void *dsp, azaBuffer *dst, azaBuffer *src, uint32_t flags) {
	int err = AZA_SUCCESS;
	assert(dsp != NULL);
	azaDSPDebugger *data = (azaDSPDebugger*)dsp;

	if AZA_UNLIKELY(flags & AZA_DSP_PROCESS_FLAG_CUT) {
		azaDSPDebuggerReset(data);
	}

	err = azaCheckBuffersForDSPProcess(dst, src, /* sameFrameCount: */ true, /* sameChannelCount: */ true);
	if AZA_UNLIKELY(err) return err;

	if (src->pSamples != dst->pSamples) {
		azaBufferCopy(dst, src);
	}
	return err;
}

azaDSPSpecs azaDSPDebuggerGetSpecs(void *dsp, uint32_t samplerate) {
	azaDSPDebugger *data = (azaDSPDebugger*)dsp;
	return data->config.specsToReport;
}



// GUI



void azagDrawDSPDebugger(void *dsp, azagRect bounds) {
	azaDSPDebugger *data = dsp;
	int usedWidth;
	float latencyFrames = (float)data->config.specsToReport.latencyFrames;
	usedWidth = azagDrawSliderFloat(bounds, &latencyFrames, 0.0f, 48000.0f, 1.0f, 0.0f, "Latency Frames", "%.0f");
	data->config.specsToReport.latencyFrames = (uint32_t)roundf(latencyFrames);
	azagRectShrinkLeftMargin(&bounds, usedWidth);

	float leadingFrames = (float)data->config.specsToReport.leadingFrames;
	usedWidth = azagDrawSliderFloat(bounds, &leadingFrames, 0.0f, 48000.0f, 1.0f, 0.0f, "Leading Frames", "%.0f");
	data->config.specsToReport.leadingFrames = (uint32_t)roundf(leadingFrames);
	azagRectShrinkLeftMargin(&bounds, usedWidth);

	float trailingFrames = (float)data->config.specsToReport.trailingFrames;
	usedWidth = azagDrawSliderFloat(bounds, &trailingFrames, 0.0f, 48000.0f, 1.0f, 0.0f, "Trailing Frames", "%.0f");
	data->config.specsToReport.trailingFrames = (uint32_t)roundf(trailingFrames);
	azagRectShrinkLeftMargin(&bounds, usedWidth);
}