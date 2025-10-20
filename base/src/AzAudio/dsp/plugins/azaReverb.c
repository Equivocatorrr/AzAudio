/*
	File: azaReverb.c
	Author: Philip Haynes
*/

#include "azaReverb.h"

#include "../../AzAudio.h"
#include "../../math.h"
#include "../../error.h"

void azaReverbInit(azaReverb *data, azaReverbConfig config) {
	data->header = azaReverbHeader;
	data->config = config;

	azaDelayInit(&data->inputDelay, (azaDelayConfig){
		.gainWet = 0.0f,
		.muteDry = true,
		.delay_ms = config.delay_ms,
		.feedback = 0.0f,
		.pingpong = 0.0f,
		.inputEffects = NULL,
	});

	float delays[AZAUDIO_REVERB_DELAY_COUNT] = {
		aza_samples_to_ms(2111, 48000),
		aza_samples_to_ms(2129, 48000),
		aza_samples_to_ms(2017, 48000),
		aza_samples_to_ms(2029, 48000),
		aza_samples_to_ms(1753, 48000),
		aza_samples_to_ms(1733, 48000),
		aza_samples_to_ms(1699, 48000),
		aza_samples_to_ms(1621, 48000),
		aza_samples_to_ms(1447, 48000),
		aza_samples_to_ms(1429, 48000),
		aza_samples_to_ms(1361, 48000),
		aza_samples_to_ms(1319, 48000),
		aza_samples_to_ms(1201, 48000),
		aza_samples_to_ms(1171, 48000),
		aza_samples_to_ms(1129, 48000),
		aza_samples_to_ms(1117, 48000),
		aza_samples_to_ms(1063, 48000),
		aza_samples_to_ms(1051, 48000),
		aza_samples_to_ms(1039, 48000),
		aza_samples_to_ms(1009, 48000),
		aza_samples_to_ms( 977, 48000),
		aza_samples_to_ms( 919, 48000),
		aza_samples_to_ms( 857, 48000),
		aza_samples_to_ms( 773, 48000),
		aza_samples_to_ms( 743, 48000),
		aza_samples_to_ms( 719, 48000),
		aza_samples_to_ms( 643, 48000),
		aza_samples_to_ms( 641, 48000),
		aza_samples_to_ms( 631, 48000),
		aza_samples_to_ms( 619, 48000),
	};
	for (int tap = 0; tap < AZAUDIO_REVERB_DELAY_COUNT; tap++) {
		azaDelay *delay = &data->delays[tap];
		azaFilter *filter = &data->filters[tap];
		azaDelayInit(delay, (azaDelayConfig) {
			.gainWet = 0.0f,
			.muteDry = true,
			.delay_ms = delays[tap],
			.feedback = 0.0f,
			.pingpong = 0.05f,
			.inputEffects = NULL,
		});
		azaFilterInit(filter, (azaFilterConfig) {
			.kind = AZA_FILTER_LOW_PASS,
			.poles = AZA_FILTER_6_DB,
			.frequency = 1000.0f,
			.dryMix = 0.0f,
			.gainWet = 0.0f,
		});
	}
}

void azaReverbDeinit(azaReverb *data) {
	azaDelayDeinit(&data->inputDelay);
	for (int tap = 0; tap < AZAUDIO_REVERB_DELAY_COUNT; tap++) {
		azaDelayDeinit(&data->delays[tap]);
		azaFilterDeinit(&data->filters[tap]);
	}
}

void azaReverbReset(azaReverb *data) {
	azaMetersReset(&data->metersInput);
	azaMetersReset(&data->metersOutput);
	azaDelayReset(&data->inputDelay);
	for (int tap = 0; tap < AZAUDIO_REVERB_DELAY_COUNT; tap++) {
		azaDelayReset(&data->delays[tap]);
		azaFilterReset(&data->filters[tap]);
	}
}

void azaReverbResetChannels(azaReverb *data, uint32_t firstChannel, uint32_t channelCount) {
	azaMetersResetChannels(&data->metersInput, firstChannel, channelCount);
	azaMetersResetChannels(&data->metersOutput, firstChannel, channelCount);
	azaDelayResetChannels(&data->inputDelay, firstChannel, channelCount);
	for (int tap = 0; tap < AZAUDIO_REVERB_DELAY_COUNT; tap++) {
		azaDelayResetChannels(&data->delays[tap], firstChannel, channelCount);
		azaFilterResetChannels(&data->filters[tap], firstChannel, channelCount);
	}
}

azaReverb* azaMakeReverb(azaReverbConfig config) {
	azaReverb *result = aza_calloc(1, sizeof(azaReverb));
	if (result) {
		azaReverbInit(result, config);
	}
	return result;
}

void azaFreeReverb(void *data) {
	azaReverbDeinit(data);
	aza_free(data);
}

azaDSP* azaMakeDefaultReverb() {
	return (azaDSP*)azaMakeReverb((azaReverbConfig) {
		.gainWet = -9.0f,
		.gainDry = 0.0f,
		.muteWet = false,
		.muteDry = false,
		.roomsize = 5.0f,
		.color = 1.0f,
		.delay_ms = 50.0f,
	});
}

int azaReverbProcess(void *dsp, azaBuffer *dst, azaBuffer *src, uint32_t flags) {
	// Bypass and chaining handled by azaDSPProcess
	int err = AZA_SUCCESS;
	assert(dsp != NULL);
	azaReverb *data = (azaReverb*)dsp;

	if AZA_UNLIKELY(flags & AZA_DSP_PROCESS_FLAG_CUT) {
		azaReverbReset(data);
	}

	err = azaCheckBuffersForDSPProcess(dst, src, /* sameFrameCount: */ true, /* sameChannelCount: */ true);
	if AZA_UNLIKELY(err) return err;

	// TODO: Our own combined delay buffer so we don't have so many
	// err = azaReverbHandleBufferResizes(data, dst->samplerate, dst->channelLayout.count);
	// if AZA_UNLIKELY(err) return err;

	if (dst->channelLayout.count > data->header.prevChannelCountDst) {
		azaReverbResetChannels(data, data->header.prevChannelCountDst, dst->channelLayout.count - data->header.prevChannelCountDst);
	}
	data->header.prevChannelCountDst = dst->channelLayout.count;

	if (data->header.selected) {
		azaMetersUpdate(&data->metersInput, src, 1.0f);
	}
	azaBuffer inputBuffer = azaPushSideBuffer(src->frames, 0, 0, src->channelLayout.count, src->samplerate);
	if (data->config.delay_ms != 0.0f) {
		data->inputDelay.config.delay_ms = data->config.delay_ms;
		err = azaDelayProcess(&data->inputDelay, &inputBuffer, src, flags);
		if AZA_UNLIKELY(err) return err;
	} else {
		azaBufferCopy(&inputBuffer, src);
	}
	azaBuffer sideBufferCombined = azaPushSideBufferZero(src->frames, 0, 0, src->channelLayout.count, src->samplerate);
	azaBuffer sideBufferEarly = azaPushSideBuffer(src->frames, 0, 0, src->channelLayout.count, src->samplerate);
	azaBuffer sideBufferDiffuse = azaPushSideBuffer(src->frames, 0, 0, src->channelLayout.count, src->samplerate);
	float feedback = 0.985f - (0.2f / data->config.roomsize);
	float color = data->config.color * 4000.0f;
	float amount = data->config.muteWet ? 0.0f :  aza_db_to_ampf(data->config.gainWet);
	float amountDry = data->config.muteDry ? 0.0f : aza_db_to_ampf(data->config.gainDry);
	for (int tap = 0; tap < AZAUDIO_REVERB_DELAY_COUNT*2/3; tap++) {
		// TODO: Make feedback depend on delay time such that they all decay in amplitude at the same rate over time
		azaDelay *delay = &data->delays[tap];
		azaFilter *filter = &data->filters[tap];
		delay->config.feedback = feedback;
		filter->config.frequency = color;
		azaBufferCopy(&sideBufferEarly, &inputBuffer);
		err = azaFilterProcess(filter, &sideBufferEarly, &sideBufferEarly, flags);
		if AZA_UNLIKELY(err) return err;
		err = azaDelayProcess(delay, &sideBufferEarly, &sideBufferEarly, flags);
		if AZA_UNLIKELY(err) return err;
		azaBufferMix(&sideBufferCombined, 1.0f, &sideBufferEarly, 1.0f / (float)AZAUDIO_REVERB_DELAY_COUNT);
	}
	for (int tap = AZAUDIO_REVERB_DELAY_COUNT*2/3; tap < AZAUDIO_REVERB_DELAY_COUNT; tap++) {
		azaDelay *delay = &data->delays[tap];
		azaFilter *filter = &data->filters[tap];
		delay->config.feedback = (float)(tap+AZAUDIO_REVERB_DELAY_COUNT) / (AZAUDIO_REVERB_DELAY_COUNT*2);
		filter->config.frequency = color*4.0f;
		azaBufferCopy(&sideBufferDiffuse, &sideBufferCombined);
		// What the hell is this?
		// azaBufferCopyChannel(&sideBufferDiffuse, 0, &sideBufferCombined, 0);
		err = azaFilterProcess(filter, &sideBufferDiffuse, &sideBufferDiffuse, flags);
		if AZA_UNLIKELY(err) return err;
		err = azaDelayProcess(delay, &sideBufferDiffuse, &sideBufferDiffuse, flags);
		if AZA_UNLIKELY(err) return err;
		azaBufferMix(&sideBufferCombined, 1.0f, &sideBufferDiffuse, 1.0f / (float)AZAUDIO_REVERB_DELAY_COUNT);
	}
	azaBufferMix(dst, amountDry, &sideBufferCombined, amount);
	azaPopSideBuffers(4);
	return AZA_SUCCESS;
}

azaDSPSpecs azaReverbGetSpecs(void *dsp, uint32_t samplerate) {
	azaReverb *data = (azaReverb*)dsp;
	azaDSPSpecs specs = azaDelayGetSpecs(&data->inputDelay, samplerate);
	azaDSPSpecs specsIndividualDelays = {0};
	for (uint32_t tap = 0; tap < AZAUDIO_REVERB_DELAY_COUNT; tap++) {
		azaDSPSpecs specTap = azaDSPGetSpecs(&data->delays[tap].header, samplerate);
		azaDSPSpecs specFilter = azaDSPGetSpecs(&data->filters[tap].header, samplerate);
		azaDSPSpecsCombineSerial(&specTap, &specFilter);
		azaDSPSpecsCombineParallel(&specsIndividualDelays, &specTap);
	}
	azaDSPSpecsCombineSerial(&specs, &specsIndividualDelays);
	return specs;
}