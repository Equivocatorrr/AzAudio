/*
	File: azaMonitorSpectrum.c
	Author: Philip Haynes
*/

#include "azaMonitorSpectrum.h"

#include "../../AzAudio.h"
#include "../../math.h"
#include "../../error.h"
#include "../../fft.h"

void azaMonitorSpectrumInit(azaMonitorSpectrum *data, azaMonitorSpectrumConfig config) {
	memset(data, 0, sizeof(*data));
	data->header = azaMonitorSpectrumHeader;
	data->config = config;
}

void azaMonitorSpectrumDeinit(azaMonitorSpectrum *data) {
	if (data->inputBuffer) {
		aza_free(data->inputBuffer);
	}
	if (data->outputBuffer) {
		aza_free(data->outputBuffer);
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
