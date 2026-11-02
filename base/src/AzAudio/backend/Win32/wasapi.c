/*
	File: wasapi.c
	Author: Philip Haynes
*/

#include "../../AzAudio.h"
#include "../../dsp/azaResampler.h"
#include "../backend.h"
#include "../interface.h"
#include "../../error.h"

#include "../threads.h"
#include "platform_util.h"

#define byte MS_byte
#include <Mmdeviceapi.h>
#include <mmreg.h>
#include <functiondiscoverykeys_devpkey.h>
#include <Ks.h>
#include <Ksmedia.h>
#include <stringapiset.h>
#include <Audioclient.h>
#undef byte

#include <assert.h>

// Microsoft APIs are really stinky and smelly and dirty and leaving their dirt all over everything they touch.
#ifdef interface
#undef interface
#endif
#ifdef ERROR
#undef ERROR
#endif

// TODO: Maybe make this configurable.
#define AZA_BACKEND_RESAMPLING_WINDOW 25

#define backendResamplingKernel (azaKernelDefaultLanczos[AZA_BACKEND_RESAMPLING_WINDOW-1])

// This is kinda pretty
#define GUID_FORMAT_STR "%08x-%04hx-%04hx-%02hhx%02hhx-%02hhx%02hhx%02hhx%02hhx%02hhx%02hhx"
#define GUID_ARGS(guid) (guid).Data1, (guid).Data2, (guid).Data3, (guid).Data4[0], (guid).Data4[1], (guid).Data4[2], (guid).Data4[3], (guid).Data4[4], (guid).Data4[5], (guid).Data4[6], (guid).Data4[7]

#define CHECK_RESULT(description, onFail) if (FAILED(hr)) {\
	AZA_LOG_ERR(description " failed:%s\n", HRESULT_String(hr));\
	onFail;\
}
#define SAFE_RELEASE(pSomething) if ((pSomething)) { (pSomething)->lpVtbl->Release((pSomething)); (pSomething) = NULL; }
#define SAFE_FREE(pSomething) if ((pSomething)) { aza_free((void*)(pSomething)); (pSomething) = NULL; }

// Don't forget to aza_free() the return value later.
static char* wstrToCstr(WCHAR *wstr) {
	char *cstr;
	int neededSize = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, NULL, 0, NULL, NULL);
	cstr = aza_malloc(neededSize);
	WideCharToMultiByte(CP_UTF8, 0, wstr, -1, cstr, neededSize, NULL, NULL);
	return cstr;
}

#define PROPERTY_STORE_GET(pPropertyStore, key, VT_KIND, onSuccess, onFail) {\
	PROPVARIANT propVariant;\
	PropVariantInit(&propVariant);\
	hr = (pPropertyStore)->lpVtbl->GetValue((pPropertyStore), &(key), &propVariant);\
	CHECK_RESULT("PropertyStore::GetValue(" #key ")\n", onFail);\
	if (propVariant.vt == (VT_KIND)) {\
		onSuccess;\
		PropVariantClear(&propVariant);\
	} else {\
		AZA_LOG_ERR("PropertyStore::GetValue(" #key ") vt kind is not " #VT_KIND " (was %hu)\n", propVariant.vt);\
		PropVariantClear(&propVariant);\
		onFail;\
	}\
}



typedef struct azaDeviceInfo {
	IMMDevice *pDevice;
	// I'm buying clothes at the Property Store
	IPropertyStore *pPropertyStore;
	const WCHAR *idWStr;
	enum azaFormFactor formFactor;
	// available may become false if a device is unplugged, and later true again if it's plugged back in. We don't delete them.
	bool available, input, output;
	const char *name;
	uint32_t channels;
	uint32_t sampleBitDepth;
	uint32_t samplerate;
} azaDeviceInfo;

typedef struct azaStreamData {
	bool isActive;
	azaStream *stream;
	azaDeviceInfo *deviceInfo;
	uint32_t deviceBufferFrames;
	azaBuffer conversionBuffer;
	// If resampling is necessary, we first resample
	azaResampler resampler;
	azaBuffer resamplingBuffer;
	// If channel mixing is necessary, we do that second
	azaChannelMatrix channelMatrix;
	azaBuffer channelMixBuffer;
	azaBuffer processingBuffer;

	IAudioClient *pAudioClient;
	union {
		IAudioRenderClient *pRenderClient;
		IAudioCaptureClient *pCaptureClient;
	};
	WAVEFORMATEXTENSIBLE waveFormatExtensible;
} azaStreamData;

// ONLY CALL THIS FROM THE SOUND THREAD
// These COM objects can only be Released from the thread they're created on.
static void azaStreamDataDeinit(azaStreamData *streamData) {
	if (streamData->pAudioClient) {
		HRESULT hr = streamData->pAudioClient->lpVtbl->Stop(streamData->pAudioClient);
		CHECK_RESULT("IAudioClient::Stop", (void)0);
		SAFE_RELEASE(streamData->pAudioClient);
		if (streamData->stream) {
			if (streamData->stream->deviceInterface == AZA_OUTPUT) {
				SAFE_RELEASE(streamData->pRenderClient);
			} else {
				SAFE_RELEASE(streamData->pCaptureClient);
			}
		}
	}
	azaResamplerDeinit(&streamData->resampler);
	azaChannelMatrixDeinit(&streamData->channelMatrix);
	azaBufferDeinit(&streamData->resamplingBuffer, false);
	azaBufferDeinit(&streamData->conversionBuffer, false);
	azaBufferDeinit(&streamData->channelMixBuffer, false);
	azaBufferDeinit(&streamData->processingBuffer, false);
}



// Globals



static IMMDeviceEnumerator *pEnumerator = NULL;
static IMMDeviceCollection *pDeviceCollection = NULL;

static HRESULT STDMETHODCALLTYPE immncQueryInterface(IMMNotificationClient *This, REFIID riid, void **ppvObject);
static ULONG STDMETHODCALLTYPE immncAddRef(IMMNotificationClient *This);
static ULONG STDMETHODCALLTYPE immncRelease(IMMNotificationClient *This);
static HRESULT STDMETHODCALLTYPE immncOnDeviceStateChanged(IMMNotificationClient *This, LPCWSTR pwstrDeviceId, DWORD dwNewState);
static HRESULT STDMETHODCALLTYPE immncOnDeviceAdded(IMMNotificationClient *This, LPCWSTR pwstrDeviceId);
static HRESULT STDMETHODCALLTYPE immncOnDeviceRemoved(IMMNotificationClient *This, LPCWSTR pwstrDeviceId);
static HRESULT STDMETHODCALLTYPE immncOnDefaultDeviceChanged(IMMNotificationClient *This, EDataFlow flow, ERole role, LPCWSTR pwstrDefaultDeviceId);
static HRESULT STDMETHODCALLTYPE immncOnPropertyValueChanged(IMMNotificationClient *This, LPCWSTR pwstrDeviceId, const PROPERTYKEY key);
static IMMNotificationClientVtbl immNotificationClientVtbl = {
	.QueryInterface = immncQueryInterface,
	.AddRef = immncAddRef,
	.Release = immncRelease,
	.OnDeviceStateChanged = immncOnDeviceStateChanged,
	.OnDeviceAdded = immncOnDeviceAdded,
	.OnDeviceRemoved = immncOnDeviceRemoved,
	.OnDefaultDeviceChanged = immncOnDefaultDeviceChanged,
	.OnPropertyValueChanged = immncOnPropertyValueChanged,
};
static IMMNotificationClient immNotificationClient = { .lpVtbl=&immNotificationClientVtbl };



#define AZA_MAX_DEVICES 256

static azaThread thread = {0};
static azaMutex mutex = {0};
static bool shouldQuit = false;
static bool hadInitError = false;
static bool initCompleted = false;

static azaDeviceInfo device[AZA_MAX_DEVICES];
static size_t deviceCount = 0;

static azaDeviceInfo *deviceOutput[AZA_MAX_DEVICES];
static size_t deviceOutputCount = 0;
static azaDeviceInfo *deviceInput[AZA_MAX_DEVICES];
static size_t deviceInputCount = 0;

static azaDeviceInfo *defaultOutputDevice = NULL;
static azaDeviceInfo *defaultInputDevice = NULL;



static struct {
	azaStreamData **data;
	uint32_t count;
	uint32_t capacity;
} streams = {0};

static azaStreamData* azaStreamDataMake() {
	azaStreamData *result = aza_calloc(1, sizeof(azaStreamData));
	assert(result);
	azaResamplerInit(&result->resampler, &backendResamplingKernel);
	AZA_DA_APPEND(streams, result, assert(false));
	return result;
}

// ONLY CALL THIS FROM THE SOUND THREAD
static void azaStreamDataFree(azaStreamData *streamData) {
	uint32_t index = UINT32_MAX;
	for (uint32_t i = 0; i < streams.count; i++) {
		if (streams.data[i] == streamData) {
			index = i;
			break;
		}
	}
	azaStreamDataDeinit(streamData);
	aza_free(streamData);
	if (index != UINT32_MAX) {
		AZA_DA_ERASE(streams, index, 1);
	}
}



// Sound Thread Execution



typedef struct SoundThreadQueueAction {
	int (*fp_Action)(void *parameter);
	void *parameter;
	int ret_val;
	bool completed; // Action completed
	bool occupied; // Queue slot occupied
	bool pleaseFreeParameter; // Parameter will be aza_free'd upon completion
	bool autoClear; // We set occupied to false immediately upon completion rather than waiting for synchronization
} SoundThreadQueueAction;

#define STQA_CAPACITY 64
static SoundThreadQueueAction stqaQueue[STQA_CAPACITY] = {0};
static azaMutex stqaMutex;

static void executeSoundThreadQueueActions() {
	azaMutexLock(&stqaMutex);
	for (int i = 0; i < STQA_CAPACITY; i++) {
		if (stqaQueue[i].occupied && !stqaQueue[i].completed) {
			int (*fp_Action)(void*) = stqaQueue[i].fp_Action;
			void *parameter = stqaQueue[i].parameter;
			azaMutexUnlock(&stqaMutex);
			int ret_val = fp_Action(parameter);
			azaMutexLock(&stqaMutex);
			stqaQueue[i].ret_val = ret_val;
			stqaQueue[i].completed = true;
			if (stqaQueue[i].pleaseFreeParameter) {
				aza_free(stqaQueue[i].parameter);
			}
			if (stqaQueue[i].autoClear) {
				stqaQueue[i].occupied = false;
			}
		}
	}
	azaMutexUnlock(&stqaMutex);
}

// Queue the function for execution on the sound thread but don't wait for it to finish.
// if ownedParameter is true, then once execution is finished, we'll call aza_free on the parameter pointer
// returns the index in the queue
static int soundThreadQueue(int (*fp_Action)(void*), void *parameter, bool ownedParameter, bool autoClear) {
	azaMutexLock(&stqaMutex);
	int queue_index = -1;
	while (true) {
		for (int i = 0; i < STQA_CAPACITY; i++) {
			if (!stqaQueue[i].occupied) {
				queue_index = i;
				break;
			}
		}
		if (queue_index != -1) {
			break;
		}
		azaMutexUnlock(&stqaMutex);
		azaThreadYield();
		azaMutexLock(&stqaMutex);
	}
	stqaQueue[queue_index] = (SoundThreadQueueAction) {
		.fp_Action = fp_Action,
		.parameter = parameter,
		.completed = false,
		.occupied = true,
		.pleaseFreeParameter = ownedParameter,
		.autoClear = autoClear,
	};
	azaMutexUnlock(&stqaMutex);
	return queue_index;
}

// Execute the function on the sound thread, wait for it to finish, and return the result.
static int soundThreadExecute(int (*fp_Action)(void*), void *parameter) {
	azaMutexLock(&stqaMutex);
	int queue_index = soundThreadQueue(fp_Action, parameter, false, false);
	do {
		azaMutexUnlock(&stqaMutex);
		azaThreadYield();
		azaMutexLock(&stqaMutex);
	} while (!stqaQueue[queue_index].completed);
	int result = stqaQueue[queue_index].ret_val;
	stqaQueue[queue_index].occupied = false;
	azaMutexUnlock(&stqaMutex);
	return result;
}



// Utils



static azaChannelLayout azaGetChannelLayoutFromMask(uint8_t count, DWORD mask) {
	#define CHECK_CHANNEL(msPos, myPos)\
	if (mask & (msPos)) {\
		layout.positions[layout.count++] = myPos;\
	}
	azaChannelLayout layout;
	layout.count = 0;
	CHECK_CHANNEL(SPEAKER_FRONT_LEFT, AZA_POS_LEFT_FRONT);
	CHECK_CHANNEL(SPEAKER_FRONT_RIGHT, AZA_POS_RIGHT_FRONT);
	CHECK_CHANNEL(SPEAKER_FRONT_CENTER, AZA_POS_CENTER_FRONT);
	CHECK_CHANNEL(SPEAKER_LOW_FREQUENCY, AZA_POS_SUBWOOFER);
	CHECK_CHANNEL(SPEAKER_BACK_LEFT, AZA_POS_LEFT_BACK);
	CHECK_CHANNEL(SPEAKER_BACK_RIGHT, AZA_POS_RIGHT_BACK);
	CHECK_CHANNEL(SPEAKER_FRONT_LEFT_OF_CENTER, AZA_POS_LEFT_CENTER_FRONT);
	CHECK_CHANNEL(SPEAKER_FRONT_RIGHT_OF_CENTER, AZA_POS_RIGHT_CENTER_FRONT);
	CHECK_CHANNEL(SPEAKER_BACK_CENTER, AZA_POS_CENTER_BACK);
	CHECK_CHANNEL(SPEAKER_SIDE_LEFT, AZA_POS_LEFT_SIDE);
	CHECK_CHANNEL(SPEAKER_SIDE_RIGHT, AZA_POS_RIGHT_SIDE);
	CHECK_CHANNEL(SPEAKER_TOP_CENTER, AZA_POS_CENTER_TOP);
	CHECK_CHANNEL(SPEAKER_TOP_FRONT_LEFT, AZA_POS_LEFT_FRONT_TOP);
	CHECK_CHANNEL(SPEAKER_TOP_FRONT_CENTER, AZA_POS_CENTER_FRONT_TOP);
	CHECK_CHANNEL(SPEAKER_TOP_FRONT_RIGHT, AZA_POS_RIGHT_FRONT_TOP);
	CHECK_CHANNEL(SPEAKER_TOP_BACK_LEFT, AZA_POS_LEFT_BACK_TOP);
	CHECK_CHANNEL(SPEAKER_TOP_BACK_CENTER, AZA_POS_CENTER_BACK_TOP);
	CHECK_CHANNEL(SPEAKER_TOP_BACK_RIGHT, AZA_POS_RIGHT_BACK_TOP);
	#undef CHECK_CHANNEL
	assert(layout.count == count);
	return layout;
}

static azaChannelLayout azaGetChannelLayoutFromStreamDevice(azaStreamData *data) {
	azaChannelLayout result = azaGetChannelLayoutFromMask((uint8_t)data->waveFormatExtensible.Format.nChannels, data->waveFormatExtensible.dwChannelMask);
	result.formFactor = data->deviceInfo->formFactor;
	return result;
}

static DWORD azaGetChannelMaskFromChannelPosition(uint8_t position) {
	switch (position) {
		case AZA_POS_LEFT_FRONT: return SPEAKER_FRONT_LEFT;
		case AZA_POS_RIGHT_FRONT: return SPEAKER_FRONT_RIGHT;
		case AZA_POS_CENTER_FRONT: return SPEAKER_FRONT_CENTER;
		case AZA_POS_SUBWOOFER: return SPEAKER_LOW_FREQUENCY;
		case AZA_POS_LEFT_BACK: return SPEAKER_BACK_LEFT;
		case AZA_POS_RIGHT_BACK: return SPEAKER_BACK_RIGHT;
		case AZA_POS_LEFT_CENTER_FRONT: return SPEAKER_FRONT_LEFT_OF_CENTER;
		case AZA_POS_RIGHT_CENTER_FRONT: return SPEAKER_FRONT_RIGHT_OF_CENTER;
		case AZA_POS_CENTER_BACK: return SPEAKER_BACK_CENTER;
		case AZA_POS_LEFT_SIDE: return SPEAKER_SIDE_LEFT;
		case AZA_POS_RIGHT_SIDE: return SPEAKER_SIDE_RIGHT;
		case AZA_POS_CENTER_TOP: return SPEAKER_TOP_CENTER;
		case AZA_POS_LEFT_FRONT_TOP: return SPEAKER_TOP_FRONT_LEFT;
		case AZA_POS_CENTER_FRONT_TOP: return SPEAKER_TOP_FRONT_CENTER;
		case AZA_POS_RIGHT_FRONT_TOP: return SPEAKER_TOP_FRONT_RIGHT;
		case AZA_POS_LEFT_BACK_TOP: return SPEAKER_TOP_BACK_LEFT;
		case AZA_POS_CENTER_BACK_TOP: return SPEAKER_TOP_BACK_CENTER;
		case AZA_POS_RIGHT_BACK_TOP: return SPEAKER_TOP_BACK_RIGHT;
		default: return 0;
	}
}

static DWORD azaGetChannelMaskFromChannelLayout(azaChannelLayout layout) {
	DWORD mask = 0;
	for (uint8_t i = 0; i < layout.count; i++) {
		mask |= azaGetChannelMaskFromChannelPosition(layout.positions[i]);
	}
	return mask;
}

// TODO: Move the functionality of the following two functions into someplace not backend-specific

// deviceBuffer should have the same channel layout and frame count as the dst buffer
static void azaConvertFromNativeWASAPI(WAVEFORMATEXTENSIBLE *wfx, azaBuffer *dst, uint8_t *deviceBuffer) {
	uint32_t numSamples = dst->frames * dst->channelLayout.count;
	if (IsEqualGUID(&wfx->SubFormat, &KSDATAFORMAT_SUBTYPE_PCM)) {
		switch (wfx->Format.wBitsPerSample) {
			case 8: {
				uint8_t *src = (uint8_t*)deviceBuffer;
				for (uint32_t i = 0; i < numSamples; i++) {
					// 1 is -1.0f, 128 is 0.0f, 255 1.0f
					dst->pSamples[i] = ((float)src[i] - 1.0f) / 127.0f - 1.0f;
				}
			} break;
			case 16: {
				int16_t *src = (int16_t*)deviceBuffer;
				for (uint32_t i = 0; i < numSamples; i++) {
					dst->pSamples[i] = (float)src[i] / 32767.0f;
				}
			} break;
			case 24: {
				uint8_t *src = deviceBuffer;
				for (uint32_t i = 0; i < numSamples; i++) {
					dst->pSamples[i] = (float)azaSignExtend24Bit(*(uint32_t*)&src[i*3]) / 8388607.0f;
				}
			} break;
			case 32: {
				int32_t *src = (int32_t*)deviceBuffer;
				for (uint32_t i = 0; i < numSamples; i++) {
					dst->pSamples[i] = (float)src[i] / 2147483647.0f;
				}
			} break;
			default:
				assert(0);
				break;
		}
	} else {
		assert(IsEqualGUID(&wfx->SubFormat, &KSDATAFORMAT_SUBTYPE_IEEE_FLOAT));
		switch (wfx->Format.wBitsPerSample) {
			case 32: {
				float *src = (float*)deviceBuffer;
				memcpy(dst->pSamples, src, numSamples * sizeof(float));
			} break;
			case 64: {
				// NOTE: This is probably never a thing, but whatever
				double *src = (double*)deviceBuffer;
				for (uint32_t i = 0; i < numSamples; i++) {
					dst->pSamples[i] = (float)src[i];
				}
			} break;
			default:
				assert(0);
				break;
		}
	}
}

// deviceBuffer should have the same channel layout and frame count as the src buffer
static void azaConvertToNativeWASAPI(WAVEFORMATEXTENSIBLE *wfx, azaBuffer *src, uint8_t *deviceBuffer) {
	uint32_t numSamples = src->frames * src->channelLayout.count;
	if (IsEqualGUID(&wfx->SubFormat, &KSDATAFORMAT_SUBTYPE_PCM)) {
		switch (wfx->Format.wBitsPerSample) {
			case 8: {
				uint8_t *dst = (uint8_t*)deviceBuffer;
				for (uint32_t i = 0; i < numSamples; i++) {
					// 1 is -1.0f, 128 is 0.0f, 255 1.0f
					dst[i] = (uint8_t)(azaClampf(src->pSamples[i],-1.0f,1.0f) * 127.0f + 128.0f);
				}
			} break;
			case 16: {
				int16_t *dst = (int16_t*)deviceBuffer;
				for (uint32_t i = 0; i < numSamples; i++) {
					dst[i] = (int16_t)(azaClampf(src->pSamples[i],-1.0f,1.0f) * 32767.0f);
				}
			} break;
			case 24: {
				uint8_t *dst = deviceBuffer;
				for (uint32_t i = 0; i < numSamples; i++) {
					uint32_t value = (uint32_t)(int32_t)(azaClampf(src->pSamples[i],-1.0f,1.0f) * 8388607.0f);
					dst[i*3+0] = (value >>  0) & 0xff;
					dst[i*3+1] = (value >>  8) & 0xff;
					dst[i*3+2] = (value >> 16) & 0xff;
				}
			} break;
			case 32: {
				int32_t *dst = (int32_t*)deviceBuffer;
				for (uint32_t i = 0; i < numSamples; i++) {
					// 2147483648 is the closest f32 can get to 2147483647, so just handle integer overflow with 64-bit ints instead of lowering our output to 2147483520
					dst[i] = (int32_t)AZA_CLAMP((int64_t)(src->pSamples[i] * 2147483648.0f), -2147483647ll, 2147483647ll);
				}
			} break;
			default:
				assert(0);
				break;
		}
	} else {
		assert(IsEqualGUID(&wfx->SubFormat, &KSDATAFORMAT_SUBTYPE_IEEE_FLOAT));
		switch (wfx->Format.wBitsPerSample) {
			case 32: {
				float *dst = (float*)deviceBuffer;
				memcpy(dst, src->pSamples, numSamples * sizeof(float));
			} break;
			case 64: {
				// NOTE: Is this ever even a thing? This much precision is surely never needed...
				double *dst = (double*)deviceBuffer;
				for (uint32_t i = 0; i < numSamples; i++) {
					dst[i] = (double)src->pSamples[i];
				}
			} break;
			default:
				assert(0);
				break;
		}
	}
}

static bool WaveFormatExtensibleIsFloat32(WAVEFORMATEXTENSIBLE *wfe) {
	return IsEqualGUID(&wfe->SubFormat, &KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) && wfe->Format.wBitsPerSample == 32;
}

static int azaChannelMatrixUpdate(azaChannelMatrix *data, azaChannelLayout srcLayout, azaChannelLayout dstLayout) {
	if (data->inputs != srcLayout.count || data->outputs != dstLayout.count) {
		// TODO: Is it possible for the counts to stay the same and the layouts to change?
		// On the user side the channels don't necessarily have to be in backend order
		// But the backend channel order is fixed
		// So this could only come up if users were to change the stream channel layout after creating one, which is technically possible, but likely ill-advised due to how many places would need to be updated to support this. Really I may just need to support that, but it's not actively encouraged by the API.
		azaChannelMatrixDeinit(data);
		int err = azaChannelMatrixInit(data, srcLayout.count, dstLayout.count);
		if (err) return err;
		azaChannelMatrixGenerateRoutingFromLayouts(data, srcLayout, dstLayout);
	}
	return AZA_SUCCESS;
}

static azaDeviceInfo* azaFindDefaultDevice(azaDeviceInfo *devicePool[], size_t deviceCount, EDataFlow dataFlow, const char *poolTag) {
	azaDeviceInfo *result = NULL;
	HRESULT hr;
	IMMDevice *pDevice = NULL;
	IPropertyStore *pPropertyStore = NULL;
	char *name = NULL;
	hr = pEnumerator->lpVtbl->GetDefaultAudioEndpoint(pEnumerator, dataFlow, eConsole, &pDevice);
	CHECK_RESULT("GetDefaultAudioEndpoint", goto error);
	hr = pDevice->lpVtbl->OpenPropertyStore(pDevice, STGM_READ, &pPropertyStore);
	CHECK_RESULT("OpenPropertyStore", goto error);
	PROPERTY_STORE_GET(pPropertyStore, PKEY_Device_FriendlyName, VT_LPWSTR,
		// onSuccess:
		name = wstrToCstr(propVariant.pwszVal),
		// onFail:
		goto error
	);
	aza_str_strip(name, name, strlen(name)+1);
	// Check by name
	for (size_t i = 0; i < deviceCount; i++) {
		if (!devicePool[i]->available) continue;
		if (strcmp(devicePool[i]->name, name) == 0) {
			result = devicePool[i];
			break;
		}
	}
	if (result == NULL) {
		AZA_LOG_ERR("Didn't find the default %s device in our list!\n", poolTag);
		goto error;
	} else {
		AZA_LOG_INFO("Default %s device: \"%s\"\n", poolTag, result->name);
	}
error:
	SAFE_FREE(name);
	SAFE_RELEASE(pPropertyStore);
	SAFE_RELEASE(pDevice);
	return result;
#undef FAIL_ACTION
}



// Devices



// parameter is a single azaDeviceInfo*
static int azaDeviceUpdateName(void *parameter) {
	azaDeviceInfo *dstDevice = parameter;
	HRESULT hr;
	char *name;
	PROPERTY_STORE_GET(dstDevice->pPropertyStore, PKEY_Device_FriendlyName, VT_LPWSTR,
		// onSuccess:
		name = wstrToCstr(propVariant.pwszVal),
		// onFail:
		return AZA_ERROR_BACKEND_ERROR
	);
	aza_str_strip(name, name, strlen(name)+1);
	SAFE_FREE(dstDevice->name);
	dstDevice->name = name;
	return AZA_SUCCESS;
}

// parameter is a single azaDeviceInfo*
static int azaDeviceUpdateDeviceFormat(void *parameter) {
	azaDeviceInfo *dstDevice = parameter;
	HRESULT hr;
	WAVEFORMATEXTENSIBLE waveFormatEx;
	PROPERTY_STORE_GET(dstDevice->pPropertyStore, PKEY_AudioEngine_DeviceFormat, VT_BLOB,
		// onSuccess:
		memcpy(&waveFormatEx, propVariant.blob.pBlobData, AZA_MIN(sizeof(waveFormatEx), propVariant.blob.cbSize)),
		// onFail:
		return AZA_ERROR_BACKEND_ERROR
	);
	dstDevice->channels = (uint32_t)waveFormatEx.Format.nChannels;
	dstDevice->sampleBitDepth = (uint32_t)waveFormatEx.Format.wBitsPerSample;
	dstDevice->samplerate = (uint32_t)waveFormatEx.Format.nSamplesPerSec;
	return AZA_SUCCESS;
}

// Expects dstDevice to be fully initted with an open pPropertyStore
// parameter is a single azaDeviceInfo*
static int azaDeviceUpdateFormFactor(void *parameter) {
	azaDeviceInfo *dstDevice = parameter;
	HRESULT hr = S_OK;
	if (dstDevice->output) {
		// This is apparently not necessarily accurate, so we'll see if we can match the name for headphones right below
		PROPERTY_STORE_GET(dstDevice->pPropertyStore, PKEY_AudioEndpoint_FormFactor, VT_UI4,
			// onSuccess:
			switch (propVariant.uintVal) {
				case RemoteNetworkDevice:
				case Speakers:
				case LineLevel:
				case Microphone:
				case Handset:
				case UnknownDigitalPassthrough:
				case SPDIF:
				case DigitalAudioDisplayDevice:
				case UnknownFormFactor:
					dstDevice->formFactor = AZA_FORM_FACTOR_SPEAKERS;
					AZA_LOG_TRACE("%s is Speakers\n", dstDevice->name);
					break;
				case Headphones:
				case Headset:
					dstDevice->formFactor = AZA_FORM_FACTOR_HEADPHONES;
					AZA_LOG_TRACE("%s is Headphones\n", dstDevice->name);
					break;
			},
			// onFail:
			dstDevice->formFactor = AZA_FORM_FACTOR_SPEAKERS
		);
		// TODO: This is super fragile... probably get some other way to get the form factor (or give up and let the user configure it).
		if (aza_str_begins_with(dstDevice->name, "Headphones")) {
			dstDevice->formFactor = AZA_FORM_FACTOR_HEADPHONES;
			AZA_LOG_TRACE("%s is Headphones (matched name string)\n", dstDevice->name);
		}
	}
	return AZA_SUCCESS;
}

typedef struct azaDeviceInitWASAPIParameters {
	azaDeviceInfo *dstDevice;
	IMMDevice *pDevice;
} azaDeviceInitWASAPIParameters;

static int azaDeviceInitWASAPIAction(void *parameter) {
	azaDeviceInitWASAPIParameters *p = parameter;
	azaDeviceInfo *dstDevice = p->dstDevice;
	IMMDevice *pDevice = p->pDevice;
	HRESULT hr;
	IMMEndpoint *pEndpoint = NULL;
	IPropertyStore *pPropertyStore = NULL;
	WCHAR *idWStr;
	hr = pDevice->lpVtbl->QueryInterface(pDevice, &IID_IMMEndpoint, (void**)&pEndpoint);
	CHECK_RESULT("IMMDevice::QueryInterface(IID_IMMEndpoint)", goto err);
	dstDevice->pDevice = pDevice;
	pDevice->lpVtbl->GetId(pDevice, &idWStr);
	dstDevice->idWStr = idWStr;

	hr = pDevice->lpVtbl->OpenPropertyStore(pDevice, STGM_READ, &pPropertyStore);
	CHECK_RESULT("OpenPropertyStore", goto err);
	dstDevice->pPropertyStore = pPropertyStore;
	if (azaDeviceUpdateName(dstDevice)) {
		goto err;
	}
	if (azaDeviceUpdateDeviceFormat(dstDevice)) {
		goto err;
	}
	EDataFlow dataFlow;
	hr = pEndpoint->lpVtbl->GetDataFlow(pEndpoint, &dataFlow);
	CHECK_RESULT("IMMEndpoint::GetDataFlow", goto err);
	dstDevice->input = false;
	dstDevice->output = false;
	switch (dataFlow) {
		case eRender:
			dstDevice->output = true;
			break;
		case eCapture:
			dstDevice->input = true;
			break;
		case eAll:
			AZA_LOG_INFO("Warning: Device data flow is both capture AND render, which is supposedly impossible.\n");
			dstDevice->output = true;
			dstDevice->input = true;
			break;
		default: break;
	}
	azaDeviceUpdateFormFactor(dstDevice);
	if (azaLogLevel >= AZA_LOG_LEVEL_TRACE) {
		char *idCStr = wstrToCstr(idWStr);
		AZA_LOG_TRACE("Device idStr: \"%s\"\n\tname: \"%s\"\n\tchannels: %u\n\tbitDepth: %u\n\tsamplerate: %u\n", idCStr, dstDevice->name, dstDevice->channels, dstDevice->sampleBitDepth, dstDevice->samplerate);
		aza_free(idCStr);
	}
	dstDevice->available = true;
	return AZA_SUCCESS;
err:
	SAFE_RELEASE(pPropertyStore);
	SAFE_RELEASE(pEndpoint);
	dstDevice->available = false;
	return AZA_ERROR_BACKEND_ERROR;
}

static int azaDeviceInitWASAPI(azaDeviceInfo *dstDevice, IMMDevice *pDevice) {
	azaDeviceInitWASAPIParameters parameters = {
		.dstDevice = dstDevice,
		.pDevice = pDevice,
	};
	return soundThreadExecute(azaDeviceInitWASAPIAction, &parameters);
}

// This should only be run by the sound thread
static void azaDeviceDeinitWASAPI(azaDeviceInfo *device) {
	SAFE_RELEASE(device->pDevice);
	SAFE_RELEASE(device->pPropertyStore);
	CoTaskMemFree((LPVOID)device->idWStr);
	SAFE_FREE(device->name);
}



// Stream



typedef struct azaStreamInitWASAPIParameters {
	azaStream *stream;
	azaStreamConfig config;
	azaDeviceInterface deviceInterface;
	uint32_t flags;
	bool activate;
} azaStreamInitWASAPIParameters;

static int azaStreamInitWASAPIAction(void *parameter) {
	azaStreamInitWASAPIParameters *p = parameter;
	azaStream *stream = p->stream;
	azaStreamConfig config = p->config;
	azaDeviceInterface deviceInterface = p->deviceInterface;
	uint32_t flags = p->flags;
	bool activate = p->activate;
	if (!stream) {
		return AZA_ERROR_NULL_POINTER;
	}
	stream->config = config;
	stream->deviceInterface = deviceInterface;
	int errCode = AZA_SUCCESS;
	azaDeviceInfo *deviceInfoDefault = NULL;
	azaDeviceInfo **devicePool = NULL;
	azaDeviceInfo *deviceInfo = NULL;
	size_t deviceCount = 0;
	azaStreamData *data = NULL;
	WAVEFORMATEXTENSIBLE *defaultFormat = NULL;
	if (stream->processCallback == NULL) {
		AZA_LOG_ERR("azaStreamInitWASAPI error: no mix callback provided.\n");
		return AZA_ERROR_NULL_POINTER;
	}
	data = azaStreamDataMake();
	switch (stream->deviceInterface) {
		case AZA_OUTPUT:
			deviceInfoDefault = defaultOutputDevice;
			devicePool = deviceOutput;
			deviceCount = deviceOutputCount;
			break;
		case AZA_INPUT:
			deviceInfoDefault = defaultInputDevice;
			devicePool = deviceInput;
			deviceCount = deviceInputCount;
			break;
		default:
			AZA_LOG_ERR("azaStreamInitWASAPI error: stream->deviceInterface (%d) is invalid.\n", stream->deviceInterface);
			errCode = AZA_ERROR_INVALID_CONFIGURATION;
			goto error;
	}
	if (deviceCount == 0) {
		AZA_LOG_ERR("azaStreamInitWASAPI error: There are no %s devices available\n", stream->deviceInterface == AZA_OUTPUT ? "output" : "input");
		errCode = AZA_ERROR_NO_DEVICES_AVAILABLE;
		goto error;
	}
	// Search the nodes for the device name
	if (stream->config.deviceName) {
		for (size_t i = 0; i < deviceCount; i++) {
			azaDeviceInfo *check = devicePool[i];
			if (strcmp(check->name, stream->config.deviceName) == 0) {
				if (!check->available) {
					AZA_LOG_INFO("Found device by name, but it was unplugged: \"%s\"\n", stream->config.deviceName);
					continue;
				}
				deviceInfo = check;
				AZA_LOG_INFO("Chose device by name: \"%s\"\n", stream->config.deviceName);
				break;
			}
		}
	}
	if (!deviceInfo) {
		deviceInfo = deviceInfoDefault;
		AZA_LOG_INFO("Chose default device: \"%s\"\n", deviceInfo->name);
	}
	data->deviceInfo = deviceInfo;
#define FAIL_ACTION errCode = AZA_ERROR_BACKEND_ERROR; goto error
	HRESULT hr;
	hr = deviceInfo->pDevice->lpVtbl->Activate(deviceInfo->pDevice, &IID_IAudioClient, CLSCTX_ALL, NULL, (void**)&data->pAudioClient);
	CHECK_RESULT("IMMDevice::Activate(IID_IAudioClient)", FAIL_ACTION);
	// Find out if we natively support our desired format and samplerate, otherwise we have to resample and convert formats
	hr = data->pAudioClient->lpVtbl->GetMixFormat(data->pAudioClient, (WAVEFORMATEX**)&defaultFormat);
	CHECK_RESULT("IAudioClient::GetMixFormat", FAIL_ACTION);

#define TEST_BACKEND_FORMAT 0
#define TEST_BACKEND_FORMAT_SUBTYPE KSDATAFORMAT_SUBTYPE_PCM
#define TEST_BACKEND_FORMAT_BITS_PER_SAMPLE 24
#if TEST_BACKEND_FORMAT
	WAVEFORMATEXTENSIBLE desiredFormat;
	desiredFormat.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
	desiredFormat.Format.nChannels = stream->config.channelLayout.count ? stream->config.channelLayout.count : defaultFormat->Format.nChannels;
	desiredFormat.Format.nSamplesPerSec = stream->config.samplerate ? stream->config.samplerate : defaultFormat->Format.nSamplesPerSec;
	desiredFormat.Format.wBitsPerSample = TEST_BACKEND_FORMAT_BITS_PER_SAMPLE;
	desiredFormat.Format.nBlockAlign = desiredFormat.Format.nChannels * (TEST_BACKEND_FORMAT_BITS_PER_SAMPLE/8);
	desiredFormat.Format.nAvgBytesPerSec = desiredFormat.Format.nSamplesPerSec * desiredFormat.Format.nBlockAlign;
	desiredFormat.Format.cbSize = sizeof(desiredFormat) - sizeof(desiredFormat.Format);
	desiredFormat.Samples.wValidBitsPerSample = desiredFormat.Format.wBitsPerSample;
	if (stream->config.channelLayout.count != 0) {
		desiredFormat.dwChannelMask = azaGetChannelMaskFromChannelLayout(stream->config.channelLayout);
	} else {
		desiredFormat.dwChannelMask = defaultFormat->dwChannelMask;
	}
	desiredFormat.SubFormat = TEST_BACKEND_FORMAT_SUBTYPE;
	WAVEFORMATEXTENSIBLE *pClosestFormat = NULL;
	hr = data->pAudioClient->lpVtbl->IsFormatSupported(data->pAudioClient, AUDCLNT_SHAREMODE_SHARED, (WAVEFORMATEX*)&desiredFormat, (WAVEFORMATEX**)&pClosestFormat);
	// TODO: Probably handle the return values of IsFormatSupported (namely AUDCLNT_E_DEVICE_INVALIDATED and AUDCLNT_E_SERVICE_NOT_RUNNING)
	if (hr == S_FALSE) {
		// We got a closest match which is not an exact match
		assert(pClosestFormat != NULL);
		AZA_LOG_INFO("Device \"%s\" doesn't support the exact format desired\n", deviceInfo->name);
		if (pClosestFormat->Format.nSamplesPerSec != desiredFormat.Format.nSamplesPerSec) {
			AZA_LOG_INFO("\tNo support for %dHz samplerate (closest was %dHz)\n", desiredFormat.Format.nSamplesPerSec, pClosestFormat->Format.nSamplesPerSec);
		}
		if (pClosestFormat->Format.nChannels != desiredFormat.Format.nChannels) {
			AZA_LOG_INFO("\tNo support for %hd channels (closest was %hd)\n", desiredFormat.Format.nChannels, pClosestFormat->Format.nChannels);
		}
		if (pClosestFormat->Format.wBitsPerSample != desiredFormat.Format.wBitsPerSample) {
			AZA_LOG_INFO("\tNo support for %hd bits per sample (closest was %hd)\n", desiredFormat.Format.wBitsPerSample, pClosestFormat->Format.wBitsPerSample);
		}
		if (!IsEqualGUID(&pClosestFormat->SubFormat, &desiredFormat.SubFormat)) {
			AZA_LOG_INFO("\tNo support for IEEE float formats\n");
		}
		if (pClosestFormat->dwChannelMask != desiredFormat.dwChannelMask) {
			AZA_LOG_INFO("\tWe have a different channel mask!\n");
		}
		data->waveFormatExtensible = *pClosestFormat;
	} else if (hr == S_OK) {
		// Our exact format is supported
		data->waveFormatExtensible = desiredFormat;
		if (data->waveFormatExtensible.dwChannelMask == SPEAKER_ALL) {
			data->waveFormatExtensible.dwChannelMask = defaultFormat->dwChannelMask;
		}
		AZA_LOG_INFO("Device \"%s\" supports the exact format desired :)\n", deviceInfo->name);
	} else if (hr == AUDCLNT_E_UNSUPPORTED_FORMAT) {
		// Fallback to default
		data->waveFormatExtensible = *defaultFormat;
		AZA_LOG_INFO("Device \"%s\" can't compromise, falling back to default format\n", deviceInfo->name);
	} else {
		CHECK_RESULT("IAudioClient::IsFormatSupported", FAIL_ACTION);
	}
#else
	// Always pick the mix format, since it's possible for formats to report as supported where some channels go nowhere >_>
	// We'll handle any conversions necessary on our side.
	data->waveFormatExtensible = *defaultFormat;
#endif

	hr = data->pAudioClient->lpVtbl->Initialize(data->pAudioClient, AUDCLNT_SHAREMODE_SHARED, 0, 10000 * 25, 0, (WAVEFORMATEX*)&data->waveFormatExtensible, NULL);
	CHECK_RESULT("IAudioClient::Initialize", FAIL_ACTION);
	hr = data->pAudioClient->lpVtbl->GetBufferSize(data->pAudioClient, &data->deviceBufferFrames);
	CHECK_RESULT("IAudioClient::GetBufferSize", FAIL_ACTION);
	AZA_LOG_INFO("Buffer has %u frames\n", data->deviceBufferFrames);
	if (stream->deviceInterface == AZA_OUTPUT) {
		hr = data->pAudioClient->lpVtbl->GetService(data->pAudioClient, &IID_IAudioRenderClient, (void**)&data->pRenderClient);
		CHECK_RESULT("IAudioClient::GetService", FAIL_ACTION);
		uint8_t *buffer;
		hr = data->pRenderClient->lpVtbl->GetBuffer(data->pRenderClient, data->deviceBufferFrames, &buffer);
		CHECK_RESULT("IAudioRenderClient::GetBuffer", FAIL_ACTION);
		memset(buffer, 0, data->deviceBufferFrames * data->waveFormatExtensible.Format.nBlockAlign);
		hr = data->pRenderClient->lpVtbl->ReleaseBuffer(data->pRenderClient, data->deviceBufferFrames, 0);
		CHECK_RESULT("IAudioRenderClient::ReleaseBuffer", FAIL_ACTION);
	} else {
		hr = data->pAudioClient->lpVtbl->GetService(data->pAudioClient, &IID_IAudioCaptureClient, (void**)&data->pCaptureClient);
		CHECK_RESULT("IAudioClient::GetService", FAIL_ACTION);
	}

	hr = data->pAudioClient->lpVtbl->Start(data->pAudioClient);
	CHECK_RESULT("IAudioClient::Start", FAIL_ACTION);

	data->stream = stream;
	stream->data = data;

	if (!stream->config.deviceName && (flags & AZA_STREAM_COMMIT_DEVICE_NAME)) {
		stream->config.deviceName = aza_strdup(data->deviceInfo->name);
	}
	if (!stream->config.samplerate && (flags & AZA_STREAM_COMMIT_SAMPLERATE)) {
		stream->config.samplerate = data->deviceInfo->samplerate;
	}
	if (!stream->config.channelLayout.count && (flags & AZA_STREAM_COMMIT_CHANNEL_LAYOUT)) {
		stream->config.channelLayout = azaGetChannelLayoutFromStreamDevice(data);
	}

	data->isActive = activate;

	CoTaskMemFree(defaultFormat);
#if TEST_BACKEND_FORMAT
	CoTaskMemFree(pClosestFormat);
#endif
	return AZA_SUCCESS;
error:
	CoTaskMemFree(defaultFormat);
#if TEST_BACKEND_FORMAT
	CoTaskMemFree(pClosestFormat);
#endif
	azaStreamDataFree(data);
	return errCode;
#undef FAIL_ACTION
}

// parameter is a single azaStream*
static int azaStreamDeinitWASAPIAction(void *parameter) {
	azaStream *stream = parameter;
	azaStreamDataFree(stream->data);
	return AZA_SUCCESS;
}

// parameter is a single azaStream*
static int azaStreamReinitWASAPIAction(void *parameter) {
	azaStream *stream = parameter;
	azaStreamData *streamData = stream->data;
	bool isActive = streamData->isActive;
	azaStreamDeinitWASAPIAction(stream);
	azaStreamInitWASAPIParameters parameters = {
		.stream = stream,
		.config = stream->config,
		.deviceInterface = stream->deviceInterface,
		.flags = 0,
		.activate = isActive,
	};
	return azaStreamInitWASAPIAction(&parameters);
}

static void azaStreamProcess(azaStreamData *data) {
	int err = AZA_SUCCESS;
#define ERROR(ERR) err = ERR; goto error
#define CHECK_ERROR() { if (err) goto error; }
	HRESULT hr;
	azaStream *stream = data->stream;
	if (!data->isActive || stream->error) return;

	azaCPUUsageTimingStart(&stream->cpuUsage);

	uint32_t samplerateNative = data->waveFormatExtensible.Format.nSamplesPerSec;
	uint32_t samplerate = data->stream->config.samplerate ? data->stream->config.samplerate : samplerateNative;
	azaChannelLayout channelLayoutNative = azaGetChannelLayoutFromStreamDevice(data);
	azaChannelLayout channelLayout = data->stream->config.channelLayout.count ? data->stream->config.channelLayout : channelLayoutNative;

	uint32_t numFramesNative;
	uint8_t *deviceBufferRaw;

	// To handle multiple optional steps we point this at whatever has the current data
	azaBuffer buffer;

	bool doResampling = samplerate != data->waveFormatExtensible.Format.nSamplesPerSec;
	bool doChannelMixing = channelLayout.count != data->waveFormatExtensible.Format.nChannels;
	bool doTypeConversion = !WaveFormatExtensibleIsFloat32(&data->waveFormatExtensible);

	bool directToDevice = !doResampling && !doChannelMixing && !doTypeConversion;

	if (stream->deviceInterface == AZA_OUTPUT) {
		uint32_t numFramesUnread;
		hr = data->pAudioClient->lpVtbl->GetCurrentPadding(data->pAudioClient, &numFramesUnread);
		if (hr == AUDCLNT_E_DEVICE_INVALIDATED) {
			defaultOutputDevice = azaFindDefaultDevice(deviceOutput, deviceOutputCount, eRender, "output");
			azaStreamReinitWASAPIAction(stream);
			return;
		}
		CHECK_RESULT("IAudioRenderClient::GetCurrentPadding", ERROR(AZA_ERROR_BACKEND_ERROR));
		numFramesNative = data->deviceBufferFrames - numFramesUnread;
		if (numFramesNative == 0) return;
		hr = data->pRenderClient->lpVtbl->GetBuffer(data->pRenderClient, numFramesNative, &deviceBufferRaw);
		CHECK_RESULT("IAudioRenderClient::GetBuffer", ERROR(AZA_ERROR_BACKEND_ERROR));
		buffer = (azaBuffer) {
			.pSamples = (float*)deviceBufferRaw,
			.samplerate = samplerateNative,
			.frames = numFramesNative,
			.stride = channelLayout.count,
			.channelLayout = channelLayout,
		};
		if (directToDevice) {
			// We good
		} else {
			uint32_t numFrames = azaResamplerGetNextInputCount(&data->resampler, &buffer, samplerate);
			err = azaBufferResize(&data->processingBuffer, numFrames, 0, 0, channelLayout);
			CHECK_ERROR();
			data->processingBuffer.samplerate = samplerate;
			buffer = azaBufferView(&data->processingBuffer);
		}
		azaBufferZero(&buffer);
	} else {
		hr = data->pCaptureClient->lpVtbl->GetNextPacketSize(data->pCaptureClient, &numFramesNative);
		if (hr == AUDCLNT_E_DEVICE_INVALIDATED) {
			// TODO: We may not need to do this here, since IMMNotificationClient should do this for us. Probably best to verify that's always the case. Trusting a Microsoft API is a rocky road.
			defaultInputDevice = azaFindDefaultDevice(deviceInput, deviceInputCount, eCapture, "input");
			azaStreamDeinit(stream);
			azaStreamInit(stream, stream->config, stream->deviceInterface, 0, true);
			return;
		}
		CHECK_RESULT("IAudioCaptureClient::GetNextPacketSize", ERROR(AZA_ERROR_BACKEND_ERROR));
		if (numFramesNative == 0) return;
		DWORD flags;
		hr = data->pCaptureClient->lpVtbl->GetBuffer(data->pCaptureClient, &deviceBufferRaw, &numFramesNative, &flags, NULL, NULL);
		CHECK_RESULT("IAudioCaptureClient::GetBuffer", ERROR(AZA_ERROR_BACKEND_ERROR));
		if (directToDevice) {
			buffer = (azaBuffer) {
				.pSamples = (float*)deviceBufferRaw,
				.samplerate = samplerate,
				.frames = numFramesNative,
				.stride = channelLayout.count,
				.channelLayout = channelLayout,
			};
		} else {
			if (doTypeConversion) {
				err = azaBufferResize(&data->conversionBuffer, numFramesNative, 0, 0, channelLayoutNative);
				CHECK_ERROR();
				azaConvertFromNativeWASAPI(&data->waveFormatExtensible, &data->conversionBuffer, deviceBufferRaw);
				buffer = azaBufferView(&data->conversionBuffer);
			} else {
				buffer = (azaBuffer) {
					.pSamples = (float*)deviceBufferRaw,
					.samplerate = samplerate,
					.frames = numFramesNative,
					.stride = channelLayout.count,
					.channelLayout = channelLayout,
				};
			}
			if (doResampling) {
				uint32_t numFrames = azaResamplerGetNextOutputCount(&data->resampler, samplerate, &buffer);
				err = azaBufferResize(&data->resamplingBuffer, numFrames, 0, 0, channelLayoutNative);
				CHECK_ERROR();
				err = azaResamplerResample(&data->resampler, &data->resamplingBuffer, &buffer);
				CHECK_ERROR();
				buffer = azaBufferView(&data->resamplingBuffer);
			}
			if (doChannelMixing) {
				err = azaChannelMatrixUpdate(&data->channelMatrix, channelLayoutNative, channelLayout);
				CHECK_ERROR();
				err = azaBufferResize(&data->channelMixBuffer, buffer.frames, 0, 0, channelLayout);
				CHECK_ERROR();
				azaBufferMixMatrix(&data->channelMixBuffer, 0.0f, &buffer, 1.0f, &data->channelMatrix);
				buffer = azaBufferView(&data->channelMixBuffer);
			}
		}
	}
	buffer.channelLayout.formFactor = data->deviceInfo->formFactor;

	err = stream->processCallback(stream->userdata, &buffer, &buffer, 0);
	CHECK_ERROR();

	if (stream->deviceInterface == AZA_OUTPUT) {
		if (!directToDevice) {
			if (doResampling) {
				err = azaBufferResize(&data->resamplingBuffer, numFramesNative, 0, 0, channelLayout);
				CHECK_ERROR();
				data->resamplingBuffer.samplerate = samplerateNative;
				err = azaResamplerResample(&data->resampler, &data->resamplingBuffer, &buffer);
				CHECK_ERROR();
				buffer = azaBufferView(&data->resamplingBuffer);
			}
			if (doChannelMixing) {
				err = azaChannelMatrixUpdate(&data->channelMatrix, channelLayout, channelLayoutNative);
				CHECK_ERROR();
				err = azaBufferResize(&data->channelMixBuffer, buffer.frames, 0, 0, channelLayoutNative);
				CHECK_ERROR();
				azaBufferMixMatrix(&data->channelMixBuffer, 0.0f, &buffer, 1.0f, &data->channelMatrix);
				buffer = azaBufferView(&data->channelMixBuffer);
			}
			azaConvertToNativeWASAPI(&data->waveFormatExtensible, &buffer, deviceBufferRaw);
		}
		hr = data->pRenderClient->lpVtbl->ReleaseBuffer(data->pRenderClient, numFramesNative, 0);
		CHECK_RESULT("IAudioRenderClient::ReleaseBuffer", ERROR(AZA_ERROR_BACKEND_ERROR));
	} else {
		hr = data->pCaptureClient->lpVtbl->ReleaseBuffer(data->pCaptureClient, numFramesNative);
		CHECK_RESULT("IAudioCaptureClient::ReleaseBuffer", ERROR(AZA_ERROR_BACKEND_ERROR));
	}
	azaCPUUsageTimingEnd(&stream->cpuUsage);
	return;
error:
	azaCPUUsageTimingEnd(&stream->cpuUsage);
	stream->error = err;\
	AZA_LOG_ERR("Processing stream for device \"%s\" had an error (%s). Disabling stream...\n", data->deviceInfo->name, azaErrorString(err));
#undef ERROR
#undef CHECK_ERROR
}



// Sound Thread



static bool soundThreadInit() {
	HRESULT hr;
	hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
	CHECK_RESULT("soundThreadProc CoInitialize", goto error);

	hr = CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL, &IID_IMMDeviceEnumerator, (void**)&pEnumerator);
	CHECK_RESULT("CoCreateInstance", goto error);

	hr = pEnumerator->lpVtbl->EnumAudioEndpoints(pEnumerator, eAll, DEVICE_STATE_ACTIVE, &pDeviceCollection);
	CHECK_RESULT("EnumAudioEndpoints", goto error);
	UINT deviceCountUINT = 0;
	pDeviceCollection->lpVtbl->GetCount(pDeviceCollection, &deviceCountUINT);
	if (deviceCountUINT > AZA_MAX_DEVICES) {
		AZA_LOG_INFO("System has too many devices (%u)! Only enumerating the first %u...\n", deviceCountUINT, AZA_MAX_DEVICES);
		deviceCountUINT = AZA_MAX_DEVICES;
	}
	deviceCount = 0;
	for (UINT i = 0; i < deviceCountUINT; i++) {
		IMMDevice *pDevice = NULL;
		pDeviceCollection->lpVtbl->Item(pDeviceCollection, i, &pDevice);
		azaDeviceInfo *dstDevice = &device[deviceCount];
		if (AZA_SUCCESS == azaDeviceInitWASAPIAction(&(azaDeviceInitWASAPIParameters){dstDevice, pDevice})) {
			deviceCount++;
			if (dstDevice->output) {
				deviceOutput[deviceOutputCount++] = dstDevice;
			}
			if (dstDevice->input) {
				deviceInput[deviceInputCount++] = dstDevice;
			}
		}
	}
	defaultOutputDevice = azaFindDefaultDevice(deviceOutput, deviceOutputCount, eRender, "output");
	if (defaultOutputDevice == NULL) goto error;
	defaultInputDevice = azaFindDefaultDevice(deviceInput, deviceInputCount, eCapture, "input");
	if (defaultInputDevice == NULL) goto error;

	hr = pEnumerator->lpVtbl->RegisterEndpointNotificationCallback(pEnumerator, &immNotificationClient);
	CHECK_RESULT("RegisterEndpointNotificationCallback", goto error);

	initCompleted = true;
	return true;
error:
	hadInitError = true;
	return false;
}

static void soundThreadDeinit() {
	// TODO: Deinit all the devices and streams
	for (uint32_t i = 0; i < streams.count; i++) {
		azaStreamDataFree(streams.data[i]);
	}
	AZA_DA_DEINIT(streams);
	for (uint32_t i = 0; i < deviceCount; i++) {
		azaDeviceDeinitWASAPI(&device[i]);
	}
	deviceCount = 0;
	SAFE_RELEASE(pEnumerator);
	SAFE_RELEASE(pDeviceCollection);
	CoUninitialize();
}

static AZA_THREAD_PROC_DEF(soundThreadProc, userdata) {
	azaMutexLock(&mutex);
	if (!soundThreadInit()) {
		azaMutexUnlock(&mutex);
		return 1;
	}

	while (!shouldQuit) {
		azaMutexUnlock(&mutex);
		executeSoundThreadQueueActions();
		azaMutexLock(&mutex);
		// Do the sound stuffs
		for (uint32_t i = 0; i < streams.count; i++) {
			azaStreamProcess(streams.data[i]);
		}

		azaMutexUnlock(&mutex);
		// TODO: Actually time our runtime so we only sleep if we're not too busy
		azaThreadSleep(1);
		azaMutexLock(&mutex);
	}
	soundThreadDeinit();
	azaMutexUnlock(&mutex);
	return 0;
}



// IMMNotificationClient



// If this reference is temporary, remember to Release it
static IMMDevice* azaGetIMMDeviceFromDeviceId(LPCWSTR pwstrDeviceId) {
	IMMDevice *pDevice = NULL;
	HRESULT hr = pEnumerator->lpVtbl->GetDevice(pEnumerator, pwstrDeviceId, &pDevice);
	CHECK_RESULT("GetDevice", return NULL);
	return pDevice;
}
// Can fail and return NULL
static azaDeviceInfo* azaGetDeviceFromDeviceId(LPCWSTR pwstrDeviceId) {
	azaDeviceInfo *result = NULL;
	for (size_t i = 0; i < deviceCount; i++) {
		if (wcscmp(device[i].idWStr, pwstrDeviceId) == 0) {
			result = &device[i];
			break;
		}
	}
	return result;
}

static void immncHandleStreamsOnDeviceActivated(azaDeviceInfo *device) {
	for (size_t i = 0; i < streams.count; i++) {
		azaStreamData *streamData = streams.data[i];
		const char *configName = streamData->stream->config.deviceName;
		const char *curDevName = streamData->deviceInfo->name;
		const char *newDevName = device->name;
		if (configName // We have a device name we want
			&& strcmp(configName, curDevName) != 0 // The current device name doesn't match it
			&& strcmp(configName, newDevName) == 0 // The new device name DOES match it
		) {
			soundThreadQueue(azaStreamReinitWASAPIAction, streamData->stream, false, true);
		}
	}
}

static void immncHandleStreamsOnDeviceDeactivated(azaDeviceInfo *device) {
	for (size_t i = 0; i < streams.count; i++) {
		azaStreamData *streamData = streams.data[i];
		if (streamData->deviceInfo == device) {
			soundThreadQueue(azaStreamReinitWASAPIAction, streamData->stream, false, true);
		}
	}
}

static HRESULT STDMETHODCALLTYPE immncQueryInterface(IMMNotificationClient *This, REFIID riid, void **ppvObject) {
	if (IsEqualGUID(&riid, &IID_IUnknown) || IsEqualGUID(&riid, &IID_IMMNotificationClient)) {
		*ppvObject = This;
	} else {
		*ppvObject = NULL;
		return E_NOINTERFACE;
	}
	return S_OK;
}
static ULONG STDMETHODCALLTYPE immncAddRef(IMMNotificationClient * This) {
	return 1; // Leave my singleton alone!
}
static ULONG STDMETHODCALLTYPE immncRelease(IMMNotificationClient * This) {
	return 1; // Leave my singleton alone!
}
static HRESULT STDMETHODCALLTYPE immncOnDeviceStateChanged(IMMNotificationClient *This, LPCWSTR pwstrDeviceId, DWORD dwNewState) {
	azaDeviceInfo *dstDevice = azaGetDeviceFromDeviceId(pwstrDeviceId);
	if (!dstDevice) {
		AZA_LOG_ERR("Received an OnDeviceStateChanged about a device that we don't have enumerated already.");
		goto done;
	}
	switch (dwNewState) {
		case DEVICE_STATE_ACTIVE:
			dstDevice->available = true;
			break;
		case DEVICE_STATE_DISABLED:
		case DEVICE_STATE_NOTPRESENT:
		case DEVICE_STATE_UNPLUGGED:
			dstDevice->available = false;
			break;
	}
	if (dstDevice->available) {
		// Check if any streams match the device name exactly and put them on here
		immncHandleStreamsOnDeviceActivated(dstDevice);
	} else {
		// Check if any streams are on this device currently and put them elsewhere
		immncHandleStreamsOnDeviceDeactivated(dstDevice);
	}
done:
	return S_OK;
}
static HRESULT STDMETHODCALLTYPE immncOnDeviceAdded(IMMNotificationClient *This, LPCWSTR pwstrDeviceId) {
	azaDeviceInfo *dstDevice = azaGetDeviceFromDeviceId(pwstrDeviceId);
	if (dstDevice) {
		dstDevice->available = true;
	} else {
		// Actually add the device
		IMMDevice *pDevice = azaGetIMMDeviceFromDeviceId(pwstrDeviceId);
		dstDevice = &device[deviceCount];
		if (azaDeviceInitWASAPI(dstDevice, pDevice)) {
			deviceCount++;
			if (dstDevice->input) {
				deviceInput[deviceInputCount++] = dstDevice;
			}
			if (dstDevice->output) {
				deviceOutput[deviceOutputCount++] = dstDevice;
			}
		} else {
			SAFE_RELEASE(pDevice);
		}
	}
	immncHandleStreamsOnDeviceActivated(dstDevice);
	return S_OK;
}
static HRESULT STDMETHODCALLTYPE immncOnDeviceRemoved(IMMNotificationClient *This, LPCWSTR pwstrDeviceId) {
	azaDeviceInfo *dstDevice = azaGetDeviceFromDeviceId(pwstrDeviceId);
	if (!dstDevice) {
		AZA_LOG_ERR("Received an OnDeviceRemoved about a device that we don't have enumerated yet.");
		goto done;
	}
	dstDevice->available = false;
	immncHandleStreamsOnDeviceDeactivated(dstDevice);
done:
	return S_OK;
}
static HRESULT STDMETHODCALLTYPE immncOnDefaultDeviceChanged(IMMNotificationClient * This, EDataFlow flow, ERole role, LPCWSTR pwstrDefaultDeviceId) {
	azaDeviceInfo *dstDevice = azaGetDeviceFromDeviceId(pwstrDefaultDeviceId);
	if (!dstDevice) {
		AZA_LOG_ERR("Received an OnDefaultDeviceChanged about a device that we don't have enumerated yet.");
		goto done;
	}
	azaDeviceInfo *oldDefaultDevice = NULL;
	azaDeviceInfo *newDefaultDevice = NULL;
	if (dstDevice->input) {
		oldDefaultDevice = defaultInputDevice;
		defaultInputDevice = dstDevice;
		newDefaultDevice = defaultInputDevice;
	}
	if (dstDevice->output) {
		oldDefaultDevice = defaultOutputDevice;
		defaultOutputDevice = dstDevice;
		newDefaultDevice = defaultOutputDevice;
	}
	// Update any default-device streams
	for (size_t i = 0; i < streams.count; i++) {
		azaStreamData *streamData = streams.data[i];
		if (streamData->stream->config.deviceName) continue; // This can't affect devices chosen by name.
		azaDeviceInfo *di = streamData->deviceInfo;
		if (di == oldDefaultDevice && di != newDefaultDevice) {
			soundThreadQueue(azaStreamReinitWASAPIAction, streamData->stream, false, true);
		}
	}
done:
	return S_OK;
}
static HRESULT STDMETHODCALLTYPE immncOnPropertyValueChanged(IMMNotificationClient * This, LPCWSTR pwstrDeviceId, const PROPERTYKEY key) {
	azaDeviceInfo *dstDevice = azaGetDeviceFromDeviceId(pwstrDeviceId);
	if (!dstDevice) {
		AZA_LOG_ERR("Received an OnPropertyValueChanged about a device that we don't have enumerated yet.");
		goto done;
	}
	if (IsEqualGUID(&key.fmtid, &PKEY_Device_FriendlyName.fmtid)) {
		AZA_LOG_TRACE("OnPropertyValueChanged FriendlyName(was \"%s\")", dstDevice->name);
		soundThreadQueue(azaDeviceUpdateName, dstDevice, false, true);
		AZA_LOG_TRACE("New name: \"%s\"", dstDevice->name);
		// This handles any name matching updates, but only if the new device is matched.
		// Does nothing if the new device no longer matches name.
		immncHandleStreamsOnDeviceActivated(dstDevice);
	} else if (IsEqualGUID(&key.fmtid, &PKEY_AudioEngine_DeviceFormat.fmtid)) {
		AZA_LOG_TRACE("OnPropertyValueChanged DeviceFormat for Device \"%s\"", dstDevice->name);
		soundThreadQueue(azaDeviceUpdateDeviceFormat, dstDevice, false, true);
	} else if (IsEqualGUID(&key.fmtid, &PKEY_AudioEndpoint_FormFactor.fmtid)) {
		AZA_LOG_TRACE("OnPropertyValueChanged FormFactor for Device \"%s\"", dstDevice->name);
		soundThreadQueue(azaDeviceUpdateFormFactor, dstDevice, false, true);
	} else {
		AZA_LOG_TRACE("OnPropertyValueChanged for Device \"%s\": Unhandled key", dstDevice->name);
	}
done:
	return S_OK;
}



// API Boilerplate



static int azaWASAPIInit() {
	shouldQuit = false;
	hadInitError = false;
	initCompleted = false;
	azaMutexInit(&mutex);
	azaMutexInit(&stqaMutex);
	if (azaThreadLaunch(&thread, soundThreadProc, NULL)) {
		AZA_LOG_ERR("Couldn't initialize the sound thread...%d\n", errno);
		goto error;
	}
	azaMutexLock(&mutex);
	while (!initCompleted) {
		azaMutexUnlock(&mutex);
		azaThreadYield();
		azaMutexLock(&mutex);
		if (hadInitError) {
			azaThreadJoin(&thread);
			goto error;
		}
	}
	azaMutexUnlock(&mutex);
	return AZA_SUCCESS;
error:
	return AZA_ERROR_BACKEND_LOAD_ERROR;
}

static void azaWASAPIDeinit() {
	azaMutexLock(&mutex);
	shouldQuit = true;
	azaMutexUnlock(&mutex);
	azaThreadJoin(&thread);
	azaMutexDeinit(&mutex);
	azaMutexDeinit(&stqaMutex);
}

static int azaStreamInitWASAPI(azaStream *stream, azaStreamConfig config, azaDeviceInterface deviceInterface, uint32_t flags, bool activate) {
	azaStreamInitWASAPIParameters parameters = { stream, config, deviceInterface, flags, activate };
	stream->cpuUsage = (azaCPUUsage){0};
	return soundThreadExecute(azaStreamInitWASAPIAction, &parameters);
}

static void azaStreamDeinitWASAPI(azaStream *stream) {
	soundThreadExecute(azaStreamDeinitWASAPIAction, stream);
}

static void azaStreamSetActiveWASAPI(azaStream *stream, bool active) {
	azaMutexLock(&mutex);
	azaStreamData *data = stream->data;
	data->isActive = active;
	azaMutexUnlock(&mutex);
}

static bool azaStreamGetActiveWASAPI(azaStream *stream) {
	azaMutexLock(&mutex);
	azaStreamData *data = stream->data;
	bool result = data->isActive;
	azaMutexUnlock(&mutex);
	return result;
}

static const char* azaStreamGetDeviceNameWASAPI(azaStream *stream) {
	azaMutexLock(&mutex);
	azaStreamData *data = stream->data;
	const char *result = data->deviceInfo->name;
	azaMutexUnlock(&mutex);
	return result;
}

static uint32_t azaStreamGetSamplerateWASAPI(azaStream *stream) {
	azaMutexLock(&mutex);
	azaStreamData *data = stream->data;
	uint32_t result = stream->config.samplerate ? stream->config.samplerate : data->waveFormatExtensible.Format.nSamplesPerSec;
	azaMutexUnlock(&mutex);
	return result;
}

static azaChannelLayout azaStreamGetChannelLayoutWASAPI(azaStream *stream) {
	azaMutexLock(&mutex);
	azaStreamData *data = stream->data;
	azaChannelLayout result = stream->config.channelLayout.count ? stream->config.channelLayout : azaGetChannelLayoutFromStreamDevice(data);
	azaMutexUnlock(&mutex);
	return result;
}

static uint32_t azaStreamGetBufferFrameCountWASAPI(azaStream *stream) {
	azaMutexLock(&mutex);
	azaStreamData *data = stream->data;
	uint32_t samplerateNative = data->waveFormatExtensible.Format.nSamplesPerSec;
	uint32_t samplerate = stream->config.samplerate ? stream->config.samplerate : samplerateNative;
	uint32_t result;
	if (stream->deviceInterface == AZA_OUTPUT) {
		result = azaGetResampledSrcFrameCount(samplerateNative, samplerate, data->deviceBufferFrames);
	} else {
		result = azaGetResampledDstFrameCount(samplerate, samplerateNative, data->deviceBufferFrames);
	}
	azaMutexUnlock(&mutex);
	return result;
}

static size_t azaGetDeviceCountWASAPI(azaDeviceInterface interface) {
	azaMutexLock(&mutex);
	size_t result;
	switch (interface) {
		case AZA_OUTPUT:
			result = deviceOutputCount;
			break;
		case AZA_INPUT:
			result = deviceInputCount;
			break;
		default:
			result = 0;
			break;
	}
	azaMutexUnlock(&mutex);
	return result;
}

static const char* azaGetDeviceNameWASAPI(azaDeviceInterface interface, size_t index) {
	azaMutexLock(&mutex);
	const char *result;
	switch (interface) {
		case AZA_OUTPUT:
			assert(index < deviceOutputCount);
			result = deviceOutput[index]->name;
			break;
		case AZA_INPUT:
			assert(index < deviceInputCount);
			result = deviceInput[index]->name;
			break;
		default:
			result = "Invalid Interface!";
			break;
	}
	azaMutexUnlock(&mutex);
	return result;
}

static size_t azaGetDeviceChannelsWASAPI(azaDeviceInterface interface, size_t index) {
	azaMutexLock(&mutex);
	size_t result;
	switch (interface) {
		case AZA_OUTPUT:
			assert(index < deviceOutputCount);
			result = deviceOutput[index]->channels;
			break;
		case AZA_INPUT:
			assert(index < deviceInputCount);
			result = deviceInput[index]->channels;
			break;
		default:
			result = 0;
			break;
	}
	azaMutexUnlock(&mutex);
	return result;
}

static bool azaGetDeviceIsAvailableWASAPI(azaDeviceInterface interface, size_t index) {
	azaMutexLock(&mutex);
	bool result;
	switch (interface) {
		case AZA_OUTPUT:
			assert(index < deviceOutputCount);
			result = deviceOutput[index]->available;
			break;
		case AZA_INPUT:
			assert(index < deviceInputCount);
			result = deviceInput[index]->available;
			break;
		default:
			result = false;
			break;
	}
	azaMutexUnlock(&mutex);
	return result;
}

int azaBackendWASAPIInit() {
	azaStreamInit = azaStreamInitWASAPI;
	azaStreamDeinit = azaStreamDeinitWASAPI;
	azaStreamSetActive = azaStreamSetActiveWASAPI;
	azaStreamGetActive = azaStreamGetActiveWASAPI;
	azaStreamGetDeviceName = azaStreamGetDeviceNameWASAPI;
	azaStreamGetSamplerate = azaStreamGetSamplerateWASAPI;
	azaStreamGetChannelLayout = azaStreamGetChannelLayoutWASAPI;
	azaStreamGetBufferFrameCount = azaStreamGetBufferFrameCountWASAPI;
	azaGetDeviceCount = azaGetDeviceCountWASAPI;
	azaGetDeviceName = azaGetDeviceNameWASAPI;
	azaGetDeviceChannels = azaGetDeviceChannelsWASAPI;
	azaGetDeviceIsAvailable = azaGetDeviceIsAvailableWASAPI;

	return azaWASAPIInit();
}

void azaBackendWASAPIDeinit() {
	azaWASAPIDeinit();
}