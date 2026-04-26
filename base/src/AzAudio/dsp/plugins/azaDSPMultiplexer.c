/*
	File: azaDSPMultiplexer.c
	Author: Philip Haynes
*/

#include "azaDSPMultiplexer.h"

#include "../../error.h"


static const azaDSPFuncs azaDSPMultiplexerFuncs = {
	.fp_makeDefault = azaDSPMultiplexerMakeDefault,
	.fp_makeDuplicate = azaDSPMultiplexerMakeDuplicate,
	.fp_copyConfig = azaDSPMultiplexerCopyConfig,
	.fp_getSpecs = azaDSPMultiplexerGetSpecs,
	.fp_process = azaDSPMultiplexerProcess,
	.fp_free = azaDSPMultiplexerFree,
	.fp_draw = NULL,
};

const azaDSP azaDSPMultiplexerHeader = {
	.header =  {
		.size    = sizeof(azaDSPMultiplexer),
		.version = 1,
		.owned   = false,
		.bypass  = false,
	},
	.processMetadata = { 0 }, // ZII
	.guiMetadata = {
		.name             = "DSP Multiplexer",
		.selected         = 0,
		.drawTargetWidth  = 0.0f,
		.drawCurrentWidth = 0.0f,
	},
	.pFuncs = &azaDSPMultiplexerFuncs,
};

void azaDSPMultiplexerInit(azaDSPMultiplexer *data) {
	data->dsp = azaDSPMultiplexerHeader;
	azaDSPChainInit(&data->origin, 0);
	azaMutexInit(&data->mutex);
}

void azaDSPMultiplexerDeinit(azaDSPMultiplexer *data) {
	azaDSPChainDeinit(&data->origin);
	azaMutexDeinit(&data->mutex);
	for (uint32_t i = 0; i < data->instances.count; i++) {
		azaDSPChainDeinit(&data->instances.data[i].chain);
	}
	AZA_DA_DEINIT(data->instances);
}

azaDSPMultiplexer* azaDSPMultiplexerMake() {
	azaDSPMultiplexer *result = aza_calloc(1, sizeof(azaDSPMultiplexer));
	if (result) {
		azaDSPMultiplexerInit(result);
	}
	return result;
}

void azaDSPMultiplexerFree(azaDSP *dsp) {
	azaDSPMultiplexerDeinit((azaDSPMultiplexer*)dsp);
	aza_free(dsp);
}

azaDSP* azaDSPMultiplexerMakeDefault() {
	return (azaDSP*)azaDSPMultiplexerMake();
}

azaDSP* azaDSPMultiplexerMakeDuplicate(azaDSP *src) {
	azaDSPMultiplexer *result = azaDSPMultiplexerMake();
	if (!result) {
		return NULL;
	}
	azaDSPMultiplexer *data = (azaDSPMultiplexer*)src;
	if (azaDSPChainEnsureParity(&result->origin, &data->origin)) {
		azaDSPMultiplexerFree((azaDSP*)result);
		return NULL;
	}
	return (azaDSP*)result;
}

int azaDSPMultiplexerCopyConfig(azaDSP *dst, azaDSP *src) {
	azaDSPMultiplexer *dataDst = (azaDSPMultiplexer*)dst;
	azaDSPMultiplexer *dataSrc = (azaDSPMultiplexer*)src;
	return azaDSPChainEnsureParity(&dataDst->origin, &dataSrc->origin);
}

int azaDSPMultiplexerProcess(void *dsp, azaBuffer *dst, azaBuffer *src, uint32_t flags) {
	azaDSPMultiplexer *data = (azaDSPMultiplexer*)dsp;
	if (data->instances.count == 0) {
		azaBufferZero(dst);
		return AZA_SUCCESS;
	}
	uint8_t numSideBuffers = 1;
	azaBuffer processingBuffer, sideBufferSrc;
	processingBuffer = azaPushSideBufferCopyZero(dst);
	int result = AZA_SUCCESS;
	if (azaBuffersOverlap(dst, src)) {
		sideBufferSrc = azaPushSideBufferCopy(src);
		numSideBuffers++;
		src = &sideBufferSrc;
	}
	azaBufferZero(dst);

	azaMutexLock(&data->mutex);

	for (uint32_t i = 0; i < data->instances.count; i++) {
		azaDSPMultiplexerInstance *instance = &data->instances.data[i];
		if (!instance->active) continue;
		if (!instance->initted) {
			result = azaDSPChainInitDuplicate(&instance->chain, &data->origin);
			if (result) {
				goto error;
			}
			instance->initted = true;
		}
		result = azaDSPChainEnsureParity(&instance->chain, &data->origin);
		if (result) {
			goto error;
		}
		azaBufferZero(&processingBuffer);
		result = azaDSPChainProcess(&instance->chain, &processingBuffer, src, flags);
		if (result) {
			goto error;
		}
		azaBufferMix(dst, 1.0f, &processingBuffer, 1.0f);
	}
error:
	azaMutexUnlock(&data->mutex);
	azaPopSideBuffers(numSideBuffers);
	return result;
}

azaDSPSpecs azaDSPMultiplexerGetSpecs(azaDSP *dsp, uint32_t samplerate) {
	azaDSPMultiplexer *data = (azaDSPMultiplexer*)dsp;
	return azaDSPChainGetSpecs(&data->origin, samplerate);
}





























