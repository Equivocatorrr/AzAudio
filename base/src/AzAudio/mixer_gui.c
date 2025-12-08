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
#include <errno.h>

#if defined(__GNUC__)
// Suppress some unused function warnings
#pragma GCC diagnostic ignored "-Wunused-function"
#endif




// Global state




static azaMixer *currentMixer = NULL;
static int dspSelectionLayer = 0;
static float dspSelectionScroll = 0;
static azagWindow mixerWindow = AZAG_WINDOW_INVALID;

static float scrollTracksX = 0.0f;
// Allows us to target a specific track to scroll towards
static int trackScrollTarget = -1;

typedef struct azaTrackGUIMetadata {
	float scrollFXY;
	float scrollSendsX;
	float width;
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
		// Scroll to see new track
		trackScrollTarget = contextMenuTrackIndex+1;
	}
	if (doRemoveTrack) {
		if (azagDrawContextMenuButton("Remove Track")) {
			azagContextMenuOpenSub(azagContextMenuTrackRemove);
		}
	}
	if (azagDrawContextMenuButton("Add Send")) {
		azagContextMenuOpenSub(azagContextMenuSendAdd);
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
	azagDrawContextMenuBegin("Add Send To:");

	azaTrack *target = &currentMixer->master;
	azaTrack *track = azagContextMenuTrackFromIndex();
	for (int32_t i = 0; i < (int32_t)currentMixer->tracks.count+1; target = currentMixer->tracks.data[i++]) {
		if (i == contextMenuTrackIndex) {
			continue; // Skip self
		}
		if (azaTrackGetReceive(track, target) != NULL) {
			continue; // Don't include sends that already exist
		}
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
		azagContextMenuOpenSub(azagContextMenuTrackFXAdd);
	}
	if (doRemovePlugin) {
		if (azagDrawContextMenuButton(azaTextFormat("Remove %s", contextMenuTrackFXDSP->guiMetadata.name))) {
			azaTrackRemoveDSP(azagContextMenuTrackFromIndex(), contextMenuTrackFXDSP);
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
		if (azaDSPRegistry.data[i].base.pFuncs->fp_makeDefault == NULL) continue;
		const char *name = azaDSPRegistry.data[i].base.guiMetadata.name;
		if (azagDrawContextMenuButton(name)) {
			azaDSP *newDSP = azaDSPRegistry.data[i].base.pFuncs->fp_makeDefault();
			if (newDSP) {
				newDSP->header.owned = true;
				azaTrackInsertDSP(track, newDSP, contextMenuTrackFXDSP);
			} else {
				snprintf(contextMenuError, sizeof(contextMenuError), "Failed to make \"%s\": Out of memory!\n", name);
				AZA_LOG_ERR(contextMenuError);
				azagContextMenuOpenSub(azagContextMenuErrorReport);
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
	pluginRect.h = azagGetFontSizeForScale(AZAG_TEXT_SCALE_TEXT) + azagThemeCurrent.margin.y * 2.0f;
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
				azaMixerGUIDSPToggleSelection(dsp);
			}
			mouseoverDSP = dsp;
		} else if (azagMouseInRect(muteRect)) {
			azagSetMouseCursor(AZAG_MOUSE_CURSOR_POINTING_HAND);
			if (dsp->processMetadata.error) {
				azagTooltipAdd("Click to Clear Error", (azaVec2) {muteRect.x + muteRect.w, muteRect.y + muteRect.h/2}, (azaVec2) { 0.0f, 0.5f });
				if (azagMousePressed(AZAG_MOUSE_BUTTON_LEFT)) {
					dsp->processMetadata.error = 0;
				}
			} else {
				azagTooltipAdd("Bypass", (azaVec2) {muteRect.x + muteRect.w, muteRect.y + muteRect.h/2}, (azaVec2) { 0.0f, 0.5f });
				if (azagMousePressed(AZAG_MOUSE_BUTTON_LEFT)) {
					dsp->header.bypass = !dsp->header.bypass;
				}
			}
		}
		azagDrawRectOutline(pluginRect, azaMixerGUIDSPIsSelected(dsp) ? azagThemeCurrent.dspChain.colorBorderSelected : azagThemeCurrent.dspChain.colorBorder);
		azagDrawText(dsp->guiMetadata.name, azaAddVec2(pluginRect.xy, azagThemeCurrent.margin), AZAG_TEXT_SCALE_TEXT, azagThemeCurrent.dspChain.colorText);
		// Bypass/error
		if (dsp->processMetadata.error) {
			azagDrawRect(muteRect, azagThemeCurrent.dspChain.colorError);
		} else {
			if (dsp->header.bypass) {
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
		float scroll = azagMouseWheelV() * 8.0f;
		if (!trackFXCanScrollDown && scroll < 0) scroll = 0;
		metadata->scrollFXY += scroll;
		if (metadata->scrollFXY > 0) metadata->scrollFXY = 0;
		trackFXCanScrollDown = false;
	}
	azagPopScissor();
	if (azagMousePressedInRectDepth(AZAG_MOUSE_BUTTON_RIGHT, bounds, AZAG_MOUSE_DEPTH_CONTEXT_MENU)) {
		azagContextMenuSetIndexFromTrack(track);
		contextMenuTrackFXDSP = mouseoverDSP;
		azagContextMenuOpen(azagContextMenuTrackFX);
	}
}

static const float trackMeterDBRange = 72;
static const float trackMeterDBHeadroom = 12;
static const float trackFaderDBRange = 72;
static const float trackFaderDBHeadroom = 12;

// returns used width
static float azagDrawTrackControls(azaTrack *track, uint32_t metadataIndex, azagRect bounds) {
	azaTrackGUIMetadata *metadata = &azaTrackGUIMetadatas.data[metadataIndex];
	float takenWidth = bounds.w = metadata->width;
	metadata->width = azagThemeCurrent.margin.x;
	bool openedContextMenu = false;
	if (azagMousePressedInRectDepth(AZAG_MOUSE_BUTTON_RIGHT, bounds, AZAG_MOUSE_DEPTH_CONTEXT_MENU)) {
		azagContextMenuSetIndexFromTrack(track);
		azagContextMenuOpen(azagContextMenuTrack);
		contextMenuTrackSend = NULL;
		openedContextMenu = true;
	}
	azagDrawRectGradientV(bounds, azagThemeCurrent.track.colorControlsBGTop, azagThemeCurrent.track.colorControlsBGBot);
	azagRectShrinkMargin(&bounds, 0.0f);
	// Fader
	float usedWidth = azagDrawFader(bounds, &track->gain, &track->mute, false, "Track Gain", trackFaderDBRange, trackFaderDBHeadroom);
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
		if (openedContextMenu && azagMouseInRectDepth((azagRect) { .xy = bounds.xy, .w = usedWidth, .h = bounds.h }, AZAG_MOUSE_DEPTH_CONTEXT_MENU)) {
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
			if (openedContextMenu && azagMouseInRectDepth((azagRect) { .xy = bounds.xy, .w = usedWidth, .h = bounds.h }, AZAG_MOUSE_DEPTH_CONTEXT_MENU)) {
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
static float azagDrawTrack(azaTrack *track, uint32_t metadataIndex, azagRect bounds) {
	azagRectShrinkAllXY(&bounds, azagThemeCurrent.margin);
	float labelDrawHeight = azagGetFontSizeForScale(AZAG_TEXT_SCALE_TEXT) + azagThemeCurrent.marginText.y * 2.0f;
	float fxOffset = labelDrawHeight + azagThemeCurrent.margin.y;
	float controlsOffset = fxOffset + azagThemeCurrent.track.fxHeight + azagThemeCurrent.margin.y * 2.0f;
	azagRect controlsRect = {
		bounds.x,
		bounds.y + controlsOffset,
		bounds.w,
		bounds.h - controlsOffset,
	};
	float usedWidth = azagDrawTrackControls(track, metadataIndex, controlsRect);
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
	if (trackScrollTarget == metadataIndex) {
		bool offLeft = fxRect.x < azagThemeCurrent.margin.x;
		bool offRight = fxRect.x + fxRect.w > azagGetScreenWidth() - azagThemeCurrent.margin.x;
		if (offLeft && !offRight) {
			scrollTracksX += 5.0f;
		} else if (offRight && !offLeft) {
			scrollTracksX -= 5.0f;
		} else {
			trackScrollTarget = -1;
		}
	}
	return usedWidth;
}



// Mixer



static void azagDrawMixer() {
	// TODO: This granularity might get in the way of audio processing. Probably only lock the mutex when it matters most.
	azaMutexLock(&currentMixer->mutex);
	float screenWidth = azagGetScreenWidth();
	float pluginDrawHeight = azagGetScreenHeight() - (azagThemeCurrent.track.size.y + azagThemeCurrent.scrollbar.thickness + azagThemeCurrent.margin.y);
	azagRect tracksRect = {
		azagThemeCurrent.margin.x,
		pluginDrawHeight,
		screenWidth - azagThemeCurrent.margin.x * 2.0f,
		azagThemeCurrent.track.size.y,
	};
	azagPushScissor(tracksRect);
	tracksRect.x += scrollTracksX;
	float totalWidth = 0.0f;
	AZA_DA_RESERVE_COUNT(azaTrackGUIMetadatas, currentMixer->tracks.count+1,
		/* onAllocFail: */ AZA_LOG_ERR_ONCE("Failed to allocate %u track metadatas\n", currentMixer->tracks.count+1); goto noMetadatas);
	azaTrackGUIMetadatas.count = currentMixer->tracks.count+1;
	float usedWidth = azagDrawTrack(&currentMixer->master, 0, tracksRect);
	totalWidth = usedWidth;
	for (uint32_t i = 0; i < currentMixer->tracks.count; i++) {
		azagRectShrinkLeft(&tracksRect, usedWidth + azagThemeCurrent.track.spacing);
		usedWidth = azagDrawTrack(currentMixer->tracks.data[i], i+1, tracksRect);
		totalWidth += usedWidth + azagThemeCurrent.track.spacing;
	}
noMetadatas:
	azagPopScissor();
	azagRect scrollbarRect = {
		0,
		pluginDrawHeight + azagThemeCurrent.track.size.y + azagThemeCurrent.margin.y,
		screenWidth,
		azagThemeCurrent.scrollbar.thickness,
	};
	float scrollableWidth = screenWidth - (totalWidth + azagThemeCurrent.margin.x*2);
	azagDrawScrollbarHorizontal(scrollbarRect, &scrollTracksX, AZA_MIN(scrollableWidth, 0), 0, -azagThemeCurrent.track.size.x / 3);
	azagTooltipAdd(azaTextFormat("CPU: %.2f%%", currentMixer->cpuPercentSlow), (azaVec2) {screenWidth, 0}, (azaVec2) { 0.0f, 0.0f });
	if (currentMixer->hasCircularRouting) {
		azagTooltipAddError("Circular Routing Detected!!!", (azaVec2) {0, pluginDrawHeight}, (azaVec2) { 0.0f, 1.0f });
	}
	azaMutexUnlock(&currentMixer->mutex);
}

static const float pluginDrawHeightDefault = 200.0f;

static void azagDrawSelectedDSP() {
	azaMutexLock(&currentMixer->mutex);
	float pluginDrawHeight = azagGetScreenHeight() - (azagThemeCurrent.track.size.y + azagThemeCurrent.scrollbar.thickness);
	azagRect bounds = {
		.xy = azaMulVec2Scalar(azagThemeCurrent.margin, 2.0f),
		azagGetScreenWidth() - azagThemeCurrent.margin.x*4,
		pluginDrawHeight - azagThemeCurrent.margin.y*4,
	};
	azagPushScissor(bounds);
	int pluginLines = AZA_MAX(1, (int)(bounds.h * 4.0f / (3.0f * pluginDrawHeightDefault)));
	pluginDrawHeight /= pluginLines;
	bounds.h = pluginDrawHeight - azagThemeCurrent.margin.y*4;
	float offsetX = bounds.x + dspSelectionScroll;
	for (int32_t trackIndex = -1; trackIndex < (int32_t)currentMixer->tracks.count; trackIndex++) {
		azaTrack *track = trackIndex >= 0 ? currentMixer->tracks.data[trackIndex] : &currentMixer->master;
		bool once = true, again = false;
		for (uint32_t fxIndex = 0; fxIndex < track->plugins.steps.count; fxIndex++) {
			azaDSP *dsp = track->plugins.steps.data[fxIndex].dsp;
			if (dsp->pFuncs->fp_draw && azaMixerGUIDSPIsSelected(dsp)) {
				float width = dsp->guiMetadata.drawCurrentWidth + azagThemeCurrent.margin.x * 2.0f;
				{
					// Check if we need to go to a new line, using a guess as to how big the header will be
					// TODO: This could be improved to use the actual header size with a little bit of work. The guess works well enough for now though.
					float headerWidth = azagTextHeightMargin(track->name, AZAG_TEXT_SCALE_HEADER) + azagThemeCurrent.track.spacing;
					float right = offsetX + width + (once ? headerWidth : 0.0f);
					if (right > (azagGetScreenWidth() - azagThemeCurrent.margin.x * 2.0f)) {
						again = !once; // Only again if we've printed once already
						offsetX = azagThemeCurrent.margin.x * 2.0f + dspSelectionScroll;
						bounds.y += bounds.h + azagThemeCurrent.track.spacing;
					}
				}
				char trackNameHeader[64];
				if (again) {
					snprintf(trackNameHeader, sizeof(trackNameHeader), "%s Cont.", track->name);
				} else {
					aza_strcpy(trackNameHeader, track->name, sizeof(trackNameHeader));
				}
				if (once || again) {
					// Track name
					azaVec2 pos = {
						offsetX + azagThemeCurrent.marginText.y,
						bounds.y + azagThemeCurrent.marginText.x,
					};
					float headerHeight = azagTextWidthMargin(trackNameHeader, AZAG_TEXT_SCALE_HEADER);
					azagTextScale headerTextScale = AZAG_TEXT_SCALE_HEADER;
					if (headerHeight > bounds.h) {
						headerTextScale = AZAG_TEXT_SCALE_TEXT;
						headerHeight = azagTextWidthMargin(trackNameHeader, headerTextScale);
						if (headerHeight > bounds.h) {
							azagTextInsertNewlinesInPlace(trackNameHeader, sizeof(trackNameHeader), headerTextScale, bounds.h - azagThemeCurrent.marginText.x*2.0f, true);
						}
					}
					azagDrawTextRotated(trackNameHeader, pos, headerTextScale, azagThemeCurrent.colorText, 90.0f, (azaVec2) { 0.0f, 1.0f });
					offsetX += azagTextHeightMargin(trackNameHeader, headerTextScale) + azagThemeCurrent.track.spacing;
					once = false;
					again = false;
				}
				bounds.x = offsetX;
				bounds.w = width;
				azagDrawRectGradientV(bounds, azagThemeCurrent.plugin.colorBGTop, azagThemeCurrent.plugin.colorBGBot);
				azagDrawRectOutline(bounds, azagThemeCurrent.plugin.colorBorder);
				azagRect pluginRect = bounds;
				azagRectShrinkAllXY(&pluginRect, azaMulVec2Scalar(azagThemeCurrent.margin, 2.0f));
				azagPushScissor(pluginRect);
				char pluginNameHeader[64];
				azagTextScale pluginHeaderTextScale = AZAG_TEXT_SCALE_HEADER;
				if (azagTextWidthMargin(dsp->guiMetadata.name, AZAG_TEXT_SCALE_HEADER) > pluginRect.w) {
					pluginHeaderTextScale = AZAG_TEXT_SCALE_TEXT;
					azagTextInsertNewlines(pluginNameHeader, sizeof(pluginNameHeader), dsp->guiMetadata.name, pluginHeaderTextScale, pluginRect.w - azagThemeCurrent.marginText.x*2.0f, true);
				} else {
					aza_strcpy(pluginNameHeader, dsp->guiMetadata.name, sizeof(pluginNameHeader));
				}
				azagDrawTextMargin(pluginNameHeader, pluginRect.xy, pluginHeaderTextScale, azagThemeCurrent.plugin.colorPluginName);
				azagRectShrinkTopMargin(&pluginRect, azagTextHeightMargin(pluginNameHeader, pluginHeaderTextScale));
				dsp->pFuncs->fp_draw(dsp, pluginRect);
				azagPopScissor();
				if (dsp->guiMetadata.drawTargetWidth == 0) {
					azagRect rightScaleRect = {
						.x = offsetX + width - 5.0f,
						.y = bounds.y,
						.w = 10.0f,
						.h = bounds.h,
					};
					const float dragRegion = 2000.0f;
					const float widthMinimum = 50.0f;
					bool dragging = azagMouseDragFloat(rightScaleRect, &dsp->guiMetadata.drawCurrentWidth, false, dragRegion, false, widthMinimum, (widthMinimum + dragRegion), true, 0.0f, false, 25.0f, true);
					if (dragging || azagMouseInRect(rightScaleRect)) {
						azagSetMouseCursor(AZAG_MOUSE_CURSOR_RESIZE_H);
					}
					if (dsp->guiMetadata.drawCurrentWidth <= widthMinimum) {
						dsp->guiMetadata.drawCurrentWidth = widthMinimum;
					}
					const float dragWidgetHeight = bounds.h / 3.0f;
					float x = offsetX + width - 2.0f;
					const float y1 = bounds.y + bounds.h / 2.0f - dragWidgetHeight / 2.0f;
					azagDrawRect((azagRect) { x - 1.0f, y1 - 1.0f, 5.0f, dragWidgetHeight + 2.0f }, azagThemeCurrent.colorBG);
					azagDrawRect((azagRect) { x, y1, 1.0f, dragWidgetHeight }, azagThemeCurrent.colorAttenuation);
					x += 2.0f;
					azagDrawRect((azagRect) { x, y1, 1.0f, dragWidgetHeight }, azagThemeCurrent.colorAttenuation);
				} else {
					if (dsp->guiMetadata.drawCurrentWidth < dsp->guiMetadata.drawTargetWidth) {
						dsp->guiMetadata.drawCurrentWidth = azaMinf(dsp->guiMetadata.drawTargetWidth, dsp->guiMetadata.drawCurrentWidth + 10.0f);
					} else if (dsp->guiMetadata.drawCurrentWidth > dsp->guiMetadata.drawTargetWidth) {
						dsp->guiMetadata.drawCurrentWidth = azaMaxf(dsp->guiMetadata.drawTargetWidth, dsp->guiMetadata.drawCurrentWidth - 10.0f);
					}
				}
				offsetX += width + azagThemeCurrent.track.spacing; // TODO: Make a new theme entry for fx spacing
			}
		}
	}
	azagPopScissor();
	azaMutexUnlock(&currentMixer->mutex);
}




// API




static azaThread thread;
static bool isWindowOpen = false;
static bool alwaysOnTop = false;

static AZA_THREAD_PROC_DEF(azaMixerGUIThreadProc, userdata) {
	int width = AZA_MIN((int)((azagThemeCurrent.track.size.x + azagThemeCurrent.margin.x*2) * (1 + currentMixer->tracks.count) + azagThemeCurrent.margin.x*2), 640);
	int height = (int)(pluginDrawHeightDefault + azagThemeCurrent.track.size.y + azagThemeCurrent.scrollbar.thickness);
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
	AZA_DA_DEINIT(azaTrackGUIMetadatas);
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

bool azaMixerGUIDSPIsSelected(azaDSP *dsp) {
	return (dsp->guiMetadata.selected & (1u << dspSelectionLayer)) != 0;
}

void azaMixerGUIDSPSelect(azaDSP *dsp) {
	dsp->guiMetadata.selected |= (1u << dspSelectionLayer);
}

void azaMixerGUIDSPUnselect(azaDSP *dsp) {
	dsp->guiMetadata.selected &= ~(1u << dspSelectionLayer);
}

void azaMixerGUIDSPToggleSelection(azaDSP *dsp) {
	dsp->guiMetadata.selected ^= (1u << dspSelectionLayer);
}

void azaMixerGUIShowError(const char *message) {
	aza_strcpy(contextMenuError, message, sizeof(contextMenuError));
	azagContextMenuOpen(azagContextMenuErrorReport);
}
