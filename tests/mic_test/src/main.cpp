/*
	File: main.cpp
	Author: singularity
	Simple test program for our library
*/

#if defined(_MSC_VER)
	#define _CRT_USE_CONFORMING_ANNEX_K_TIME 1
	#define _CRT_SECURE_NO_WARNINGS
#endif
#include <ctime>

#include <iostream>
#include <vector>

#include <cstdarg>

#include "log.hpp"
#include "AzAudio/AzAudio.h"
#include "AzAudio/dsp.h"
#include "AzAudio/error.h"

#ifdef __unix
#include <csignal>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <execinfo.h>
#include <unistd.h>

void handler(int sig) {
	void *array[50];
	size_t size = backtrace(array, 50);
	char **strings;
	strings = backtrace_symbols(array, size);
	sys::cout <<  "Error: signal " << sig << std::endl;
	for (uint32_t i = 0; i < size; i++) {
		sys::cout << strings[i] << std::endl;
	}
	free(strings);
	exit(1);
}
#endif

void logCallback(AzaLogLevel level, const char* format, ...) {
	if (level > azaLogLevel) return;
	char buffer[1024];
	time_t now = time(nullptr);
	strftime(buffer, sizeof(buffer), "%T", localtime(&now));
	sys::cout << "AzAudio[" << buffer << "] ";
	va_list args;
	va_start(args, format);
	vsnprintf(buffer, 1024, format, args);
	va_end(args);
	sys::cout << buffer;
}

azaLookaheadLimiter *limiter = nullptr;
azaCompressor *compressor = nullptr;
azaDelay *delay = nullptr;
azaDelay *delay2 = nullptr;
azaDelay *delay3 = nullptr;
azaReverb *reverb = nullptr;
azaFilter *highPass = nullptr;
azaGate *gate = nullptr;
azaFilter *gateBandPass = nullptr;
azaFilter *delayWetFilter = nullptr;
azaDelayDynamic *delayDynamic = nullptr;
azaSpatialize *spatialize = nullptr;

std::vector<float> micBuffer;
size_t lastMicBufferSize=0;
size_t lastInputBufferSize=0;
size_t numOutputBuffers=0;
size_t numInputBuffers=0;

float angle = 0.0f;
float angle2 = 0.0f;
std::vector<float> endChannelDelays;

struct SideBufferPopper {
	int count = 0;
	~SideBufferPopper() {
		azaPopSideBuffers(count);
	}
};

int processCallbackOutput(void *userdata, azaBuffer *dst, azaBuffer *src, uint32_t flags) {
	SideBufferPopper popper{1};
	azaBuffer srcBuffer = azaPushSideBufferZero(dst->frames, 0, 0, 1, dst->samplerate);
	numOutputBuffers++;
	if (micBuffer.size() == lastMicBufferSize && micBuffer.size() > dst->frames*2) {
		sys::cout << "Shrunk!" << std::endl;
		// Crossfade from new end to actual end
		float t = 0.0f;
		size_t crossFadeLen = std::min((size_t)(micBuffer.size() - dst->frames), (size_t)256);
		for (size_t i = micBuffer.size()-crossFadeLen; i < micBuffer.size(); i++) {
			t = std::min(1.0f, t + 1.0f / (float)crossFadeLen);
			float *micDst = &micBuffer[i - dst->frames];
			float micSrc = micBuffer[i];
			*micDst = *micDst + (micSrc - *micDst) * t;
		}
		micBuffer.erase(micBuffer.end() - dst->frames, micBuffer.end());
	}
	lastMicBufferSize = micBuffer.size();
	// printf("micBuffer size: %d\n", micBuffer.size() / dst->channels);
	// printf("output has ");
	size_t i = 0;
	static float lastSample = 0.0f;
	static float fadein = 0.0f;
	for (; i < std::min((size_t)dst->frames, micBuffer.size()); i++) {
		fadein = std::min(1.0f, fadein + 1.0f / 256.0f);
		lastSample = std::max(0.0f, lastSample - 1.0f / 256.0f);
		srcBuffer.pSamples[i] = lastSample + micBuffer[i] * fadein;
	}
	if (dst->frames > micBuffer.size()) {
		fadein = 0.0f;
		if (micBuffer.size()) lastSample = micBuffer.back();
		sys::cout << "Buffer underrun (" << micBuffer.size() << "/" << dst->frames << " frames available, last input buffer was " << lastInputBufferSize << " samples and last output buffer was " << dst->frames << " samples, had " << numInputBuffers << " input buffers and " << numOutputBuffers << " output buffers so far)" << std::endl;
	}
	micBuffer.erase(micBuffer.begin(), micBuffer.begin() + i);
	for (; i < dst->frames; i++) {
		lastSample = std::max(0.0f, lastSample - 1.0f / 256.0f);
		srcBuffer.pSamples[i] = lastSample;
	}
	int err;
	// float distance = (0.5f + 0.5f * sin(angle2)) * 100.0f;
	azaVec3 srcPosStart = {
		sin(angle) * 100.0f,
		10.0f,
		0.0f,
		// 0.0f,
	};
	angle += ((float)dst->frames / (float)dst->samplerate) * AZA_TAU * 0.15f;
	if (angle > AZA_TAU) {
		angle -= AZA_TAU;
	}
	angle2 += ((float)dst->frames / (float)dst->samplerate) * AZA_TAU * 0.125f;
	if (angle2 > AZA_TAU) {
		angle2 -= AZA_TAU;
	}
	// distance = (0.5f + 0.5f * sin(angle2)) * 100.0f;
	azaVec3 srcPosEnd = {
		sin(angle) * 100.0f,
		10.0f,
		// cos(angle) * distance,
		0.0f,
		// 0.0f,
	};
	if ((err = azaGateProcess(gate, &srcBuffer, &srcBuffer, flags))) {
		return err;
	}
	azaBufferZero(dst);
	float volumeStart = azaClampf(10.0f / sqrtf(azaVec3Norm(srcPosStart)), 0.0f, 1.0f);
	float volumeEnd = azaClampf(10.0f / sqrtf(azaVec3Norm(srcPosEnd)), 0.0f, 1.0f);
	azaSpatializeChannelConfig start = {{
		srcPosStart,
		volumeStart,
	}};
	azaSpatializeChannelConfig end = {{
		srcPosEnd,
		volumeEnd,
	}};
	azaSpatializeSetRamps(spatialize, 1, &start, &end, srcBuffer.frames, srcBuffer.samplerate);
	if ((err = azaSpatializeProcess(spatialize, dst, &srcBuffer, flags))) {
		return err;
	}
	// printf("gate gain: %f\n", gate->gain);
	float startChannelDelays[AZA_MAX_CHANNEL_POSITIONS];
	endChannelDelays.resize(dst->channelLayout.count, 0.0f);
	float lfo2 = sinf(angle2);
	for (size_t i = 0; i < endChannelDelays.size(); i++) {
		float &delay = endChannelDelays[i];
		startChannelDelays[i] = delay;
		delay = 700.0f + lfo2 * 300.0f;
	}
	// delayDynamic->config.feedback = 1.0f - (0.1f * pow(lfo2 * 0.5f + 0.5f, 4.0f));
	azaDelayDynamicSetRamps(delayDynamic, dst->channelLayout.count, startChannelDelays, endChannelDelays.data(), dst->frames, dst->samplerate);
	if ((err = azaDelayDynamicProcess(delayDynamic, dst, dst, flags))) {
		return err;
	}
	// if ((err = azaDelayProcess(delay, dst, dst, flags))) {
	// 	return err;
	// }
	// if ((err = azaDelayProcess(delay2, dst, dst, flags))) {
	// 	return err;
	// }
	// if ((err = azaDelayProcess(delay3, dst, dst, flags))) {
	// 	return err;
	// }
	if ((err = azaFilterProcess(highPass, dst, dst, flags))) {
		return err;
	}
	if ((err = azaReverbProcess(reverb, dst, dst, flags))) {
		return err;
	}
	if ((err = azaCompressorProcess(compressor, dst, dst, flags))) {
		return err;
	}
	if ((err = azaLookaheadLimiterProcess(limiter, dst, dst, flags))) {
		return err;
	}
	return AZA_SUCCESS;
}

int processCallbackInput(void *userdata, azaBuffer *dst, azaBuffer *src, uint32_t flags) {
	numInputBuffers++;
	lastInputBufferSize = src->frames;
	size_t b_i = micBuffer.size();
	micBuffer.resize(micBuffer.size() + src->frames);
	for (unsigned long i = 0; i < src->frames; i++) {
		micBuffer[b_i + i] = src->pSamples[i];
	}
	return AZA_SUCCESS;
}

int main(int argumentCount, char** argumentValues) {
	using a_fit = std::runtime_error;
	#ifdef __unix
	signal(SIGSEGV, handler);
	#endif
	try {
		azaSetLogCallback(logCallback);
		int err = azaInit();
		if (err) {
			throw a_fit("Failed to azaInit!");
		}
		// Change world to z-up
		azaWorldDefault.orientation.right = azaVec3 { 1.0f, 0.0f, 0.0f };
		azaWorldDefault.orientation.up = azaVec3 { 0.0f, 0.0f, 1.0f };
		azaWorldDefault.orientation.forward = azaVec3 { 0.0f, 1.0f, 0.0f };
		{ // Query devices
			size_t numOutputDevices = azaGetDeviceCount(AZA_OUTPUT);
			sys::cout << "Output Devices: " << numOutputDevices << std::endl;
			for (size_t i = 0; i < numOutputDevices; i++) {
				size_t channels = azaGetDeviceChannels(AZA_OUTPUT, i);
				sys::cout << "\t" << azaGetDeviceName(AZA_OUTPUT, i) << " with " << channels << " channels." << std::endl;
			}
			size_t numInputDevices = azaGetDeviceCount(AZA_INPUT);
			sys::cout << "Input Devices: " << numInputDevices << std::endl;
			for (size_t i = 0; i < numInputDevices; i++) {
				size_t channels = azaGetDeviceChannels(AZA_INPUT, i);
				sys::cout << "\t" << azaGetDeviceName(AZA_INPUT, i) << " with " << channels << " channels." << std::endl;
			}
		}
		azaStream streamInput = {0};
		streamInput.processCallback = processCallbackInput;
		if (azaStreamInit(&streamInput, azaStreamConfig {
				/*.deviceName = */ NULL,
				/*.samplerate = */ 0,
				/*.channels   = */ azaChannelLayoutMono(),
			}, AZA_INPUT, AZA_STREAM_COMMIT_FORMAT, false) != AZA_SUCCESS) {
			throw a_fit("Failed to init input stream!");
		}
		azaStream streamOutput = {0};
		// streamOutput.channels = azaChannelLayoutStandardFromCount(NUM_CHANNELS);
		streamOutput.processCallback = processCallbackOutput;
		if (azaStreamInit(&streamOutput, azaStreamConfig {
				/* .deviceName = */ NULL,
				/* .samplerate = */ azaStreamGetSamplerate(&streamInput),
				/* .channels   = */ 0,
			}, AZA_OUTPUT, 0, false) != AZA_SUCCESS) {
			throw a_fit("Failed to init output stream!");
		}



		// Configure all the DSP functions



		// gate runs on the single-channel mic buffer
		gateBandPass = azaMakeFilter(azaFilterConfig{
			/* .kind      = */ AZA_FILTER_BAND_PASS,
			/* .poles     = */ AZA_FILTER_6_DB,
			/* .frequency = */ 300.0f,
			/* .dryMix    = */ 0.0f,
		});

		gate = azaMakeGate(azaGateConfig{
			/* .threshold         = */-42.0f,
			/* .ratio             = */ 5.0f,
			/* .attack_ms         = */ 10.0f,
			/* .decay_ms          = */ 500.0f,
			/* .gainInput         = */ 0.0f,
			/* .gainOutput        = */ 0.0f,
			/* .activationEffects = */ (azaDSP*)gateBandPass,
		});

		delay = azaMakeDelay(azaDelayConfig{
			/* .gainWet      = */-15.0f,
			/* .gainDry      = */ 0.0f,
			/* .muteWet      = */ false,
			/* .muteDry      = */ false,
			/* _reserved[2]  = */ {0},
			/* .delay_ms     = */ 1234.5f,
			/* .feedback     = */ 0.5f,
			/* .pingpong     = */ 0.9f,
			/* .inputEffects = */ nullptr,
		});

		delay2 = azaMakeDelay(azaDelayConfig{
			/* .gainWet      = */-15.0f,
			/* .gainDry      = */ 0.0f,
			/* .muteWet      = */ false,
			/* .muteDry      = */ false,
			/* _reserved[2]  = */ {0},
			/* .delay        = */ 2345.6f,
			/* .feedback     = */ 0.5f,
			/* .pingpong     = */ 0.2f,
			/* .inputEffects = */ nullptr,
		});

		delayWetFilter = azaMakeFilter(azaFilterConfig{
			/* .kind      = */ AZA_FILTER_BAND_PASS,
			/* .poles     = */ AZA_FILTER_6_DB,
			/* .frequency = */ 800.0f,
			/* .dryMix    = */ 0.8f,
		});

		delay3 = azaMakeDelay(azaDelayConfig{
			/* .gainWet      = */-15.0f,
			/* .gainDry      = */ 0.0f,
			/* .muteWet      = */ false,
			/* .muteDry      = */ false,
			/* _reserved[2]  = */ {0},
			/* .delay        = */ 1000.0f / 3.0f,
			/* .feedback     = */ 0.98f,
			/* .pingpong     = */ 0.0f,
			/* .inputEffects = */ (azaDSP*)delayWetFilter,
		});

		highPass = azaMakeFilter(azaFilterConfig{
			/* .kind      = */ AZA_FILTER_HIGH_PASS,
			/* .poles     = */ AZA_FILTER_6_DB,
			/* .frequency = */ 50.0f,
			/* .dryMix    = */ 0.0f,
		});

		reverb = azaMakeReverb(azaReverbConfig{
			/* .gainWet  = */-15.0f,
			/* .gainDry  = */ 0.0f,
			/* .muteWet  = */ false,
			/* .muteDry  = */ false,
			/* .roomsize = */ 100.0f,
			/* .color    = */ 1.0f,
			/* .delay_ms = */ 50.0f,
		});
		// TODO: maybe recreate this? reverbData[c].delay = c * 377.0f / 48000.0f;

		compressor = azaMakeCompressor(azaCompressorConfig{
			/* .threshold = */-12.0f,
			/* .ratio     = */ 10.0f,
			/* .attack_ms = */ 100.0f,
			/* .decay_ms  = */ 200.0f,
			/* .gainInput = */ 24.0f,
		});

		limiter = azaMakeLookaheadLimiter(azaLookaheadLimiterConfig{
			/* .gainInput  = */ 6.0f,
			/* .gainOutput = */-6.0f,
		});

		delayDynamic = azaMakeDelayDynamic(azaDelayDynamicConfig{
			/* .gainWet            = */-3.0f,
			/* .gainDry            = */ 0.0f,
			/* .muteWet            = */ false,
			/* .muteDry            = */ false,
			/* .reserved[6]        = */ {0},
			/* .delayMax_ms        = */ 1000.0f,
			/* .delayFollowTime_ms = */ 100.0f,
			/* .feedback           = */ 0.7f,
			/* .pingpong           = */ 1.0f,
			// /* .inputEffects       = */ nullptr,
			/* .inputEffects       = */ (azaDSP*)delayWetFilter,
			/* .kernel             = */ nullptr,
		});

		spatialize = azaMakeSpatialize(azaSpatializeConfig{
			/* .world                 = */ nullptr,
			/* .doDoppler             = */ true,
			/* .doFilter              = */ true,
			/* .usePerChannelDelay    = */ true,
			/* .usePerChannelFilter   = */ true,
			/* .numSrcChannelsActive  = */ 1,
			/* .reserved[7]           = */ {0},
			/* .positionFollowTime_ms = */ 100.0f,
			/* .delayMax_ms           = */ 0.0f,
			/* .earDistance           = */ 0.0f,
			/* .channels[]            = */ {0},
		});

		azaStreamSetActive(&streamInput, true);
		azaStreamSetActive(&streamOutput, true);
		std::cout << "Press ENTER to stop" << std::endl;
		std::cin.get();
		azaStreamDeinit(&streamInput);
		azaStreamDeinit(&streamOutput);

		azaFreeLookaheadLimiter(limiter);
		azaFreeCompressor(compressor);
		azaFreeDelay(delay);
		azaFreeDelay(delay2);
		azaFreeDelay(delay3);
		azaFreeReverb(reverb);
		azaFreeFilter(highPass);
		azaFreeGate(gate);
		azaFreeFilter(gateBandPass);
		azaFreeFilter(delayWetFilter);
		azaFreeDelayDynamic(delayDynamic);
		azaFreeSpatialize(spatialize);

		azaDeinit();
	} catch (std::runtime_error& e) {
		sys::cout << "Runtime Error: " << e.what() << std::endl;
	}
	return 0;
}
