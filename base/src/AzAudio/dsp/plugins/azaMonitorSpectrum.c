/*
	File: azaMonitorSpectrum.c
	Author: Philip Haynes
*/

#include "azaMonitorSpectrum.h"

#include "../../AzAudio.h"
#include "../../math.h"
#include "../../error.h"
#include "../../fft.h"

#include "../../gui/gui.h"



const char *azaMonitorSpectrumModeString[AZA_MONITOR_SPECTRUM_MODE_COUNT] = {
	[AZA_MONITOR_SPECTRUM_MODE_ONE_CHANNEL] = "Single-Channel Mode",
	[AZA_MONITOR_SPECTRUM_MODE_AVG_CHANNELS] = "Channel-Average Mode",
};




void azaMonitorSpectrumInit(azaMonitorSpectrum *data, azaMonitorSpectrumConfig config) {
	memset(data, 0, sizeof(*data));
	data->header = azaMonitorSpectrumHeader;
	data->config = config;
}

void azaMonitorSpectrumDeinit(azaMonitorSpectrum *data) {
	if (data->inputBuffer) {
		aza_free(data->inputBuffer);
		data->inputBuffer = NULL;
		data->inputBufferCapacity = 0;
	}
	if (data->outputBuffer) {
		aza_free(data->outputBuffer);
		data->outputBuffer = NULL;
		data->outputBufferCapacity = 0;
	}
}

void azaMonitorSpectrumReset(azaMonitorSpectrum *data) {
	if (data->outputBuffer) {
		memset(data->outputBuffer, 0, sizeof(data->outputBuffer[0]) * data->outputBufferCapacity);
	}
	data->numCounted = 0;
}

void azaMonitorSpectrumResetChannels(azaMonitorSpectrum *data, uint32_t firstChannel, uint32_t channelCount) {
	// Nothing to do :)
}

azaMonitorSpectrum* azaMakeMonitorSpectrum(azaMonitorSpectrumConfig config) {
	azaMonitorSpectrum *result = aza_calloc(1, sizeof(azaMonitorSpectrum));
	if (result) {
		azaMonitorSpectrumInit(result, config);
	}
	return result;
}

void azaFreeMonitorSpectrum(void *data) {
	azaMonitorSpectrumDeinit(data);
	aza_free(data);
}

azaDSP* azaMakeDefaultMonitorSpectrum() {
	azaMonitorSpectrum *result = azaMakeMonitorSpectrum((azaMonitorSpectrumConfig) {
		.mode = AZA_MONITOR_SPECTRUM_MODE_AVG_CHANNELS,
		.fullWindowProgression = false,
		.window = 1024,
		.smoothing = 1,
		.floor = -96,
		.ceiling = 12,
	});
	return (azaDSP*)result;
}

static int azaMonitorSpectrumHandleBufferResizes(azaMonitorSpectrum *data, azaBuffer *buffer) {
	uint32_t requiredInputCapacity = data->config.window * buffer->channelLayout.count;
	if (requiredInputCapacity > data->inputBufferCapacity) {
		// Don't bother carrying data over
		if (data->inputBuffer) {
			aza_free(data->inputBuffer);
		}
		data->inputBuffer = aza_calloc(1, sizeof(float) * requiredInputCapacity);
		if (!data->inputBuffer) {
			data->inputBufferCapacity = 0;
			return AZA_ERROR_OUT_OF_MEMORY;
		}
		data->inputBufferCapacity = requiredInputCapacity;
		data->inputBufferUsed = 0;
	}
	if (data->inputBufferChannelCount != buffer->channelLayout.count) {
		// Reset
		data->inputBufferChannelCount = buffer->channelLayout.count;
		data->inputBufferUsed = 0;
	}
	uint32_t requiredOutputCapacity = data->config.window * 2;
	if (requiredOutputCapacity > data->outputBufferCapacity) {
		if (data->outputBuffer) {
			aza_free(data->outputBuffer);
		}
		data->outputBuffer = aza_calloc(1, sizeof(float) * requiredOutputCapacity);
		if (!data->outputBuffer) {
			data->outputBufferCapacity = 0;
			return AZA_ERROR_OUT_OF_MEMORY;
		}
		data->outputBufferCapacity = requiredOutputCapacity;
	}
	return AZA_SUCCESS;
}

// offset is the frame offset into buffer to start from
// returns how many frames were used from buffer
static uint32_t azaMonitorSpectrumPrimeBuffer(azaMonitorSpectrum *data, azaBuffer *buffer, uint32_t offset) {
	assert(offset <= buffer->frames);
	uint32_t used = AZA_MIN(data->config.window - data->inputBufferUsed, buffer->frames-offset);
	if (used) {
		azaBuffer dst = (azaBuffer) {
			.pSamples = data->inputBuffer + data->inputBufferChannelCount * data->inputBufferUsed,
			.samplerate = buffer->samplerate,
			.frames = used,
			.stride = data->inputBufferChannelCount,
			.channelLayout = buffer->channelLayout,
		};
		azaBuffer src = azaBufferSlice(buffer, offset, used);
		azaBufferCopy(&dst, &src);
		// memcpy(data->inputBuffer + data->inputBufferChannelCount * data->inputBufferUsed, buffer.pSamples + data->inputBufferChannelCount * offset, sizeof(float) * used * data->inputBufferChannelCount);
		data->inputBufferUsed += used;
	}
	return used;
}

static void azaMonitorSpectrumApplyWindow(azaBuffer buffer) {
	for (uint32_t i = 0; i < buffer.frames; i++) {
		float t = (float)i / (float)buffer.frames;
		// Half-sine window (probably bad?)
		// divide by integral of this sine to keep unity gain
		// float mul = sinf(0.5f * AZA_TAU * t) / 0.636619772368f;

		// Hann window (cos(t)*0.5+0.5)/0.5
		// Implicitly dividing by the integral
		// float mul = azaWindowHannf(t) / azaWindowHannIntegral;

		// Blackman "not very serious" window a0 - a1*cos(t) + a2*cos(2*t), a0 = 0.42, a1 = 0.5, a2 = 0.08
		// float mul = azaWindowBlackmanf(t) / azaWindowBlackmanIntegral;

		// Blackman-Harris window
		float mul = azaWindowBlackmanHarrisf(t) / azaWindowBlackmanHarrisIntegral;

		// Nuttall window
		// float mul = azaWindowNuttallf(t) / azaWindowNuttallIntegral;
		buffer.pSamples[i] *= mul;
	}
}

int azaMonitorSpectrumProcess(void *dsp, azaBuffer *dst, azaBuffer *src, uint32_t flags) {
	// Bypass handled by azaDSPProcess
	int err = AZA_SUCCESS;
	assert(dsp != NULL);
	azaMonitorSpectrum *data = (azaMonitorSpectrum*)dsp;

	if AZA_UNLIKELY(flags & AZA_DSP_PROCESS_FLAG_CUT) {
		azaMonitorSpectrumReset(data);
	}

	err = azaCheckBuffersForDSPProcess(dst, src, /* sameFrameCount: */ true, /* sameChannelCount: */ true);
	if AZA_UNLIKELY(err) return err;

	err = azaMonitorSpectrumHandleBufferResizes(data, src);
	if AZA_UNLIKELY(err) return err;

	if (dst->channelLayout.count > data->header.prevChannelCountDst) {
		azaMonitorSpectrumResetChannels(data, data->header.prevChannelCountDst, dst->channelLayout.count - data->header.prevChannelCountDst);
	} else if (dst->channelLayout.count < data->header.prevChannelCountDst) {
		data->config.channelChosen = AZA_MIN(dst->channelLayout.count-1, data->config.channelChosen);
	}
	data->header.prevChannelCountDst = dst->channelLayout.count;

	data->samplerate = src->samplerate;
	for (uint32_t offset = 0, used = 0; offset < src->frames; offset += used) {
		used = azaMonitorSpectrumPrimeBuffer(data, src, offset);
		while (data->inputBufferUsed >= data->config.window) {
			azaBuffer inputBuffer = (azaBuffer) {
				.pSamples = data->inputBuffer,
				.samplerate = data->samplerate,
				.frames = AZA_MIN(data->inputBufferUsed, data->config.window),
				.stride = data->inputBufferChannelCount,
				.channelLayout = (azaChannelLayout) {
					.count = data->inputBufferChannelCount,
				}
			};
			azaBuffer full = azaPushSideBuffer(data->config.window*2, 0, 0, 1, src->samplerate);
			azaBuffer dst = (azaBuffer) {
				.pSamples = data->outputBuffer,
				.samplerate = data->samplerate,
				.frames = data->config.window*2,
				.stride = 1,
				.channelLayout = (azaChannelLayout) {
					.count = 1,
				}
			};
			azaBuffer real = full;
			real.frames = data->config.window;
			azaBuffer imag = full;
			imag.frames = data->config.window;
			imag.pSamples += data->config.window;
			switch (data->config.mode) {
				case AZA_MONITOR_SPECTRUM_MODE_ONE_CHANNEL: {
					uint8_t channelChosen = data->config.channelChosen;
					if (channelChosen >= data->inputBufferChannelCount) {
						channelChosen = 0;
					}
					azaBufferCopyChannel(&real, 0, &inputBuffer, channelChosen);
					azaBufferZero(&imag);
					azaMonitorSpectrumApplyWindow(real);
					azaFFT(real.pSamples, imag.pSamples, real.frames);
					uint32_t window = (data->config.window >> 1) + 1;
					for (uint32_t i = 0; i < window; i++) {
						float x = real.pSamples[i];
						float y = imag.pSamples[i];
						float mag = sqrtf(x*x + y*y) / (float)window;
						float phase = atan2f(y, x);
						real.pSamples[i] = mag;
						imag.pSamples[i] = phase;
					}
					float mix = 1.0f / (float)(1 + data->numCounted);
					azaBufferMix(&dst, 1.0f - mix, &full, mix);
					data->numCounted = AZA_MIN(data->numCounted+1, data->config.smoothing);
				} break;
				case AZA_MONITOR_SPECTRUM_MODE_AVG_CHANNELS: {
					uint32_t window = (data->config.window >> 1) + 1;
					for (uint8_t c = 0; c < data->inputBufferChannelCount; c++) {
						azaBufferCopyChannel(&real, 0, &inputBuffer, c);
						azaBufferZero(&imag);
						azaMonitorSpectrumApplyWindow(real);
						azaFFT(real.pSamples, imag.pSamples, real.frames);
						for (uint32_t i = 0; i < window; i++) {
							float x = real.pSamples[i];
							float y = imag.pSamples[i];
							float mag = sqrtf(x*x + y*y) / (float)window;
							float phase = atan2f(y, x);
							real.pSamples[i] = mag;
							imag.pSamples[i] = phase;
						}
						float mix = 1.0f / (float)(1 + c + data->numCounted*data->inputBufferChannelCount);
						azaBufferMix(&dst, 1.0f - mix, &full, mix);
						data->numCounted = AZA_MIN(data->numCounted+1, data->config.smoothing);
					}
				} break;
				case AZA_MONITOR_SPECTRUM_MODE_COUNT: break;
			}
			azaPopSideBuffer();
			if (data->config.fullWindowProgression) {
				data->inputBufferUsed -= data->config.window;
				if (data->inputBufferUsed) {
					memmove(data->inputBuffer, data->inputBuffer + data->config.window * data->inputBufferChannelCount, sizeof(float) * data->inputBufferChannelCount * data->inputBufferUsed);
				}
			} else {
				// Shift by half a window each time
				uint32_t halfWindow = data->config.window>>1;
				data->inputBufferUsed -= halfWindow;
				memmove(data->inputBuffer, data->inputBuffer + halfWindow * data->inputBufferChannelCount, sizeof(float) * data->inputBufferChannelCount * data->inputBufferUsed);
			}
		}
	}
	return AZA_SUCCESS;
}



// GUI



static const azagColor colorBGTop       = {  20,  30,  40, 255 };
static const azagColor colorBGBot       = {  10,  15,  20, 255 };
static const azagColor colorFG          = { 100, 150, 200, 255 };
static const azagColor colorDBTick      = {  50,  70, 100,  96 };
static const azagColor colorDBTickUnity = { 100,  70,  50, 128 };

// static const azagColor colorControl = { 10, 15, 20, 255 };
// static const azagColor colorControlHighlight = { 50, 70, 100, 255 };
static const int windowControlWidth = 45;
static const int maxWindow = 8192;
static const int minWindow = 64;
static const int maxSmoothing = 63;
static const int minSmoothing = 0;
// We don't technically need limits except to avoid 16-bit integer overflow, but this allows a pretty ridiculous dynamic range.
static const int maxDynamicRange = 240;
static const int minDynamicRange = -240;


static int azaMonitorSpectrumBarXFromIndex(azaMonitorSpectrum *data, uint32_t width, uint32_t i) {
	// float nyquist = (float)data->samplerate / 2.0f;
	// float baseFreq = (float)data->samplerate / (float)data->config.window;
	uint32_t window = (data->config.window >> 1)+1;
	float baseLog = log2f((float)window);
	if (i) {
		return (int)roundf((float)width * (log2f(((float)i + 0.75f) / (float)window) + baseLog) / baseLog);
	} else {
		return 0;
	}
}

static int azaMonitorSpectrumBarXFromFreq(azaMonitorSpectrum *data, uint32_t width, float freq) {
	float nyquist = (float)data->samplerate / 2.0f;
	float baseFreq = (float)data->samplerate / (float)data->config.window;
	uint32_t window = (data->config.window >> 1)+1;
	float baseLog = log2f((float)window);
	if (freq >= baseFreq && freq <= nyquist) {
		return (int)roundf((float)width * (log2f(0.75f / (float)window + freq / nyquist) + baseLog) / baseLog);
	} else {
		return 0;
	}
}

#define AZA_MONITOR_SPECTRUM_DEBUG_FREQUENCY_MARKS 0

static int azagDrawSwitch(azagRect bounds, const char *label, const char *tooltip, azagColor colorBG, azagColor colorBGHighlight, azagColor colorText) {
	// TODO: Probably make a dropdown-style menu, and put it in gui.c
	int result = 0;
	bool mouseover = azagMouseInRect(bounds);
	azagDrawRect(bounds, mouseover ? colorBGHighlight : colorBG);
	azagDrawTextMargin(label, bounds.xy, AZAG_TEXT_SCALE_TEXT, colorText);
	if (mouseover) {
		azagTooltipAdd(tooltip, (azagPoint) {bounds.x + bounds.w, bounds.y + bounds.h/2}, 0.0f, 0.5f);
		result += (int)azagMouseWheelV();
		result += (int)azagMousePressed(AZAG_MOUSE_BUTTON_LEFT);
		result -= (int)azagMousePressed(AZAG_MOUSE_BUTTON_RIGHT);
	}
	return result;
}

void azagDrawMonitorSpectrum(void *dsp, azagRect bounds) {
	azaMonitorSpectrum *data = dsp;
	azagRect controlRect = bounds;
	controlRect.x += azagThemeCurrent.margin.x;
	controlRect.y += azagThemeCurrent.margin.y;
	controlRect.w = windowControlWidth;
	controlRect.h = azagTextHeightMargin("A", AZAG_TEXT_SCALE_TEXT);
	// int vMove = (int)azagMouseWheelV();
	azagColor colorControlText = azagColorSatVal(azagThemeCurrent.colorSwitch, 0.5f, 0.9f);

	// Mode
	assert(data->config.mode < AZA_MONITOR_SPECTRUM_MODE_COUNT);
	const char *label;
	switch (data->config.mode) {
		case AZA_MONITOR_SPECTRUM_MODE_ONE_CHANNEL: {
			label = azaTextFormat("Ch %d", (int)data->config.channelChosen);
		} break;
		case AZA_MONITOR_SPECTRUM_MODE_AVG_CHANNELS: {
			label = "Avg";
		} break;
		case AZA_MONITOR_SPECTRUM_MODE_COUNT: break;
	}
	int change = azagDrawSwitch(controlRect, label, azaMonitorSpectrumModeString[data->config.mode], azagThemeCurrent.colorSwitch, azagThemeCurrent.colorSwitchHighlight, colorControlText);
	switch (data->config.mode) {
		case AZA_MONITOR_SPECTRUM_MODE_ONE_CHANNEL: {
			if (change > 0) {
				if (data->config.channelChosen < data->inputBufferChannelCount-1) {
					data->config.channelChosen++;
				} else {
					data->config.mode = AZA_MONITOR_SPECTRUM_MODE_AVG_CHANNELS;
				}
			} else if (change < 0) {
				if (data->config.channelChosen > 0) {
					data->config.channelChosen--;
				} else {
					data->config.mode = AZA_MONITOR_SPECTRUM_MODE_AVG_CHANNELS;
				}
			}
		} break;
		case AZA_MONITOR_SPECTRUM_MODE_AVG_CHANNELS: {
			if (change > 0) {
				data->config.channelChosen = 0;
				data->config.mode = AZA_MONITOR_SPECTRUM_MODE_ONE_CHANNEL;
			} else if (change < 0) {
				data->config.channelChosen = data->inputBufferChannelCount-1;
				data->config.mode = AZA_MONITOR_SPECTRUM_MODE_ONE_CHANNEL;
			}
		} break;
		case AZA_MONITOR_SPECTRUM_MODE_COUNT: break;
	}

	controlRect.y += controlRect.h + azagThemeCurrent.margin.y*2;
	// FFT Window

	float ups = (float)data->samplerate / (float)data->config.window;
	if (!data->config.fullWindowProgression) {
		ups *= 2.0f;
	}
	change = azagDrawSwitch(controlRect, azaTextFormat2(0, "%d", data->config.window), azaTextFormat2(1, "FFT Window (%d updates/s)", (int)roundf(ups)), azagThemeCurrent.colorSwitch, azagThemeCurrent.colorSwitchHighlight, colorControlText);
	data->config.window = AZA_CLAMP((int)aza_shl_signed(data->config.window, change), minWindow, maxWindow);

	controlRect.y += controlRect.h + azagThemeCurrent.margin.y*2;
	// Smoothing

	change = azagDrawSwitch(controlRect, azaTextFormat("%d", data->config.smoothing), "Smoothing", azagThemeCurrent.colorSwitch, azagThemeCurrent.colorSwitchHighlight, colorControlText);
	data->config.smoothing = AZA_CLAMP((int)data->config.smoothing+change, minSmoothing, maxSmoothing);

	controlRect.y += controlRect.h + azagThemeCurrent.margin.y*2;
	// Ceiling

	change = azagDrawSwitch(controlRect, azaTextFormat("%+ddB", (int)data->config.ceiling), "Ceiling", azagThemeCurrent.colorSwitch, azagThemeCurrent.colorSwitchHighlight, colorControlText);
	if (!azagIsShiftDown()) {
		change *= 6;
	}
	data->config.ceiling = AZA_CLAMP((int)data->config.ceiling + change, data->config.floor+12, maxDynamicRange);

	controlRect.y += controlRect.h + azagThemeCurrent.margin.y*2;
	// Floor

	change = azagDrawSwitch(controlRect, azaTextFormat("%+ddB", (int)data->config.floor), "Floor", azagThemeCurrent.colorSwitch, azagThemeCurrent.colorSwitchHighlight, colorControlText);
	if (!azagIsShiftDown()) {
		change *= 6;
	}
	data->config.floor = AZA_CLAMP((int)data->config.floor + change, minDynamicRange, data->config.ceiling-12);

	// Spectrum Visualizer

	azagRect spectrumRect = bounds;
	azagRectShrinkLeft(&spectrumRect, windowControlWidth + azagThemeCurrent.margin.x*2);
	azagRectShrinkMargin(&spectrumRect, 0);
	azagRectShrinkBottom(&spectrumRect, azagTextHeightMargin("A\nA", AZAG_TEXT_SCALE_TEXT));
	float baseFreq = (float)data->samplerate / (float)data->config.window;
	azagDrawRectGradientV(spectrumRect, colorBGTop, colorBGBot);
	if (!data->outputBuffer) return;
	azagRect bar;
	uint32_t window = AZA_MIN((uint32_t)(data->config.window >> 1), data->outputBufferCapacity-1);
#if AZA_MONITOR_SPECTRUM_DEBUG_FREQUENCY_MARKS
	float lastFreq = 1.0f;
	uint32_t lastX = 0, lastWidth = 0;
#endif
	for (uint32_t i = 0; i <= window; i++) {
		float magnitude = data->outputBuffer[i];
		// float phase = data->outputBuffer[i + data->config.window] / AZA_TAU + 0.5f;
		float magDB = aza_amp_to_dbf(magnitude);
		int yOffset = azagDBToYOffsetClamped((float)data->config.ceiling - magDB, spectrumRect.h, 0, (float)(data->config.ceiling - data->config.floor));
		bar.x = azaMonitorSpectrumBarXFromIndex(data, spectrumRect.w-1, i);
		int right = azaMonitorSpectrumBarXFromIndex(data, spectrumRect.w-1, i+1);
		bar.w = AZA_MAX(right - bar.x, 1);
		bar.y = yOffset;
		bar.h = spectrumRect.h - bar.y;
		bar.x += spectrumRect.x;
		bar.y += spectrumRect.y;
		azagDrawRect(bar, colorFG);
		// This is atrocious to look at
		// azagDrawRect(bar, ColorHSV(phase, 0.5f, 0.8f, 255));
#if AZA_MONITOR_SPECTRUM_DEBUG_FREQUENCY_MARKS
		float freq = baseFreq * i;
		if (freq / lastFreq >= 2.0f) {
			azagDrawLine(bar.x, spectrumRect.y, bar.x, spectrumRect.y + spectrumRect.h + textMargin, (Color) {255,0,0,64});
			if (bar.x - lastX >= lastWidth+10) {
				int intFreq = (int)roundf(freq);
				const char *str;
				if (intFreq % 1000 == 0) {
					str = azaTextFormat("%dk", intFreq/1000);
				} else {
					str = azaTextFormat("%d", intFreq);
				}
				int width = azagTextWidth(str, AZAG_TEXT_SCALE_TEXT);
				int x = bar.x-width/2;
				x = AZA_MAX(spectrumRect.x, x);
				x = AZA_MIN(x, spectrumRect.x + spectrumRect.w - width);
				azagDrawText(str, x, spectrumRect.y + spectrumRect.h + margin + textMargin, 10, (Color) {255, 0, 0, 64});
				lastWidth = width;
				lastX = x;
			}
			lastFreq = freq;
		}
#endif
	}
	const uint32_t freqTicks[] = {
		10, 20, 30, 40, 50,
		75, 100, 150, 200, 250,
		300, 400, 500, 600, 700,
		800, 900, 1000, 1200, 1400,
		1600, 1800, 2000, 2500, 3000,
		3500, 4000, 5000, 6000, 7000,
		8000, 9000, 10000, 12000, 14000,
		16000, 18000, 20000, 22000, 24000,
		48000, 96000,
	};
	int xPrev[2] = {0};
	for (uint32_t i = 0; i < sizeof(freqTicks) / sizeof(freqTicks[0]); i++) {
		uint32_t freq = freqTicks[i];
		if (freq > data->samplerate/2) break;
		float fFreq = (float)freq;
		if (fFreq < baseFreq) continue;

		int x = azaMonitorSpectrumBarXFromFreq(data, spectrumRect.w-1, fFreq);
		x += spectrumRect.x;
		const char *str;
		if (freq % 1000 == 0) {
			str = azaTextFormat("%dk", freq/1000);
		} else {
			str = azaTextFormat("%d", freq);
		}
		int width = azagTextWidth(str, AZAG_TEXT_SCALE_TEXT);
		int textX = x - width/2;
		textX = AZA_MAX(spectrumRect.x, textX);
		textX = AZA_MIN(textX, spectrumRect.x + spectrumRect.w - width);
		int line = 0;
		int lineOffset = 0;
		if (textX - xPrev[0] >= azagThemeCurrent.margin.x) {
			line = 1;
			lineOffset = azagThemeCurrent.marginText.y;
		} else if (textX - xPrev[1] >= azagThemeCurrent.margin.x) {
			line = 2;
			lineOffset = azagThemeCurrent.marginText.y + azagTextHeight("A", AZAG_TEXT_SCALE_TEXT);
		}

		azagDrawLine((azagPoint) {x, spectrumRect.y}, (azagPoint) {x, spectrumRect.y + spectrumRect.h + lineOffset}, (azagColor) {0,0,0,128});
		if (line) {
			azagDrawText(str, (azagPoint) {textX, spectrumRect.y + spectrumRect.h + azagThemeCurrent.margin.y + lineOffset}, AZAG_TEXT_SCALE_TEXT, azagThemeCurrent.colorText);
			xPrev[line-1] = textX + width;
		}
	}
	azagDrawDBTicks(spectrumRect, data->config.ceiling - data->config.floor, data->config.ceiling, colorDBTick, colorDBTickUnity);
}