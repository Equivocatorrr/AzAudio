/*
	File: azaSpatialize.c
	Author: Philip Haynes
*/

#include "azaSpatialize.h"

#include "../../AzAudio.h"
#include "../../math.h"
#include "../../error.h"

#define PRINT_CHANNEL_AMPS 0
#define PRINT_CHANNEL_DELAYS 0

#if PRINT_CHANNEL_AMPS || PRINT_CHANNEL_DELAYS
static int repeatCount = 0;
#endif

typedef struct channelMetadata {
	uint32_t channel;
	float amp;
	float dot;
	float delay_ms;
} channelMetadata;

int compareChannelMetadataAmp(const void *_lhs, const void *_rhs) {
	const channelMetadata *lhs = _lhs;
	const channelMetadata *rhs = _rhs;
	if (lhs->amp == rhs->amp) return 0;
	// We want descending order
	if (lhs->amp < rhs->amp) return 1;
	return -1;
};
int compareChannelMetadataChannel(const void *_lhs, const void *_rhs) {
	const channelMetadata *lhs = _lhs;
	const channelMetadata *rhs = _rhs;
	if (lhs->channel == rhs->channel) return 0;
	// We want ascending order
	if (lhs->channel < rhs->channel) return -1;
	return 1;
};


static void azaGatherChannelPresenseMetadata(azaChannelLayout channelLayout, uint8_t *hasFront, uint8_t *hasMidFront, uint8_t *hasSub, uint8_t *hasBack, uint8_t *hasSide, uint8_t *hasAerials, uint8_t *subChannel) {
	for (uint8_t i = 0; i < channelLayout.count; i++) {
		switch (channelLayout.positions[i]) {
			case AZA_POS_LEFT_FRONT:
			case AZA_POS_CENTER_FRONT:
			case AZA_POS_RIGHT_FRONT:
				*hasFront = 1;
				break;
			case AZA_POS_LEFT_CENTER_FRONT:
			case AZA_POS_RIGHT_CENTER_FRONT:
				*hasMidFront = 1;
				break;
			case AZA_POS_SUBWOOFER:
				*hasSub = 1;
				*subChannel = i;
				break;
			case AZA_POS_LEFT_BACK:
			case AZA_POS_CENTER_BACK:
			case AZA_POS_RIGHT_BACK:
				*hasBack = 1;
				break;
			case AZA_POS_LEFT_SIDE:
			case AZA_POS_RIGHT_SIDE:
				*hasSide = 1;
				break;
			case AZA_POS_CENTER_TOP:
				*hasAerials = 1;
				break;
			case AZA_POS_LEFT_FRONT_TOP:
			case AZA_POS_CENTER_FRONT_TOP:
			case AZA_POS_RIGHT_FRONT_TOP:
				*hasFront = 1;
				*hasAerials = 1;
				break;
			case AZA_POS_LEFT_BACK_TOP:
			case AZA_POS_CENTER_BACK_TOP:
			case AZA_POS_RIGHT_BACK_TOP:
				*hasBack = 1;
				*hasAerials = 1;
				break;
		}
	}
}

static void azaGetChannelMetadata(azaChannelLayout channelLayout, azaVec3 *dstVectors, uint8_t *nonSubChannels, uint8_t *hasAerials) {
	uint8_t hasFront = 0, hasMidFront = 0, hasSub = 0, hasBack = 0, hasSide = 0, subChannel = 0;
	*hasAerials = 0;
	azaGatherChannelPresenseMetadata(channelLayout, &hasFront, &hasMidFront, &hasSub, &hasBack, &hasSide, hasAerials, &subChannel);
	*nonSubChannels = hasSub ? channelLayout.count-1 : channelLayout.count;
	// Angles are relative to front center, to be signed later
	// These relate to anglePhi above
	float angleFront = AZA_DEG_TO_RAD(75.0f), angleMidFront = AZA_DEG_TO_RAD(30.0f), angleSide = AZA_DEG_TO_RAD(90.0f), angleBack = AZA_DEG_TO_RAD(130.0f);
	if (hasFront && hasMidFront && hasSide && hasBack) {
		// Standard 8 or 9 speaker layout
		angleFront = AZA_DEG_TO_RAD(60.0f);
		angleMidFront = AZA_DEG_TO_RAD(30.0f);
		angleBack = AZA_DEG_TO_RAD(140.0f);
	} else if (hasFront && hasSide && hasBack) {
		// Standard 6 or 7 speaker layout
		angleFront = AZA_DEG_TO_RAD(60.0f);
		angleBack = AZA_DEG_TO_RAD(140.0f);
	} else if (hasFront && hasBack) {
		// Standard 4 or 5 speaker layout
		angleFront = AZA_DEG_TO_RAD(60.0f);
		angleBack = AZA_DEG_TO_RAD(115.0f);
	} else if (hasFront) {
		// Standard 2 or 3 speaker layout
		angleFront = AZA_DEG_TO_RAD(75.0f);
	} else if (hasBack) {
		// Weird, will probably never actually happen, but we can work with it
		angleBack = AZA_DEG_TO_RAD(110.0f);
	} else {
		// We're confused, just do anything
		angleFront = AZA_DEG_TO_RAD(45.0f);
		angleMidFront = AZA_DEG_TO_RAD(22.5f);
		angleSide = AZA_DEG_TO_RAD(90.0f);
		angleBack = AZA_DEG_TO_RAD(120.0f);
	}
	for (uint8_t i = 0; i < channelLayout.count; i++) {
		switch (channelLayout.positions[i]) {
			case AZA_POS_LEFT_FRONT:
				dstVectors[i] = (azaVec3) { sinf(-angleFront), 0.0f, cosf(-angleFront) };
				break;
			case AZA_POS_CENTER_FRONT:
				dstVectors[i] = (azaVec3) { 0.0f, 0.0f, 1.0f };
				break;
			case AZA_POS_RIGHT_FRONT:
				dstVectors[i] = (azaVec3) { sinf(angleFront), 0.0f, cosf(angleFront) };
				break;
			case AZA_POS_LEFT_CENTER_FRONT:
				dstVectors[i] = (azaVec3) { sinf(-angleMidFront), 0.0f, cosf(-angleMidFront) };
				break;
			case AZA_POS_RIGHT_CENTER_FRONT:
				dstVectors[i] = (azaVec3) { sinf(angleMidFront), 0.0f, cosf(angleMidFront) };
				break;
			case AZA_POS_LEFT_BACK:
				dstVectors[i] = (azaVec3) { sinf(-angleBack), 0.0f, cosf(-angleBack) };
				break;
			case AZA_POS_CENTER_BACK:
				dstVectors[i] = (azaVec3) { 0.0f, 0.0f, -1.0f };
				break;
			case AZA_POS_RIGHT_BACK:
				dstVectors[i] = (azaVec3) { sinf(angleBack), 0.0f, cosf(angleBack) };
				break;
			case AZA_POS_LEFT_SIDE:
				dstVectors[i] = (azaVec3) { sinf(-angleSide), 0.0f, cosf(-angleSide) };
				break;
			case AZA_POS_RIGHT_SIDE:
				dstVectors[i] = (azaVec3) { sinf(angleSide), 0.0f, cosf(angleSide) };
				break;
			case AZA_POS_CENTER_TOP:
				dstVectors[i] = (azaVec3) { 0.0f, 1.0f, 0.0f };
				break;
			case AZA_POS_LEFT_FRONT_TOP:
				dstVectors[i] = azaVec3Normalized((azaVec3) { sinf(-angleFront), 1.0f, cosf(-angleFront) });
				break;
			case AZA_POS_CENTER_FRONT_TOP:
				dstVectors[i] = azaVec3Normalized((azaVec3) { 0.0f, 1.0f, 1.0f });
				break;
			case AZA_POS_RIGHT_FRONT_TOP:
				dstVectors[i] = azaVec3Normalized((azaVec3) { sinf(angleFront), 1.0f, cosf(angleFront) });
				break;
			case AZA_POS_LEFT_BACK_TOP:
				dstVectors[i] = azaVec3Normalized((azaVec3) { sinf(-angleBack), 1.0f, cosf(-angleBack) });
				break;
			case AZA_POS_CENTER_BACK_TOP:
				dstVectors[i] = azaVec3Normalized((azaVec3) { 0.0f, 1.0f, -1.0f });
				break;
			case AZA_POS_RIGHT_BACK_TOP:
				dstVectors[i] = azaVec3Normalized((azaVec3) { sinf(angleBack), 1.0f, cosf(angleBack) });
				break;
			default: // This includes AZA_POS_SUBWOOFER
				continue;
		}
	}
}

#define FILTER_IN_CHANNEL_DATA 1

void azaSpatializeInit(azaSpatialize *data, azaSpatializeConfig config) {
	data->header = azaSpatializeHeader;
	data->config = config;
	azaDelayDynamicConfig delayConfig = {
		.gainWet = 0.0f,
		.gainDry = 0.0f,
		.muteWet = false,
		.muteDry = true,
		.delayMax_ms = config.delayMax_ms != 0.0f ? config.delayMax_ms : 500.0f,
		.delayFollowTime_ms = 10.0f,
		.feedback = 0.0f,
		.pingpong = 0.0f,
		.inputEffects = NULL,
		.kernel = NULL,
		.channels = {0},
	};
	azaFilterConfig filterConfig = {
		.kind = AZA_FILTER_LOW_PASS,
		.poles = AZA_FILTER_6_DB,
		.frequency = 15000.0f,
		.dryMix = 0.0f,
		.gainWet = 0.0f,
		.channelFrequencyOverride = {0},
	};
#if FILTER_IN_CHANNEL_DATA
	for (uint8_t c = 0; c < AZA_MAX_CHANNEL_POSITIONS; c++) {
		azaFilterInit(&data->channelData[c].filter, filterConfig);
		azaDelayDynamicInit(&data->channelData[c].delay, delayConfig);
	}
#else
	azaFilterInit(&data->filter, filterConfig);
	azaDelayDynamicInit(&data->delay, delayConfig);
#endif
}

void azaSpatializeDeinit(azaSpatialize *data) {
#if FILTER_IN_CHANNEL_DATA
	for (uint8_t c = 0; c < AZA_MAX_CHANNEL_POSITIONS; c++) {
		azaFilterDeinit(&data->channelData[c].filter);
		azaDelayDynamicDeinit(&data->channelData[c].delay);
	}
#else
	azaFilterDeinit(&data->filter);
	azaDelayDynamicDeinit(&data->delay);
#endif
}

void azaSpatializeReset(azaSpatialize *data) {
#if FILTER_IN_CHANNEL_DATA
	for (uint8_t c = 0; c < AZA_MAX_CHANNEL_POSITIONS; c++) {
		azaFilterReset(&data->channelData[c].filter);
		azaDelayDynamicReset(&data->channelData[c].delay);
	}
#else
	azaFilterReset(&data->filter);
	azaDelayDynamicReset(&data->delay);
#endif
}

void azaSpatializeResetChannels(azaSpatialize *data, uint32_t firstChannel, uint32_t channelCount) {
#if FILTER_IN_CHANNEL_DATA
	for (uint8_t c = firstChannel; c < firstChannel + channelCount; c++) {
		azaFilterReset(&data->channelData[c].filter);
		azaDelayDynamicResetChannels(&data->channelData[c].delay, firstChannel, channelCount);
	}
#else
	azaFilterResetChannels(&data->filter, firstChannel, channelCount);
	azaDelayDynamicResetChannels(&data->delay, firstChannel, channelCount);
#endif
}

azaSpatialize* azaMakeSpatialize(azaSpatializeConfig config) {
	azaSpatialize *result = aza_calloc(1, sizeof(azaSpatialize));
	if (result) {
		azaSpatializeInit(result, config);
	}
	return result;
}

void azaFreeSpatialize(void *data) {
	azaSpatializeDeinit(data);
	aza_free(data);
}

azaDSP* azaMakeDefaultSpatialize() {
	return (azaDSP*)azaMakeSpatialize((azaSpatializeConfig) {
		.world = NULL,
		.doDoppler = true,
		.doFilter = true,
		.usePerChannelDelay = true,
		.usePerChannelFilter = true,
		.numSrcChannelsActive = 1,
		.targetFollowTime_ms = 20.0f,
		.delayMax_ms = 0.0f,
		.earDistance = 0.085f,
		.channels = {0},
	});
}

static float azaSpatializeGetFilterCutoff(float delay, float dot) {
	return 192000.0f / azaMaxf(delay, 1.0f) * (dot * 0.35f + 0.65f);
}

//int azaSpatializeProcess(azaSpatialize *data, azaBuffer dstBuffer, azaBuffer srcBuffer, azaVec3 srcPosStart, float srcAmpStart, azaVec3 srcPosEnd, float srcAmpEnd) {
int azaSpatializeProcess(void *dsp, azaBuffer *dst, azaBuffer *src, uint32_t flags) {
	// Bypass handled by azaDSPProcess
	int err = AZA_SUCCESS;
	assert(dsp != NULL);
	azaSpatialize *data = (azaSpatialize*)dsp;

	if AZA_UNLIKELY(flags & AZA_DSP_PROCESS_FLAG_CUT) {
		azaSpatializeReset(data);
	}

	err = azaCheckBuffersForDSPProcess(dst, src, /* sameFrameCount: */ true, /* sameChannelCount: */ false);
	if AZA_UNLIKELY(err) return err;

	if (dst->channelLayout.count > data->header.prevChannelCountDst) {
		azaSpatializeResetChannels(data, data->header.prevChannelCountDst, dst->channelLayout.count - data->header.prevChannelCountDst);
	}
	data->header.prevChannelCountDst = dst->channelLayout.count;

	if (data->header.selected) {
		azaMetersUpdate(&data->metersInput, src, 1.0f);
	}

	uint8_t srcChannels = data->config.numSrcChannelsActive ? AZA_MIN(src->channelLayout.count, data->config.numSrcChannelsActive) : src->channelLayout.count;

	const azaWorld *world = data->config.world;
	if (world == NULL) {
		world = &azaWorldDefault;
	}
	if (world->speedOfSound <= 0.0f) {
		AZA_LOG_ERR("%s error: world->speedOfSound (%f) is out of bounds! This must be a positive nonzero value!\n", AZA_FUNCTION_NAME, world->speedOfSound);
		return AZA_ERROR_INVALID_CONFIGURATION;
	}

	// Channel layout metadata
	uint8_t nonSubChannels, hasAerials;
	azaVec3 earNormal[AZA_MAX_CHANNEL_POSITIONS];
	azaGetChannelMetadata(dst->channelLayout, earNormal, &nonSubChannels, &hasAerials);
	// Used to divide some volumes across channels
	float channelCountDenominator = (float)AZA_MAX(nonSubChannels, 1);

	// Since src and dst can be the same buffer, copy src out, zero dst, and then go from there.
	azaBuffer srcBuffer = azaPushSideBuffer(src->frames, 0, 0, srcChannels, src->samplerate);
	{
		uint8_t oldChannels = src->channelLayout.count;
		src->channelLayout.count = srcChannels;
		azaBufferCopy(&srcBuffer, src);
		src->channelLayout.count = oldChannels;
	}
	azaBufferZero(dst);
	azaBuffer sideBuffer = azaPushSideBufferCopyZero(dst);

	// We'll add this to per-channel delays to avoid negative delays.
	// TODO: We may consider adding this to the reported plugin delay to factor in to delay compensation.
	float minDelay_ms = data->config.earDistance / world->speedOfSound * 1000.0f;
	float bufferLen_ms = aza_samples_to_ms((float)dst->frames, (float)dst->samplerate);
	float followerDeltaT = bufferLen_ms / data->config.targetFollowTime_ms;

	float minAmp = dst->channelLayout.formFactor == AZA_FORM_FACTOR_HEADPHONES ? 0.5f : 0.0f;

	for (uint8_t srcC = 0; srcC < srcChannels; srcC++) {
		// Transform srcPos to headspace
		// TODO: Handle events
		azaFollowerLinearSetTarget(&data->channelData[srcC].amplitude, data->config.channels[srcC].target.amplitude);
		azaFollowerLinear3DSetTarget(&data->channelData[srcC].position, data->config.channels[srcC].target.position);
		azaVec3 srcPosStart = azaWorldTransformPoint(world, azaFollowerLinear3DUpdate(&data->channelData[srcC].position, followerDeltaT));
		float srcAmpStart = azaFollowerLinearUpdate(&data->channelData[srcC].amplitude, followerDeltaT);
		azaVec3 srcPosEnd = azaWorldTransformPoint(world, azaFollowerLinear3DGetValue(&data->channelData[srcC].position));
		float srcAmpEnd = azaFollowerLinearGetValue(&data->channelData[srcC].amplitude);
		float delayStart_ms = azaVec3Norm(srcPosStart) / world->speedOfSound * 1000.0f;
		float delayEnd_ms = azaVec3Norm(srcPosEnd) / world->speedOfSound * 1000.0f;
		azaVec3 srcNormalStart;
		azaVec3 srcNormalEnd;

		azaBuffer srcChannelBuffer = azaBufferOneChannel(&srcBuffer, srcC);

		azaSpatializeChannelData *channelData = &data->channelData[srcC];
		float avgDelayStart_ms = minDelay_ms;
		float avgDelayEnd_ms = minDelay_ms;
		if (data->config.doDoppler) {
			avgDelayStart_ms += delayStart_ms;
			avgDelayEnd_ms += delayEnd_ms;
		}

		if (dst->channelLayout.count == 1) {
			// Nothing to do but put it in there I guess
			azaBufferMixFade(&sideBuffer, 1.0f, 1.0f, NULL, &srcBuffer, srcAmpStart, srcAmpEnd, NULL);

			if (data->config.doFilter) {
				// TODO: Probably let the filter cutoff change smoothly
				channelData->filter.config.frequency = azaSpatializeGetFilterCutoff(delayStart_ms, 1.0f);
				err = azaFilterProcess(&data->channelData[srcC].filter, &sideBuffer, &sideBuffer, flags);
				if AZA_UNLIKELY(err) goto error;
			}
			if (data->config.doDoppler) {
				// Gotta do the delay
				channelData->delay.config.delayFollowTime_ms = bufferLen_ms;
				azaFollowerLinearJump(&channelData->delay.channelData[0].delay_ms, avgDelayStart_ms);
				channelData->delay.config.channels[0].delay_ms = avgDelayEnd_ms;
				err = azaDelayDynamicProcess(&channelData->delay, &sideBuffer, &sideBuffer, flags);
				if AZA_UNLIKELY(err) goto error;
			}

			azaBufferMix(dst, 1.0f, &sideBuffer, 1.0f);
			continue;
		}
		// How much of the signal to add to all channels in case srcPos is crossing close to the head
		float allChannelAddAmpStart = 0.0f;
		float allChannelAddAmpEnd = 0.0f;
		float normStart, normEnd;
		normStart = azaVec3Norm(srcPosStart);
		// static const azaVec3 FORWARD = { 0.0f, 0.0f, 1.0f };
		if (normStart < 0.5f) {
			allChannelAddAmpStart = (0.5f - normStart) * 2.0f;
			srcNormalStart = srcPosStart;
		} else {
			srcNormalStart = azaDivVec3Scalar(srcPosStart, normStart);
		}
		normEnd = azaVec3Norm(srcPosEnd);
		if (normEnd < 0.5f) {
			allChannelAddAmpEnd = (0.5f - normEnd) * 2.0f;
			srcNormalEnd = srcPosEnd;
		} else {
			srcNormalEnd = azaDivVec3Scalar(srcPosEnd, normEnd);
		}

		// Gather some channel info
		channelMetadata channelsStart[AZA_MAX_CHANNEL_POSITIONS] = {0};
		channelMetadata channelsEnd[AZA_MAX_CHANNEL_POSITIONS] = {0};
		float totalMagnitudeStart = 0.0f;
		float totalMagnitudeEnd = 0.0f;
		float earDistance = data->config.earDistance;
		if (earDistance <= 0.0f) {
			earDistance = 0.085f;
		}
		for (uint8_t i = 0; i < sideBuffer.channelLayout.count; i++) {
			channelsStart[i].channel = i;
			channelsEnd[i].channel = i;
			channelsStart[i].dot = azaVec3Dot(earNormal[i], srcNormalStart);
			channelsEnd[i].dot = azaVec3Dot(earNormal[i], srcNormalEnd);
			channelsStart[i].amp = 0.5f * normStart + 0.5f * channelsStart[i].dot + allChannelAddAmpStart / channelCountDenominator;
			channelsEnd[i].amp = 0.5f * normEnd + 0.5f * channelsEnd[i].dot + allChannelAddAmpEnd / channelCountDenominator;
			totalMagnitudeStart += channelsStart[i].amp;
			totalMagnitudeEnd += channelsEnd[i].amp;
		}

		// Use minimum number of channels needed for surround sound by remapping channel amps
		if (sideBuffer.channelLayout.count > 2) {
			int minChannels = 2;
			if (sideBuffer.channelLayout.count > 3 && hasAerials) {
				// TODO: This probably isn't a reliable way to use aerials. Probably do something smarter.
				minChannels = 3;
			}
			// Get channel amps in descending order
			qsort(channelsStart, sideBuffer.channelLayout.count, sizeof(channelMetadata), compareChannelMetadataAmp);
			qsort(channelsEnd, sideBuffer.channelLayout.count, sizeof(channelMetadata), compareChannelMetadataAmp);

			float ampMaxRangeStart = channelsStart[0].amp;
			float ampMaxRangeEnd = channelsEnd[0].amp;
			float ampMinRangeStart = channelsStart[minChannels-1].amp;
			float ampMinRangeEnd = channelsEnd[minChannels-1].amp;
			totalMagnitudeStart = 0.0f;
			totalMagnitudeEnd = 0.0f;
			for (uint8_t i = 0; i < sideBuffer.channelLayout.count; i++) {
				channelsStart[i].amp = azaLinstepf(channelsStart[i].amp, ampMinRangeStart, ampMaxRangeStart) + allChannelAddAmpStart / channelCountDenominator;
				channelsEnd[i].amp = azaLinstepf(channelsEnd[i].amp, ampMinRangeEnd, ampMaxRangeEnd) + allChannelAddAmpEnd / channelCountDenominator;
				totalMagnitudeStart += channelsStart[i].amp;
				totalMagnitudeEnd += channelsEnd[i].amp;
			}

			// Put the amps back into channel order
			qsort(channelsStart, sideBuffer.channelLayout.count, sizeof(channelMetadata), compareChannelMetadataChannel);
			qsort(channelsEnd, sideBuffer.channelLayout.count, sizeof(channelMetadata), compareChannelMetadataChannel);
		}

	#if PRINT_CHANNEL_AMPS || PRINT_CHANNEL_DELAYS
		if (repeatCount == 0) {
			AZA_LOG_INFO("\n");
		}
	#endif
		// Calculate final channel amps by factoring in minAmp, and put each channel into sideBuffer for further processing
		for (uint8_t c = 0; c < sideBuffer.channelLayout.count; c++) {
			float ampStart = srcAmpStart;
			float ampEnd = srcAmpEnd;
			if (sideBuffer.channelLayout.positions[c] != AZA_POS_SUBWOOFER) {
				ampStart *= (channelsStart[c].amp / totalMagnitudeStart) * (1.0f - minAmp) + minAmp / channelCountDenominator;
				ampEnd *= (channelsEnd[c].amp / totalMagnitudeEnd) * (1.0f - minAmp) + minAmp / channelCountDenominator;
			}
	#if PRINT_CHANNEL_AMPS
			if (repeatCount == 0) {
				AZA_LOG_INFO("Channel %u amp: %f\n", (uint32_t)c, ampStart);
			}
	#endif
	#if PRINT_CHANNEL_DELAYS
			if (repeatCount == 0) {
				AZA_LOG_INFO("Channel %u delay: %f\n", (uint32_t)c, channelDelayStart[c]);
			}
	#endif
			azaBuffer dstChannelBuffer = azaBufferOneChannel(&sideBuffer, c);
			azaBufferMixFade(&dstChannelBuffer, 1.0f, 1.0f, NULL, &srcChannelBuffer, ampStart, ampEnd, NULL);
		}

		if (data->config.doFilter) {
			// TODO: Probably let the filter cutoff change smoothly
			if (data->config.usePerChannelFilter) {
				for (uint8_t c = 0; c < sideBuffer.channelLayout.count; c++) {
					channelData->filter.config.channelFrequencyOverride[c] = azaSpatializeGetFilterCutoff(delayStart_ms, channelsStart[c].dot);
				}
			} else {
				channelData->filter.config.frequency = azaSpatializeGetFilterCutoff(avgDelayStart_ms, 1.0f);
			}
			err = azaFilterProcess(&data->channelData[srcC].filter, &sideBuffer, &sideBuffer, flags);
			if AZA_UNLIKELY(err) goto error;
		}

		if (data->config.doDoppler || data->config.usePerChannelDelay) {
			// We need to process the delay
			float startDelay_ms[AZA_MAX_CHANNEL_POSITIONS];
			float endDelay_ms[AZA_MAX_CHANNEL_POSITIONS];
			if (data->config.usePerChannelDelay) {
				for (uint8_t c = 0; c < sideBuffer.channelLayout.count; c++) {
					azaVec3 earPos = azaMulVec3Scalar(earNormal[c], earDistance);
					startDelay_ms[c] = minDelay_ms + azaVec3Norm(azaSubVec3(srcPosStart, earPos)) / world->speedOfSound * 1000.0f;
					endDelay_ms[c] = minDelay_ms + azaVec3Norm(azaSubVec3(srcPosEnd, earPos)) / world->speedOfSound * 1000.0f;
				}
			} else {
				for (uint8_t c = 0; c < sideBuffer.channelLayout.count; c++) {
					startDelay_ms[c] = avgDelayStart_ms;
					endDelay_ms[c] = avgDelayEnd_ms;
				}
			}
			azaDelayDynamicSetRamps(&channelData->delay, sideBuffer.channelLayout.count, startDelay_ms, endDelay_ms, sideBuffer.frames, sideBuffer.samplerate);
			err = azaDelayDynamicProcess(&channelData->delay, &sideBuffer, &sideBuffer, flags);
			if AZA_UNLIKELY(err) goto error;
		}

		azaBufferMix(dst, 1.0f, &sideBuffer, 1.0f);
	}
#if PRINT_CHANNEL_AMPS || PRINT_CHANNEL_DELAYS
	repeatCount = (repeatCount + 1) % 10;
#endif

error:
	azaPopSideBuffers(2);
	return err;
}

azaDSPSpecs azaSpatializeGetSpecs(void *dsp, uint32_t samplerate) {
	azaSpatialize *data = dsp;
	azaDSPSpecs specs = {0};
	if (data->config.doDoppler || data->config.usePerChannelDelay) {
		specs = azaDSPGetSpecs(&data->channelData[0].delay.header, samplerate);
	}
	return specs;
}



// Utilities



void azaSpatializeSetRamps(azaSpatialize *data, uint8_t numChannels, azaSpatializeChannelConfig start[], azaSpatializeChannelConfig end[], uint32_t frames, uint32_t samplerate) {
	data->config.targetFollowTime_ms = aza_samples_to_ms((float)frames, (float)samplerate);
	data->config.numSrcChannelsActive = numChannels;
	for (uint8_t c = 0; c < numChannels; c++) {
		azaFollowerLinear3DJump(&data->channelData[c].position, start[c].target.position);
		data->config.channels[c].target.position = end[c].target.position;
		azaFollowerLinearJump(&data->channelData[c].amplitude, start[c].target.amplitude);
		data->config.channels[c].target.amplitude = end[c].target.amplitude;
	}
}