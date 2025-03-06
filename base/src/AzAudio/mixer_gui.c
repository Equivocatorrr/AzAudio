/*
	File: mixer_gui.c
	Author: Philip Haynes
	Implementation of a GUI for interacting with an azaMixer on-the-fly, all at the convenience of a single function call!
*/

#include <raylib.h>

#include "backend/threads.h"
#include "backend/timer.h"
#include "AzAudio.h"
#include "helpers.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "mixer.h"

static void azaRaylibTraceLogCallback(int logLevel, const char *text, va_list args) {
	AzaLogLevel myLevel = AZA_LOG_LEVEL_INFO;
	switch (logLevel) {
		case LOG_ALL:
		case LOG_TRACE:
		case LOG_DEBUG:
			myLevel = AZA_LOG_LEVEL_TRACE;
			break;
		case LOG_INFO:
		case LOG_WARNING:
			myLevel = AZA_LOG_LEVEL_INFO;
			break;
		case LOG_ERROR:
		case LOG_FATAL:
			myLevel = AZA_LOG_LEVEL_ERROR;
			break;
		case LOG_NONE:
			myLevel = AZA_LOG_LEVEL_NONE;
			break;
	}
	char buffer[256];
	vsnprintf(buffer, sizeof(buffer), text, args);
	azaLog(myLevel, "raylib: %s\n", buffer);
}




// Global state




static azaMixer *currentMixer = NULL;
static azaDSP *selectedDSP = NULL;
static bool isWindowOpen = false;
static bool isWindowTopmost = false;
static azaThread thread = {};

static int64_t lastClickTime = 0;




// Constants for size, color, etc.




static const int pluginDrawHeight = 200;
static const int trackDrawWidth = 110;
static const int trackDrawHeight = 300;
static const int trackFXDrawHeight = 60;
static const int trackLabelDrawHeight = 20;
static const int margin = 2;
static const int textMargin = 5;
static const int scrollX = 0;
static int trackFXScrollY = 0;
static bool trackFXCanScrollDown = false;

static const int meterDrawWidth = 4;
static const float meterDBRange = 48.0f;

static const int faderDrawWidth = 14;

static const Color colorBG = { 15, 25, 50, 255 };

static const Color colorPluginBorderSelected = { 200, 150, 100, 255 };
static const Color colorPluginBorder = { 100, 120, 150, 255 };

static const Color colorPluginSettingsTop = {  80,  80, 110, 255 };
static const Color colorPluginSettingsBot = {  50,  60,  80, 255 };

static const Color colorTrackFXTop = {  65,  65, 130, 255 };
static const Color colorTrackFXBot = {  40,  50,  80, 255 };

static const Color colorTrackControlsTop = {  50,  55,  90, 255 };
static const Color colorTrackControlsBot = {  30,  40,  60, 255 };

static const Color colorMeterBGTop       = {  20,  30,  40, 255 };
static const Color colorMeterBGBot       = {  10,  15,  20, 255 };
static const Color colorMeterDBTick      = {  50,  70, 100, 255 };
static const Color colorMeterDBTickUnity = { 100,  70,  50, 255 };
static const Color colorMeterPeak        = { 200, 220, 255, 255 };
static const Color colorMeterPeakUnity   = { 240, 180,  80, 255 };
static const Color colorMeterPeakOver    = { 255,   0,   0, 255 };
static const Color colorMeterRMS         = {   0, 255, 128, 255 };
static const Color colorMeterRMSOver     = { 255,   0,   0, 255 };

static const Color colorFaderBGTop       = {  20,  30,  40, 255 };
static const Color colorFaderBGBot       = {  10,  15,  20, 255 };
static const Color colorFaderDBTick      = {  50,  70, 100, 255 };
static const Color colorFaderDBTickUnity = { 100,  70,  50, 255 };
static const Color colorFaderMuteButton  = { 150,  50, 200, 255 };
static const Color colorFaderKnobTop     = { 160, 180, 200, 255 };
static const Color colorFaderKnobBot     = {  80, 120, 180, 255 };
static const Color colorFaderHighlight   = { 255, 255, 255,  64 };




// Utility functions/types




static int TextCountLines(const char *text) {
	if (!text) return 0;
	int result = 1;
	while (*text) {
		if (*text == '\n') result++;
		text++;
	}
	return result;
}

static int GetLogicalWidth() {
	return (int)((float)GetRenderWidth() / GetWindowScaleDPI().x);
}

static int GetLogicalHeight() {
	return (int)((float)GetRenderHeight() / GetWindowScaleDPI().y);
}

typedef struct azaRect {
	int x, y, w, h;
} azaRect;

static inline void DrawRect(azaRect rect, Color color) {
	DrawRectangle(rect.x, rect.y, rect.w, rect.h, color);
}

static inline void DrawRectLines(azaRect rect, Color color) {
	DrawRectangleLines(rect.x, rect.y, rect.w, rect.h, color);
}

static inline void DrawRectGradientV(azaRect rect, Color top, Color bottom) {
	DrawRectangleGradientV(rect.x, rect.y, rect.w, rect.h, top, bottom);
}

static inline void DrawRectGradientH(azaRect rect, Color left, Color right) {
	DrawRectangleGradientH(rect.x, rect.y, rect.w, rect.h, left, right);
}

static void azaRectShrinkMargin(azaRect *rect, int m) {
	rect->x += m;
	rect->y += m;
	rect->w -= m*2;
	rect->h -= m*2;
}

static void azaRectShrinkMarginH(azaRect *rect, int m) {
	rect->x += m;
	rect->w -= m*2;
}

static void azaRectShrinkMarginV(azaRect *rect, int m) {
	rect->y += m;
	rect->h -= m*2;
}

static void azaRectShrinkTop(azaRect *rect, int h) {
	rect->y += h;
	rect->h -= h;
}

static void azaRectShrinkLeft(azaRect *rect, int w) {
	rect->x += w;
	rect->w -= w;
}

static bool IsMouseInRect(azaRect rect) {
	Vector2 mouse = GetMousePosition();
	Vector2 dpi = GetWindowScaleDPI();
	mouse.x /= dpi.x;
	mouse.y /= dpi.y;
	int mx = (int)mouse.x;
	int my = (int)mouse.y;
	return (mx >= rect.x && my >= rect.y && mx <= rect.x+rect.w && my <= rect.y+rect.h);
}

static bool IsMousePressedInRect(int button, azaRect rect) {
	return IsMouseButtonPressed(button) && IsMouseInRect(rect);
}

static bool DidDoubleClick() {
	if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
		int64_t delta = azaGetTimestamp() - lastClickTime;
		int64_t delta_ns = azaGetTimestampDeltaNanoseconds(delta);
		if (delta_ns < 250 * 1000000) return true;
	}
	return false;
}




// Scissor stack




typedef struct azaScissorStack {
	azaRect scissors[32];
	int count;
} azaScissorStack;
static azaScissorStack scissorStack = {0};

static void PushScissor(azaRect rect) {
	assert(scissorStack.count < sizeof(scissorStack.scissors) / sizeof(scissorStack.scissors[0]));
	if (scissorStack.count > 0) {
		azaRect up = scissorStack.scissors[scissorStack.count-1];
		int right = rect.x + rect.w;
		int bottom = rect.y + rect.h;
		rect.x = AZA_MAX(rect.x, up.x);
		rect.y = AZA_MAX(rect.y, up.y);
		int upRight = up.x + up.w;
		int upBottom = up.y + up.h;
		right = AZA_MIN(right, upRight);
		bottom = AZA_MIN(bottom, upBottom);
		rect.w = right - rect.x;
		rect.h = bottom - rect.y;
	}
	BeginScissorMode(rect.x, rect.y, rect.w, rect.h);
	scissorStack.scissors[scissorStack.count] = rect;
	scissorStack.count++;
}

static void PopScissor() {
	assert(scissorStack.count > 0);
	scissorStack.count--;
	if (scissorStack.count > 0) {
		azaRect up = scissorStack.scissors[scissorStack.count-1];
		BeginScissorMode(up.x, up.y, up.w, up.h);
	} else {
		EndScissorMode();
	}
}




// Hovering Text/Tooltips




typedef struct azaTooltips {
	char buffer[1024];
	int posX[32];
	int posY[32];
	// How many bytes have already been written to buffer
	uint32_t bufferCount;
	// How many tooltips are represented
	uint32_t count;
} azaTooltips;

static azaTooltips tooltips = {0};

static void azaTooltipsReset() {
	tooltips.bufferCount = 0;
	tooltips.count = 0;
}

static void azaTooltipAdd(const char *text, int posX, int posY) {
	assert(text);
	assert(tooltips.bufferCount < sizeof(tooltips.buffer));
	assert(tooltips.count < sizeof(tooltips.posX) / sizeof(int));
	uint32_t count = (uint32_t)strlen(text) + 1;
	memcpy(tooltips.buffer + tooltips.bufferCount, text, count);
	tooltips.bufferCount += count;
	tooltips.posX[tooltips.count] = posX;
	tooltips.posY[tooltips.count] = posY;
	tooltips.count++;
}

static void azaTooltipsDraw() {
	const char *text = tooltips.buffer;
	for (uint32_t i = 0; i < tooltips.count; i++) {
		int width = MeasureText(text, 10);
		int height = 10 * TextCountLines(text);
		azaRect rect = {
			tooltips.posX[i],
			tooltips.posY[i],
			width + textMargin*2,
			height + textMargin*2,
		};
		int right = rect.x + rect.w;
		if (right > GetLogicalWidth()) {
			// Make sure our label doesn't go off screen
			rect.x = GetLogicalWidth() - rect.w;
		}
		DrawRect(rect, DARKGRAY);
		DrawText(text, rect.x + textMargin, rect.y + textMargin, 10, WHITE);
		text += strlen(text) + 1;
	}
	// We'll reset them here because whatever man, we already drew them
	azaTooltipsReset();
}




// Tracks




static void azaDrawTrackFX(azaTrack *track, azaRect bounds) {
	DrawRectGradientV(bounds, colorTrackFXTop, colorTrackFXBot);
	azaRectShrinkMargin(&bounds, margin);
	PushScissor(bounds);
	azaRect pluginRect = bounds;
	pluginRect.y += trackFXScrollY;
	pluginRect.h = 10 + margin * 2;
	azaDSP *dsp = track->dsp;
	while (dsp) {
		bool mouseover = IsMouseInRect(pluginRect);
		if (mouseover) {
			DrawRectGradientV(pluginRect, colorPluginBorderSelected, colorPluginBorder);
			if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
				selectedDSP = dsp;
			}
		}
		DrawRectLines(pluginRect, dsp == selectedDSP ? colorPluginBorderSelected : colorPluginBorder);
		DrawText(azaDSPKindString[(uint32_t)dsp->kind], pluginRect.x + margin, pluginRect.y + margin, 10, WHITE);
		dsp = dsp->pNext;
		pluginRect.y += pluginRect.h + margin;
	}
	if (pluginRect.y + pluginRect.h > bounds.y + bounds.h) trackFXCanScrollDown = true;
	PopScissor();
}

static int azaDBToYOffset(float db, float height, float dbRange) {
	return (int)AZA_MIN(db * height / dbRange, height);
}

static int azaDBToYOffsetClamped(float db, float height, float dbRange, int min) {
	int result = azaDBToYOffset(db, height, dbRange);
	return AZA_MAX(result, min);
}

static void azaDrawDBTicks(azaRect bounds, int dbRange, int dbOffset, Color color, Color colorUnity) {
	for (int i = 0; i <= dbRange; i++) {
		int db = i + dbOffset;
		Color myColor = i == dbOffset ? colorUnity : color;
		myColor.a = 64 + (db%6==0)*128 + (db%3==0)*63;
		int yOffset = i * bounds.h / dbRange;
		DrawLine(bounds.x, bounds.y+yOffset, bounds.x+bounds.w, bounds.y+yOffset, myColor);
	}
}

// returns used width
static int azaDrawFader(azaRect bounds, float *gain, bool *mute, const char *label) {
	bounds.w = faderDrawWidth;
	if(IsMouseInRect(bounds)) {
		azaTooltipAdd(label, bounds.x, bounds.y - (10 * TextCountLines(label) + textMargin*2));
	}
	DrawRectGradientV(bounds, colorFaderBGTop, colorFaderBGBot);
	azaRectShrinkMargin(&bounds, margin);
	if (mute) {
		azaRect muteRect = bounds;
		muteRect.h = muteRect.w;
		if (IsMousePressedInRect(MOUSE_BUTTON_LEFT, muteRect)) {
			*mute = !*mute;
		}
		if (*mute) {
			DrawRectangle(muteRect.x, muteRect.y, muteRect.w, muteRect.h, colorFaderMuteButton);
		} else {
			DrawRectangleLines(muteRect.x, muteRect.y, muteRect.w, muteRect.h, colorFaderMuteButton);
		}
		azaRectShrinkTop(&bounds, muteRect.h + margin);
	}
	azaDrawDBTicks((azaRect) {
		bounds.x - margin,
		bounds.y,
		bounds.w + margin*2,
		bounds.h,
	}, (int)meterDBRange, 6, colorFaderDBTick, colorFaderDBTickUnity);
	if (IsMouseInRect(bounds)) {
		DrawRect(bounds, colorFaderHighlight);
		azaTooltipAdd(TextFormat("%+.1fdb", *gain), bounds.x + bounds.w + margin, bounds.y + bounds.h * 6 / (int)meterDBRange);
		float delta = GetMouseWheelMoveV().y;
		if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) delta /= 10.0f;
		*gain += delta;
		if (DidDoubleClick()) {
			*gain = 0.0f;
		}
	}
	PushScissor(bounds);
	int yOffset = azaDBToYOffsetClamped(6.0f - *gain, (float)bounds.h, meterDBRange, 0);
	DrawRectangleGradientV(bounds.x, bounds.y + yOffset - 6, bounds.w, 12, colorFaderKnobTop, colorFaderKnobBot);
	DrawLine(bounds.x, bounds.y + yOffset, bounds.x + bounds.w, bounds.y + yOffset, Fade(BLACK, 0.5));
	PopScissor();
	return faderDrawWidth;
}

// Returns used width
static int azaDrawMeters(azaMeters *meters, azaRect bounds, float dbRange) {
	int usedWidth = meterDrawWidth * meters->activeMeters + margin * (meters->activeMeters+1);
	bounds.w = usedWidth;
	DrawRectangleGradientV(bounds.x, bounds.y, bounds.w, bounds.h, colorMeterBGTop, colorMeterBGBot);
	bool resetPeaks = IsMousePressedInRect(MOUSE_BUTTON_LEFT, bounds);
	azaRectShrinkMarginV(&bounds, margin);
	azaDrawDBTicks(bounds, (int)dbRange, 0, colorMeterDBTick, colorMeterDBTickUnity);
	bounds.x += margin;
	bounds.w = meterDrawWidth;
	for (uint32_t c = 0; c < meters->activeMeters; c++) {
		float peakDB = aza_amp_to_dbf(meters->peaks[c]);
		int yOffset = azaDBToYOffsetClamped(-peakDB, (float)bounds.h, dbRange, -2);
		Color peakColor = colorMeterPeak;
		if (meters->peaks[c] == 1.0f) {
			peakColor = colorMeterPeakUnity;
		} else if (meters->peaks[c] > 1.0f) {
			peakColor = colorMeterPeakOver;
		}
		DrawLine(bounds.x, bounds.y + yOffset, bounds.x+bounds.w, bounds.y + yOffset, peakColor);
		if (true /* meters->processed */) {
			float rmsDB = aza_amp_to_dbf(sqrtf(meters->rmsSquaredAvg[c]));
			float peakShortTermDB = aza_amp_to_dbf(meters->peaksShortTerm[c]);
			yOffset = azaDBToYOffsetClamped(-peakShortTermDB, (float)bounds.h, dbRange, 0);
			DrawRectangle(bounds.x+bounds.w/4, bounds.y + yOffset, bounds.w/2, bounds.h - yOffset, colorMeterPeak);

			yOffset = azaDBToYOffsetClamped(-rmsDB, (float)bounds.h, dbRange, 0);
			DrawRectangle(bounds.x, bounds.y + yOffset, bounds.w, bounds.h - yOffset, colorMeterRMS);
			if (rmsDB > 0.0f) {
				yOffset = azaDBToYOffsetClamped(rmsDB, (float)bounds.h, dbRange, 0);
				DrawRectangle(bounds.x, bounds.y, bounds.w, yOffset, colorMeterRMSOver);
			}
			if (peakShortTermDB > 0.0f) {
				yOffset = azaDBToYOffsetClamped(peakShortTermDB, (float)bounds.h, dbRange, 0);
				DrawRectangle(bounds.x+bounds.w/4, bounds.y, bounds.w/2, yOffset, colorMeterPeakOver);
			}
		}
		bounds.x += bounds.w + margin;
		meters->rmsFrames = meters->rmsFrames * 7 / 8;
		if (resetPeaks) {
			meters->peaks[c] = 0.0f;
		}
		meters->peaksShortTerm[c] = 0.0f;
	}
	return usedWidth;
}

static void azaDrawTrackControls(azaTrack *track, azaRect bounds) {
	DrawRectangleGradientV(bounds.x, bounds.y, bounds.w, bounds.h, colorTrackControlsTop, colorTrackControlsBot);
	azaRectShrinkMargin(&bounds, margin);
	// Fader
	azaDrawFader(bounds, &track->gain, &track->mute, "Track Gain");
	azaRectShrinkLeft(&bounds, faderDrawWidth + margin);
	// Meter
	int metersWidth = azaDrawMeters(&track->meters, (azaRect) {
		bounds.x,
		bounds.y + (faderDrawWidth-margin), // Align with respect to the fader's mute button
		bounds.w,
		bounds.h - (faderDrawWidth-margin),
	}, meterDBRange);
	azaRectShrinkLeft(&bounds, metersWidth + margin);
	// Sends
	PushScissor(bounds);
	azaTrackRoute *receive = azaTrackGetReceive(track, &currentMixer->master);
	if (receive) {
		int faderWidth = azaDrawFader(bounds, &receive->gain, &receive->mute, "Master Send");
		azaRectShrinkLeft(&bounds, faderWidth + margin);
	}
	for (uint32_t i = 0; i < currentMixer->config.trackCount; i++) {
		receive = azaTrackGetReceive(track, &currentMixer->tracks[i]);
		if (receive) {
			int faderWidth = azaDrawFader(bounds, &receive->gain, &receive->mute, TextFormat("Track %d Send", i+1));
			azaRectShrinkLeft(&bounds, faderWidth + margin);
		}
	}
	PopScissor();
}

static void azaDrawTrack(azaTrack *track, azaRect bounds, char *name) {
	azaRectShrinkMargin(&bounds, margin);
	DrawText(name, bounds.x + textMargin, bounds.y + textMargin, 10, WHITE);
	azaRectShrinkTop(&bounds, trackLabelDrawHeight);
	azaDrawTrackFX(track, (azaRect) { bounds.x, bounds.y, bounds.w, trackFXDrawHeight });
	azaRectShrinkTop(&bounds, trackFXDrawHeight + margin*2);
	azaDrawTrackControls(track, bounds);
}




// Mixer




static void azaDrawMixer(azaMixer *mixer) {
	// TODO: This granularity might get in the way of audio processing. Probably only lock the mutex when it matters most.
	azaMutexLock(&mixer->mutex);
	azaRect trackRect = {
		scrollX + margin,
		pluginDrawHeight + margin,
		trackDrawWidth,
		trackDrawHeight - margin*2
	};
	azaDrawTrack(&mixer->master, trackRect, "Master");
	for (uint32_t i = 0; i < mixer->config.trackCount; i++) {
		trackRect.x += trackDrawWidth;
		char nameBuffer[32];
		snprintf(nameBuffer, sizeof(nameBuffer), "Track %d", (int)i + 1);
		azaDrawTrack(&mixer->tracks[i], trackRect, nameBuffer);
	}
	azaTooltipAdd(TextFormat("CPU: %.2f%%", mixer->cpuPercentSlow), GetLogicalWidth(), 0);
	azaMutexUnlock(&mixer->mutex);
}




// Controls for selected DSP




static void azaDrawLookaheadLimiter(azaLookaheadLimiter *data, azaRect bounds) {
	int faderWidth = azaDrawFader(bounds, &data->config.gainInput, NULL, "Input Gain");
	azaRectShrinkLeft(&bounds, faderWidth + margin);
	int metersWidth = azaDrawMeters(&data->metersInput, bounds, meterDBRange);
	azaRectShrinkLeft(&bounds, metersWidth + margin);
	azaDrawFader(bounds, &data->config.gainOutput, NULL, "Output Gain");
	azaRectShrinkLeft(&bounds, faderWidth + margin);
	azaDrawMeters(&data->metersOutput, bounds, meterDBRange);
}

static void azaDrawSelectedDSP() {
	azaRect bounds = {
		margin*2,
		margin*2,
		GetLogicalWidth() - margin*4,
		pluginDrawHeight - margin*4,
	};
	DrawRectGradientV(bounds, colorPluginSettingsTop, colorPluginSettingsBot);
	DrawRectLines(bounds, selectedDSP ? colorPluginBorderSelected : colorPluginBorder);
	if (!selectedDSP) return;

	azaRectShrinkMargin(&bounds, margin*2);
	DrawText(azaDSPKindString[(uint32_t)selectedDSP->kind], bounds.x + textMargin, bounds.y + textMargin, 20, WHITE);
	azaRectShrinkTop(&bounds, textMargin * 2 + 20);
	switch (selectedDSP->kind) {
		case AZA_DSP_NONE:
		case AZA_DSP_USER_SINGLE:
		case AZA_DSP_USER_DUAL:
		case AZA_DSP_CUBIC_LIMITER:
		case AZA_DSP_RMS:
			break;
		case AZA_DSP_LOOKAHEAD_LIMITER:
			azaDrawLookaheadLimiter((azaLookaheadLimiter*)selectedDSP, bounds);
			break;
		case AZA_DSP_FILTER:
		case AZA_DSP_COMPRESSOR:
		case AZA_DSP_DELAY:
		case AZA_DSP_REVERB:
		case AZA_DSP_SAMPLER:
		case AZA_DSP_GATE:
		case AZA_DSP_DELAY_DYNAMIC:
		case AZA_DSP_SPATIALIZE:
			break;
	}
}




// API




static AZA_THREAD_PROC_DEF(azaMixerGUIThreadProc, userdata) {
	unsigned int configFlags = FLAG_VSYNC_HINT | FLAG_WINDOW_HIGHDPI | FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT;
	if (isWindowTopmost) {
		configFlags |= FLAG_WINDOW_TOPMOST;
	}
	SetConfigFlags(configFlags);
	SetTraceLogLevel(LOG_ERROR);
	SetTraceLogCallback(azaRaylibTraceLogCallback);

	InitWindow(
		AZA_MIN(trackDrawWidth * (1 + currentMixer->config.trackCount) + margin*2, 640),
		pluginDrawHeight + trackDrawHeight,
		"AzAudio Mixer"
	);
	isWindowOpen = true;

	while (true) {
		if (!isWindowOpen) break;
		if (WindowShouldClose()) {
			isWindowOpen = false;
			break;
		}
		if (isWindowTopmost && !(configFlags & FLAG_WINDOW_TOPMOST)) {
			configFlags |= FLAG_WINDOW_TOPMOST;
			SetWindowState(FLAG_WINDOW_TOPMOST);
		} else if (!isWindowTopmost && (configFlags & FLAG_WINDOW_TOPMOST)) {
			configFlags &= ~FLAG_WINDOW_TOPMOST;
			ClearWindowState(FLAG_WINDOW_TOPMOST);
		}
		{
			int scroll = (int)GetMouseWheelMoveV().y * 8;
			if (!trackFXCanScrollDown && scroll < 0) scroll = 0;
			trackFXScrollY += scroll;
			if (trackFXScrollY > 0) trackFXScrollY = 0;
			trackFXCanScrollDown = false;
		}
		BeginDrawing();
			ClearBackground(colorBG);
			azaDrawMixer(currentMixer);
			azaDrawSelectedDSP();
			azaTooltipsDraw();
			if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
				lastClickTime = azaGetTimestamp();
			}
		EndDrawing();
	}

	CloseWindow();
	return 0;
}

void azaMixerGUIOpen(azaMixer *mixer, bool onTop) {
	assert(mixer != NULL);
	currentMixer = mixer;
	isWindowTopmost = onTop;
	if (isWindowOpen) return;
	if (azaThreadJoinable(&thread)) {
		azaThreadJoin(&thread); // Unlikely, but not impossible
	}
	if (azaThreadLaunch(&thread, azaMixerGUIThreadProc, NULL)) {
		AZA_LOG_ERR("azaMixerGUIOpen error: Failed to launch thread (errno %i)\n", errno);
	}
}

void azaMixerGUIClose() {
	if (!isWindowOpen) return;
	isWindowOpen = false;
	if (azaThreadJoinable(&thread)) {
		azaThreadJoin(&thread); // Unlikely, but not impossible
	}
}

bool azaMixerGUIIsOpen() {
	return isWindowOpen;
}

bool azaMixerGUIHasDSPOpen(azaDSP *dsp) {
	return dsp == selectedDSP;
}