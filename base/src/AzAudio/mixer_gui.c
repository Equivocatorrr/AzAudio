/*
	File: mixer_gui.c
	Author: Philip Haynes
	Implementation of a GUI for interacting with an azaMixer on-the-fly, all at the convenience of a single function call!
*/

#include "mixer.h"

#include "AzAudio.h"
#include "math.h"
#include "dsp/azaDSP.h"
#include "gui/gui.h"

#include <stdio.h>

#if defined(__GNUC__)
// Suppress some unused function warnings
#pragma GCC diagnostic ignored "-Wunused-function"
#endif




// Global state




static azaMixer *currentMixer = NULL;
static azaDSP *selectedDSP = NULL;
static azagWindow mixerWindow = AZAG_WINDOW_INVALID;

static int scrollTracksX = 0;

typedef struct azaTrackGUIMetadata {
	int scrollFXY;
	int scrollSendsX;
	int width;
} azaTrackGUIMetadata;

static struct {
	azaTrackGUIMetadata *data;
	uint32_t count;
	uint32_t capacity;
} azaTrackGUIMetadatas = {0};




// Context Menus




static int contextMenuTrackIndex = 0;
static char contextMenuError[128] = {0};
static azaTrack *contextMenuTrackSend = NULL;
static azaDSP *contextMenuTrackFXDSP = NULL;

static azaTrack* azagContextMenuTrackFromIndex() {
	if (contextMenuTrackIndex <= 0) {
		return &currentMixer->master;
	} else {
		return currentMixer->tracks.data[contextMenuTrackIndex-1];
	}
}

static void azagContextMenuSetIndexFromTrack(azaTrack *track) {
	if (track == &currentMixer->master) {
		contextMenuTrackIndex = 0;
	} else {
		for (uint32_t i = 0; i < currentMixer->tracks.count; i++) {
			if (track == currentMixer->tracks.data[i]) {
				contextMenuTrackIndex = i+1;
				break;
			}
		}
	}
}



// Context Menu Implementations



static void azagContextMenuTrackRemove();
static void azagContextMenuSendAdd();

static void azagContextMenuTrack() {
	bool doRemoveTrack = contextMenuTrackIndex > 0;
	bool doRemoveSend = contextMenuTrackSend != NULL;

	azagDrawContextMenuBegin(NULL);

	if (azagDrawContextMenuButton("Add Track")) {
		AZA_LOG_TRACE("Track Add at index %d!\n", contextMenuTrackIndex);
		azaTrack *track;
		azaMixerAddTrack(currentMixer, contextMenuTrackIndex, &track, currentMixer->master.buffer.channelLayout, true);
		// TODO: Come up with a better auto name
		azaTrackSetName(track, azaTextFormat("Track %d", contextMenuTrackIndex));
		AZA_DA_INSERT(azaTrackGUIMetadatas, contextMenuTrackIndex+1, (azaTrackGUIMetadata){0}, do{}while(0));
	}
	if (doRemoveTrack) {
		if (azagDrawContextMenuButton("Remove Track")) {
			azagContextMenuOpen(azagContextMenuTrackRemove);
		}
	}
	if (azagDrawContextMenuButton("Add Send")) {
		azagContextMenuOpen(azagContextMenuSendAdd);
	}
	if (doRemoveSend) {
		if (azagDrawContextMenuButton(azaTextFormat("Remove Send to %s", contextMenuTrackSend->name))) {
			azaTrackDisconnect(azagContextMenuTrackFromIndex(), contextMenuTrackSend);
		}
	}

	azagDrawContextMenuEnd();
}

static void azagContextMenuTrackRemove() {
	azagDrawContextMenuBegin("Really Remove Track?");

	if (azagDrawContextMenuButton("Obliterate That Thang")) {
		int toRemove = contextMenuTrackIndex-1;
		if (toRemove >= 0) {
			AZA_LOG_TRACE("Track Remove at index %d!\n", toRemove);
			azaMixerRemoveTrack(currentMixer, toRemove);
			AZA_DA_ERASE(azaTrackGUIMetadatas, contextMenuTrackIndex, 1);
		}
	}
	azagDrawContextMenuButton("Cancel");

	azagDrawContextMenuEnd();
}

static void azagContextMenuSendAdd() {
	int count = currentMixer->tracks.count; // +1 for Master, -1 for self
	if (count == 0) {
		azagDrawContextMenuBegin(NULL);
		azagDrawContextMenuButton("No >:(");
		azagDrawContextMenuEnd();
		return;
	}
	azagDrawContextMenuBegin(NULL);

	azaTrack *target = &currentMixer->master;
	azaTrack *track = azagContextMenuTrackFromIndex();
	for (int32_t i = 0; i < (int32_t)currentMixer->tracks.count+1; target = currentMixer->tracks.data[i++]) {
		if (i == contextMenuTrackIndex) continue; // Skip self
		if (azagDrawContextMenuButton(target->name)) {
			azaTrackConnect(track, target, 0.0f, NULL, 0);
		}
	}

	azagDrawContextMenuEnd();
}

static void azagContextMenuTrackFXAdd();

static void azagContextMenuTrackFX() {
	bool doRemovePlugin = contextMenuTrackFXDSP != NULL;
	azagDrawContextMenuBegin(NULL);

	if (azagDrawContextMenuButton("Add Plugin")) {
		azagContextMenuOpen(azagContextMenuTrackFXAdd);
	}
	if (doRemovePlugin) {
		if (azagDrawContextMenuButton(azaTextFormat("Remove %s", contextMenuTrackFXDSP->name))) {
			azaTrackRemoveDSP(azagContextMenuTrackFromIndex(), contextMenuTrackFXDSP);
			if (selectedDSP == contextMenuTrackFXDSP) {
				selectedDSP = NULL;
			}
			azaFreeDSP(contextMenuTrackFXDSP);
			contextMenuTrackFXDSP = NULL;
		}
	}

	azagDrawContextMenuEnd();
}

static void azagContextMenuTrackFXAdd() {
	azaTrack *track = azagContextMenuTrackFromIndex();

	azagDrawContextMenuBegin(NULL);

	for (uint32_t i = 0; i < azaDSPRegistry.count; i++) {
		if (azaDSPRegistry.data[i].fp_makeDSP == NULL) continue;
		const char *name = azaDSPRegistry.data[i].base.name;
		if (azagDrawContextMenuButton(name)) {
			azaDSP *newDSP = azaDSPRegistry.data[i].fp_makeDSP();
			if (newDSP) {
				newDSP->owned = true;
				azaTrackInsertDSP(track, newDSP, contextMenuTrackFXDSP);
			} else {
				snprintf(contextMenuError, sizeof(contextMenuError), "Failed to make \"%s\": Out of memory!\n", name);
				AZA_LOG_ERR(contextMenuError);
				azagContextMenuOpen(azagContextMenuErrorReport);
			}
		}
	}

	azagDrawContextMenuEnd();
}




// Tracks




static void azagDrawTrackFX(azaTrack *track, uint32_t metadataIndex, azagRect bounds) {
	azagDrawRectGradientV(bounds, azagThemeCurrent.track.colorFXBGTop, azagThemeCurrent.track.colorFXBGBot);
	azagRectShrinkMargin(&bounds, 0);
	azagPushScissor(bounds);
	azaTrackGUIMetadata *metadata = &azaTrackGUIMetadatas.data[metadataIndex];
	azagRect pluginRect = bounds;
	pluginRect.y += metadata->scrollFXY;
	pluginRect.h = azagGetFontSizeForScale(AZAG_TEXT_SCALE_TEXT) + azagThemeCurrent.margin.y * 2;
	azagRect muteRect = pluginRect;
	azagRectShrinkRightMargin(&pluginRect, pluginRect.h);
	azagRectShrinkLeftMargin(&muteRect, pluginRect.w);
	azaDSP *mouseoverDSP = NULL;
	for (uint32_t i = 0; i < track->plugins.steps.count; i++) {
		azaDSP *dsp = track->plugins.steps.data[i].dsp;
		bool mouseover = azagMouseInRect(pluginRect);
		if (mouseover) {
			azagDrawRectGradientV(pluginRect, azagThemeCurrent.dspChain.colorHighlightBGTop, azagThemeCurrent.dspChain.colorHighlightBGBot);
			if (azagMousePressed(AZAG_MOUSE_BUTTON_LEFT)) {
				if (selectedDSP) {
					selectedDSP->selected = 0;
				}
				selectedDSP = dsp;
				// TODO: Replace this 1 with whatever layer we have active
				dsp->selected = 1;
			}
			mouseoverDSP = dsp;
		} else if (azagMouseInRect(muteRect)) {
			if (dsp->error) {
				azagTooltipAdd("Click to Clear Error", (azagPoint) {muteRect.x + muteRect.w, muteRect.y + muteRect.h/2}, 0.0f, 0.5f);
				if (azagMousePressed(AZAG_MOUSE_BUTTON_LEFT)) {
					dsp->error = 0;
				}
			} else {
				azagTooltipAdd("Bypass", (azagPoint) {muteRect.x + muteRect.w, muteRect.y + muteRect.h/2}, 0.0f, 0.5f);
				if (azagMousePressed(AZAG_MOUSE_BUTTON_LEFT)) {
					dsp->bypass = !dsp->bypass;
				}
			}
		}
		azagDrawRectOutline(pluginRect, dsp == selectedDSP ? azagThemeCurrent.dspChain.colorBorderSelected : azagThemeCurrent.dspChain.colorBorder);
		azagDrawText(dsp->name, azagPointAdd(pluginRect.xy, azagThemeCurrent.margin), AZAG_TEXT_SCALE_TEXT, azagThemeCurrent.dspChain.colorText);
		// Bypass/error
		if (dsp->error) {
			azagDrawRect(muteRect, azagThemeCurrent.dspChain.colorError);
		} else {
			if (dsp->bypass) {
				azagDrawRect(muteRect, azagThemeCurrent.dspChain.colorBypass);
			} else {
				azagDrawRectOutline(muteRect, azagThemeCurrent.dspChain.colorBypass);
			}
		}
		pluginRect.y += pluginRect.h + azagThemeCurrent.margin.y;
		muteRect.y += pluginRect.h + azagThemeCurrent.margin.y;
	}
	if (azagMouseInRect(bounds)) {
		bool trackFXCanScrollDown = (pluginRect.y + pluginRect.h > bounds.y + bounds.h);
		int scroll = (int)(azagMouseWheelV() * 8.0f);
		if (!trackFXCanScrollDown && scroll < 0) scroll = 0;
		metadata->scrollFXY += scroll;
		if (metadata->scrollFXY > 0) metadata->scrollFXY = 0;
		trackFXCanScrollDown = false;
	}
	azagPopScissor();
	if (azagMousePressedInRect(AZAG_MOUSE_BUTTON_RIGHT, bounds)) {
		azagContextMenuSetIndexFromTrack(track);
		contextMenuTrackFXDSP = mouseoverDSP;
		azagContextMenuOpen(azagContextMenuTrackFX);
	}
}

static const int trackMeterDBRange = 72;
static const int trackMeterDBHeadroom = 12;
static const int trackFaderDBRange = 72;
static const int trackFaderDBHeadroom = 12;

// returns used width
static int azagDrawTrackControls(azaTrack *track, uint32_t metadataIndex, azagRect bounds) {
	azaTrackGUIMetadata *metadata = &azaTrackGUIMetadatas.data[metadataIndex];
	int takenWidth = bounds.w = metadata->width;
	metadata->width = azagThemeCurrent.margin.x;
	bool openedContextMenu = false;
	if (azagMousePressedInRect(AZAG_MOUSE_BUTTON_RIGHT, bounds)) {
		azagContextMenuSetIndexFromTrack(track);
		azagContextMenuOpen(azagContextMenuTrack);
		contextMenuTrackSend = NULL;
		openedContextMenu = true;
	}
	azagDrawRectGradientV(bounds, azagThemeCurrent.track.colorControlsBGTop, azagThemeCurrent.track.colorControlsBGBot);
	azagRectShrinkMargin(&bounds, 0);
	// Fader
	int usedWidth = azagDrawFader(bounds, &track->gain, &track->mute, false, "Track Gain", trackFaderDBRange, trackFaderDBHeadroom);
	usedWidth += azagThemeCurrent.margin.x;
	metadata->width += usedWidth;
	azagRectShrinkLeft(&bounds, usedWidth);
	// Meter
	azagRect meterRect = bounds;
	azagRectCutOutFaderMuteButton(&meterRect);
	usedWidth = azagDrawMeters(&track->meters, meterRect, trackMeterDBRange, trackMeterDBHeadroom);
	usedWidth += azagThemeCurrent.margin.x;
	metadata->width += usedWidth;
	azagRectShrinkLeft(&bounds, usedWidth);
	// Sends
	azagPushScissor(bounds);
	azaTrackRoute *receive = azaTrackGetReceive(track, &currentMixer->master);
	if (receive) {
		usedWidth = azagDrawFader(bounds, &receive->gain, &receive->mute, false, "Master Send", trackFaderDBRange, trackFaderDBHeadroom);
		usedWidth += azagThemeCurrent.margin.x;
		metadata->width += usedWidth;
		if (openedContextMenu && azagMouseInRect((azagRect) { .xy = bounds.xy, .w = usedWidth, .h = bounds.h })) {
			contextMenuTrackSend = &currentMixer->master;
		}
		azagRectShrinkLeft(&bounds, usedWidth);
	}
	for (uint32_t i = 0; i < currentMixer->tracks.count; i++) {
		receive = azaTrackGetReceive(track, currentMixer->tracks.data[i]);
		if (receive) {
			usedWidth = azagDrawFader(bounds, &receive->gain, &receive->mute, false, azaTextFormat("%s Send", currentMixer->tracks.data[i]->name), trackFaderDBRange, trackFaderDBHeadroom);
			usedWidth += azagThemeCurrent.margin.x;
			metadata->width += usedWidth;
			if (openedContextMenu && azagMouseInRect((azagRect) { .xy = bounds.xy, .w = usedWidth, .h = bounds.h })) {
				contextMenuTrackSend = currentMixer->tracks.data[i];
			}
			azagRectShrinkLeft(&bounds, usedWidth);
		}
	}
	azagPopScissor();
	metadata->width = AZA_MAX(metadata->width, azagThemeCurrent.track.size.x);
	return takenWidth;
}

// returns used width
static int azagDrawTrack(azaTrack *track, uint32_t metadataIndex, azagRect bounds) {
	azagRectShrinkAllXY(&bounds, azagThemeCurrent.margin);
	int labelDrawHeight = azagGetFontSizeForScale(AZAG_TEXT_SCALE_TEXT) + azagThemeCurrent.marginText.y * 2;
	int fxOffset = labelDrawHeight + azagThemeCurrent.margin.y;
	int controlsOffset = fxOffset + azagThemeCurrent.track.fxHeight + azagThemeCurrent.margin.y*2;
	azagRect controlsRect = {
		bounds.x,
		bounds.y + controlsOffset,
		bounds.w,
		bounds.h - controlsOffset,
	};
	int usedWidth = azagDrawTrackControls(track, metadataIndex, controlsRect);
	azagRect fxRect = {
		bounds.x,
		bounds.y + fxOffset,
		usedWidth,
		azagThemeCurrent.track.fxHeight,
	};
	azagRect nameRect = {
		bounds.x,
		bounds.y,
		usedWidth,
		labelDrawHeight,
	};
	azagDrawTextBox(nameRect, track->name, sizeof(track->name));
	azagDrawTrackFX(track, metadataIndex, fxRect);
	return usedWidth;
}



// Mixer



static void azagDrawMixer() {
	// TODO: This granularity might get in the way of audio processing. Probably only lock the mutex when it matters most.
	azaMutexLock(&currentMixer->mutex);
	int screenWidth = azagGetScreenWidth();
	int pluginDrawHeight = azagGetScreenHeight() - (azagThemeCurrent.track.size.y + azagThemeCurrent.scrollbar.thickness + azagThemeCurrent.margin.y);
	azagRect tracksRect = {
		azagThemeCurrent.margin.x,
		pluginDrawHeight,
		screenWidth - azagThemeCurrent.margin.x*2,
		azagThemeCurrent.track.size.y,
	};
	azagPushScissor(tracksRect);
	tracksRect.x += scrollTracksX;
	AZA_DA_RESERVE_COUNT(azaTrackGUIMetadatas, currentMixer->tracks.count+1, do{}while(0));
	azaTrackGUIMetadatas.count = currentMixer->tracks.count+1;
	int usedWidth = azagDrawTrack(&currentMixer->master, 0, tracksRect);
	int totalWidth = usedWidth;
	for (uint32_t i = 0; i < currentMixer->tracks.count; i++) {
		azagRectShrinkLeft(&tracksRect, usedWidth + azagThemeCurrent.track.spacing);
		usedWidth = azagDrawTrack(currentMixer->tracks.data[i], i+1, tracksRect);
		totalWidth += usedWidth + azagThemeCurrent.track.spacing;
	}
	azagPopScissor();
	azagRect scrollbarRect = {
		0,
		pluginDrawHeight + azagThemeCurrent.track.size.y + azagThemeCurrent.margin.y,
		screenWidth,
		azagThemeCurrent.scrollbar.thickness,
	};
	int scrollableWidth = screenWidth - (totalWidth + azagThemeCurrent.margin.x*2);
	azagDrawScrollbarHorizontal(scrollbarRect, &scrollTracksX, AZA_MIN(scrollableWidth, 0), 0, -azagThemeCurrent.track.size.x / 3);
	azagTooltipAdd(azaTextFormat("CPU: %.2f%%", currentMixer->cpuPercentSlow), (azagPoint) {screenWidth, 0}, 0.0f, 0.0f);
	if (currentMixer->hasCircularRouting) {
		azagTooltipAddError("Circular Routing Detected!!!", (azagPoint) {0, pluginDrawHeight}, 0.0f, 1.0f);
	}
	azaMutexUnlock(&currentMixer->mutex);
}



static void azagDrawSelectedDSP() {
	azaMutexLock(&currentMixer->mutex);
	int pluginDrawHeight = azagGetScreenHeight() - (azagThemeCurrent.track.size.y + azagThemeCurrent.scrollbar.thickness);
	azagRect bounds = {
		.xy = azagPointMulScalar(azagThemeCurrent.margin, 2),
		azagGetScreenWidth() - azagThemeCurrent.margin.x*4,
		pluginDrawHeight - azagThemeCurrent.margin.y*4,
	};
	azagDrawRectGradientV(bounds, azagThemeCurrent.plugin.colorBGTop, azagThemeCurrent.plugin.colorBGBot);
	azagDrawRectOutline(bounds, azagThemeCurrent.plugin.colorBorder);
	if (!selectedDSP) goto done;

	azagRectShrinkAllXY(&bounds, azagPointMulScalar(azagThemeCurrent.margin, 2));
	azagDrawTextMargin(selectedDSP->name, bounds.xy, AZAG_TEXT_SCALE_HEADER, azagThemeCurrent.plugin.colorPluginName);
	azagRectShrinkTop(&bounds, azagTextHeightMargin(selectedDSP->name, AZAG_TEXT_SCALE_HEADER));
	if (selectedDSP->fp_draw) {
		selectedDSP->fp_draw(selectedDSP, bounds);
	}
done:
	azaMutexUnlock(&currentMixer->mutex);
}




// API




static azaThread thread;
static bool isWindowOpen = false;
static const int pluginDrawHeightDefault = 200;
static bool alwaysOnTop = false;

static AZA_THREAD_PROC_DEF(azaMixerGUIThreadProc, userdata) {
	int width = AZA_MIN((azagThemeCurrent.track.size.x + azagThemeCurrent.margin.x*2) * (1 + currentMixer->tracks.count) + azagThemeCurrent.margin.x*2, 640);
	int height = pluginDrawHeightDefault + azagThemeCurrent.track.size.y + azagThemeCurrent.scrollbar.thickness;
	mixerWindow = azagWindowCreate(width, height, "AzAudio Mixer");
	if (mixerWindow == AZAG_WINDOW_INVALID) {
		AZA_LOG_ERR("We couldn't create a window for the mixer!\n");
		return 0;
	}
	azagWindowSetCurrent(mixerWindow);
	if (azagWindowOpen()) {
		AZA_LOG_ERR("We couldn't open a window for the mixer!\n");
		azagWindowDestroy(mixerWindow);
		return 0;
	}

	while (!azagWindowShouldClose()) {
		if (!isWindowOpen) break;
		azagWindowSetAlwaysOnTop(alwaysOnTop);
		azagBeginDrawing();
			azagClearBackground(azagThemeCurrent.colorBG);
			azagDrawMixer();
			azagDrawSelectedDSP();
		azagEndDrawing();
	}
	isWindowOpen = false;
	azagWindowClose();
	azagWindowDestroy(mixerWindow);
	return 0;
}

void azaMixerGUIOpen(azaMixer *mixer, bool onTop) {
	assert(mixer != NULL);
	currentMixer = mixer;
	alwaysOnTop = onTop;
	if (isWindowOpen) return;
	if (azaThreadJoinable(&thread)) {
		azaThreadJoin(&thread); // Unlikely, but not impossible
	}
	if (azaThreadLaunch(&thread, azaMixerGUIThreadProc, NULL)) {
		AZA_LOG_ERR("azaMixerGUIOpen error: Failed to launch thread (errno %i)\n", errno);
	}
	isWindowOpen = true;
}

void azaMixerGUIClose() {
	if (!isWindowOpen) return;
	isWindowOpen = false;
	if (azaThreadJoinable(&thread)) {
		azaThreadJoin(&thread);
	}
}

bool azaMixerGUIIsOpen() {
	return isWindowOpen;
}

bool azaMixerGUIHasDSPOpen(azaDSP *dsp) {
	return dsp == selectedDSP;
}

void azaMixerGUIUnselectDSP(azaDSP *dsp) {
	// TODO: This is still hacky. Probably invert the responsibility such that DSP knows when it's selected instead. This would also allow us to have multiple selected at once, which we almost definitely want.
	if (dsp == selectedDSP) {
		selectedDSP = NULL;
	}
}

void azaMixerGUIShowError(const char *message) {
	aza_strcpy(contextMenuError, message, sizeof(contextMenuError));
	azagContextMenuOpen(azagContextMenuErrorReport);
}