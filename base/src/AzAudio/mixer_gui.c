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

#include "AzAudio.h"

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




static int currentDPIScale = 1;
static azaMixer *currentMixer = NULL;
static azaDSP *selectedDSP = NULL;
static bool isWindowOpen = false;
static bool isWindowTopmost = false;
static azaThread thread = {};

static int64_t lastClickTime = 0;

static int scrollTracksX = 0;

typedef struct azaTrackGUIMetadata {
	int scrollFXY;
	int scrollControlsX;
} azaTrackGUIMetadata;

static struct {
	azaTrackGUIMetadata *data;
	uint32_t count;
	uint32_t capacity;
} azaTrackGUIMetadatas = {0};




// Constants for size, color, etc.




static int pluginDrawHeight = 200;
static const int trackDrawWidth = 120;
static const int trackDrawHeight = 300;
static const int trackFXDrawHeight = 80;
static const int trackLabelDrawHeight = 20;
static const int margin = 2;
static const int textMargin = 5;


static const Color colorBG = { 15, 25, 50, 255 };

static const Color colorTooltipBGLeft       = {  15,  25,  35, 255 };
static const Color colorTooltipBGRight      = {  45,  75, 105, 255 };
static const Color colorTooltipBorder       = { 100, 200, 255, 255 };
static const Color colorTooltipErrorBGLeft  = {  40,   0,   0, 255 };
static const Color colorTooltipErrorBGRight = {  20,   0,   0, 255 };
static const Color colorTooltipErrorBorder  = { 200,   0,   0, 255 };

static const Color colorPluginBorderSelected = { 200, 150, 100, 255 };
static const Color colorPluginBorder = { 100, 120, 150, 255 };

static const Color colorPluginSettingsTop = {  80,  80, 110, 255 };
static const Color colorPluginSettingsBot = {  50,  60,  80, 255 };

static const Color colorTrackFXTop = {  65,  65, 130, 255 };
static const Color colorTrackFXBot = {  40,  50,  80, 255 };

static const Color colorTrackControlsTop = {  50,  55,  90, 255 };
static const Color colorTrackControlsBot = {  30,  40,  60, 255 };


static const int meterDrawWidth = 4;
static const int meterDBRange = 72;

static const Color colorMeterBGTop       = {  20,  30,  40, 255 };
static const Color colorMeterBGBot       = {  10,  15,  20, 255 };
static const Color colorMeterDBTick      = {  50,  70, 100, 255 };
static const Color colorMeterDBTickUnity = { 100,  70,  50, 255 };
static const Color colorMeterPeak        = { 200, 220, 255, 255 };
static const Color colorMeterPeakUnity   = { 240, 180,  80, 255 };
static const Color colorMeterPeakOver    = { 255,   0,   0, 255 };
static const Color colorMeterRMS         = {   0, 255, 128, 255 };
static const Color colorMeterRMSOver     = { 255,   0,   0, 255 };


static const int faderDrawWidth = 14;
static const int faderDBHeadroom = 12;

static const Color colorFaderMuteButton  = { 150,  50, 200, 255 };
static const Color colorFaderKnobTop     = { 160, 180, 200, 255 };
static const Color colorFaderKnobBot     = {  80, 120, 180, 255 };
static const Color colorFaderHighlight   = { 255, 255, 255,  64 };


static const int sliderDrawWidth = 14;

static const Color colorSliderBGTop      = {  20,  60,  40, 255 };
static const Color colorSliderBGBot      = {  10,  40,  20, 255 };


static const int scrollbarSize = 8;

static const Color colorScrollbarBG = {  30,  40,  60, 255 };
static const Color colorScrollbarFG = {  60,  80, 120, 255 };

static const int lookaheadLimiterMeterDBRange = 48;
static const int lookaheadLimiterAttenuationMeterDBRange = 12;
static const Color colorLookaheadLimiterAttenuation = {   0, 128, 255, 255 };

static const int compressorMeterDBRange = 48;
static const int compressorAttenuationMeterDBRange = 24;
static const Color colorCompressorAttenuation = {   0, 128, 255, 255 };






// Utility functions/types




static Color ColorHSV(float hue, float sat, float val, uint8_t alpha) {
	float r, g, b;
	r = g = b = 0.0f;
	int section = (int)(hue * 6.0f);
	float fraction = hue * 6.0f - (float)section;
	section %= 6;
	switch (section) {
		case 0:
			r = (1.0f)*val;
			g = azaLerpf(1.0f, fraction, sat)*val;
			b = 1.0f - sat;
			break;
		case 1:
			r = azaLerpf(1.0f, 1.0f - fraction, sat)*val;
			g = (1.0f)*val;
			b = 1.0f - sat;
			break;
		case 2:
			r = 1.0f - sat;
			g = (1.0f)*val;
			b = azaLerpf(1.0f, fraction, sat)*val;
			break;
		case 3:
			r = 1.0f - sat;
			g = azaLerpf(1.0f, 1.0f - fraction, sat)*val;
			b = (1.0f)*val;
			break;
		case 4:
			r = azaLerpf(1.0f, fraction, sat)*val;
			g = 1.0f - sat;
			b = (1.0f)*val;
			break;
		case 5:
			r = (1.0f)*val;
			g = 1.0f - sat;
			b = azaLerpf(1.0f, 1.0f - fraction, sat)*val;
			break;
	}
	Color result = {
		(uint8_t)(r * 255.0f),
		(uint8_t)(g * 255.0f),
		(uint8_t)(b * 255.0f),
		alpha,
	};
	return result;
}

static int TextCountLines(const char *text) {
	if (!text) return 0;
	int result = 1;
	while (*text) {
		if (*text == '\n') result++;
		text++;
	}
	return result;
}

// TODO: For raylib on glfw, wayland support appears incredibly broken, so I guess we can't have nice things for now. Check back later for updates to raylib that might address this (even if that just means they updated the version of glfw they ship.) In the meantime, just using X11 (current default for raylib) seems to work fine.

static int azaGetDPIScale() {
	int rounded = (int)roundf(GetWindowScaleDPI().x);
	return AZA_MAX(rounded, 1);
}

static void azaHandleDPIChanges() {
	int newDPI = azaGetDPIScale();
	if (newDPI != currentDPIScale) {
		// TODO: Handle any pixel-position-based state
		SetWindowSize(GetScreenWidth() * newDPI / currentDPIScale, GetScreenHeight() * newDPI / currentDPIScale);
		currentDPIScale = newDPI;
	}
}

static int GetLogicalWidth() {
	return GetRenderWidth() / currentDPIScale;
}

static int GetLogicalHeight() {
	return GetRenderHeight() / currentDPIScale;
}

static void azaDrawText(const char *text, int posX, int posY, int fontSize, Color color) {
	DrawText(text, posX * currentDPIScale, posY * currentDPIScale, fontSize * currentDPIScale, color);
}

static void azaDrawLine(int startPosX, int startPosY, int endPosX, int endPosY, Color color) {
	DrawLine(startPosX * currentDPIScale, startPosY * currentDPIScale, endPosX * currentDPIScale, endPosY * currentDPIScale, color);
}

typedef struct azaPoint {
	int x, y;
} azaPoint;

static inline azaPoint azaPointSub(azaPoint lhs, azaPoint rhs) {
	return (azaPoint) { lhs.x - rhs.x, lhs.y - rhs.y };
}

typedef struct azaRect {
	union {
		struct { int x, y; };
		azaPoint xy;
	};
	union {
		struct { int w, h; };
		azaPoint size;
	};
} azaRect;

static inline void azaDrawRect(azaRect rect, Color color) {
	DrawRectangle(rect.x * currentDPIScale, rect.y * currentDPIScale, rect.w * currentDPIScale, rect.h * currentDPIScale, color);
}

static inline void azaDrawRectLines(azaRect rect, Color color) {
	DrawRectangleLines(rect.x * currentDPIScale, rect.y * currentDPIScale, rect.w * currentDPIScale, rect.h * currentDPIScale, color);
}

static inline void azaDrawRectGradientV(azaRect rect, Color top, Color bottom) {
	DrawRectangleGradientV(rect.x * currentDPIScale, rect.y * currentDPIScale, rect.w * currentDPIScale, rect.h * currentDPIScale, top, bottom);
}

static inline void azaDrawRectGradientH(azaRect rect, Color left, Color right) {
	DrawRectangleGradientH(rect.x * currentDPIScale, rect.y * currentDPIScale, rect.w * currentDPIScale, rect.h * currentDPIScale, left, right);
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

static void azaRectShrinkBottom(azaRect *rect, int h) {
	rect->h -= h;
}

static void azaRectShrinkLeft(azaRect *rect, int w) {
	rect->x += w;
	rect->w -= w;
}

static void azaRectShrinkRight(azaRect *rect, int w) {
	rect->w -= w;
}

static void azaRectFitOnScreen(azaRect *rect) {
	int width = GetLogicalWidth();
	if ((rect->x + rect->w) > width) {
		rect->x = width - rect->w;
	}
	int height = GetLogicalHeight();
	if ((rect->y + rect->h) > height) {
		rect->y = height - rect->h;
	}
}

// Used for UI input culling (for context menus and such)
static int mouseDepth = 0;
static azaPoint mousePrev = {0};

static azaPoint azaMousePosition() {
	Vector2 mouse = GetMousePosition();
	return (azaPoint) { (int)mouse.x / currentDPIScale, (int)mouse.y / currentDPIScale };
}

static bool azaPointInRect(azaRect rect, azaPoint point) {
	return (point.x >= rect.x && point.y >= rect.y && point.x <= rect.x+rect.w && point.y <= rect.y+rect.h);
}

static bool azaMouseInRect(azaRect rect) {
	return azaPointInRect(rect, azaMousePosition());
}

static bool azaMouseButtonPressed(int button, int depth) {
	return (depth >= mouseDepth && IsMouseButtonPressed(button));
}

static bool azaMouseButtonDown(int button, int depth) {
	return (depth >= mouseDepth && IsMouseButtonDown(button));
}

static bool azaMousePressedInRect(int button, int depth, azaRect rect) {
	return azaMouseButtonPressed(button, depth) && azaMouseInRect(rect);
}

static bool azaDidDoubleClick(int depth) {
	if (azaMouseButtonPressed(MOUSE_BUTTON_LEFT, depth)) {
		int64_t delta = azaGetTimestamp() - lastClickTime;
		int64_t delta_ns = azaGetTimestampDeltaNanoseconds(delta);
		if (delta_ns < 250 * 1000000) return true;
	}
	return false;
}

static bool IsKeyRepeated(int key) {
	return (IsKeyPressed(key) || IsKeyPressedRepeat(key));
}

static void *mouseDragID = NULL;
static int64_t currentFrameTimestamp = 0;
static int64_t mouseDragTimestamp = 0;
static azaPoint mouseDragStart = {0};

// Don't use this directly (use azaCaptureMouse___)
static void azaMouseCaptureStart(void *id) {
	mouseDragID = id;
	mouseDepth = 2;
	mouseDragTimestamp = azaGetTimestamp();
	mouseDragStart = azaMousePosition();
}
// Don't use this directly (use azaCaptureMouse___)
static void azaMouseCaptureEnd() {
	mouseDragID = NULL;
	mouseDepth = 0;
}
// Call at the start of a frame
static void azaMouseCaptureStartFrame() {
	currentFrameTimestamp = azaGetTimestamp();
}
// Call at the end of a frame
static void azaMouseCaptureEndFrame() {
	// Handle resetting from ids disappearing (probably should never happen, but would be a really annoying bug if it did).
	if (mouseDragID != NULL && mouseDragTimestamp < currentFrameTimestamp) {
		azaMouseCaptureEnd();
	}
}
// Pass in a unique pointer identifier for continuity. A const char* can work just fine.
// returns true if we're capturing the mouse
// outputs the deltas from the last frame (if we're capturing, else zeroes them)
static bool azaCaptureMouseDelta(azaRect bounds, azaPoint *out_delta, void *id) {
	assert(out_delta);
	assert(id);
	azaPoint mouse = azaMousePosition();
	*out_delta = (azaPoint) {0};

	if (mouseDragID == NULL) {
		if (azaMouseButtonPressed(MOUSE_BUTTON_LEFT, 0) && azaPointInRect(bounds, mouse)) {
			azaMouseCaptureStart(id);
			return true;
		}
	} else if (mouseDragID == id) {
		if (!azaMouseButtonDown(MOUSE_BUTTON_LEFT, 2)) {
			azaMouseCaptureEnd();
			return false;
		}
		mouseDragTimestamp = azaGetTimestamp();
		*out_delta = azaPointSub(mouse, mousePrev);
		return true;
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
	BeginScissorMode(rect.x * currentDPIScale, rect.y * currentDPIScale, rect.w * currentDPIScale, rect.h * currentDPIScale);
	scissorStack.scissors[scissorStack.count] = rect;
	scissorStack.count++;
}

static void PopScissor() {
	assert(scissorStack.count > 0);
	scissorStack.count--;
	if (scissorStack.count > 0) {
		azaRect up = scissorStack.scissors[scissorStack.count-1];
		BeginScissorMode(up.x * currentDPIScale, up.y * currentDPIScale, up.w * currentDPIScale, up.h * currentDPIScale);
	} else {
		EndScissorMode();
	}
}





// Hovering Text/Tooltips




typedef struct azaTooltips {
	char buffer[1024];
	int posX[32];
	int posY[32];
	bool isError[32];
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

static void azaTooltipAdd(const char *text, int posX, int posY, bool isError) {
	assert(text);
	assert(tooltips.bufferCount < sizeof(tooltips.buffer));
	assert(tooltips.count < sizeof(tooltips.posX) / sizeof(int));
	uint32_t count = (uint32_t)strlen(text) + 1;
	memcpy(tooltips.buffer + tooltips.bufferCount, text, count);
	tooltips.bufferCount += count;
	tooltips.posX[tooltips.count] = posX;
	tooltips.posY[tooltips.count] = posY;
	tooltips.isError[tooltips.count] = isError;
	tooltips.count++;
}

static void azaDrawTooltips() {
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
		azaRectFitOnScreen(&rect);
		azaDrawRect((azaRect) {
			rect.x + 2,
			rect.y + 2,
			rect.w,
			rect.h,
		}, Fade(BLACK, 0.5f));
		if (tooltips.isError[i]) {
			azaDrawRectGradientH(rect, colorTooltipErrorBGLeft, colorTooltipErrorBGRight);
			azaDrawRectLines(rect, colorTooltipErrorBorder);
		} else {
			azaDrawRectGradientH(rect, colorTooltipBGLeft, colorTooltipBGRight);
			azaDrawRectLines(rect, colorTooltipBorder);
		}
		azaDrawText(text, rect.x + textMargin + 1, rect.y + textMargin + 1, 10, BLACK);
		azaDrawText(text, rect.x + textMargin, rect.y + textMargin, 10, WHITE);
		text += strlen(text) + 1;
	}
	// We'll reset them here because whatever man, we already drew them
	azaTooltipsReset();
}




// UI Utilities




static int azaDBToYOffset(float db, float height, float dbRange) {
	return (int)AZA_MIN(db * height / dbRange, height);
}

static int azaDBToYOffsetClamped(float db, int height, int minY, int dbRange) {
	int result = azaDBToYOffset(db, (float)height, (float)dbRange);
	return AZA_MAX(result, minY);
}

static void azaDrawDBTicks(azaRect bounds, int dbRange, int dbOffset, Color color, Color colorUnity) {
	for (int i = 0; i <= dbRange; i++) {
		int db = i + dbOffset;
		Color myColor = i == dbOffset ? colorUnity : color;
		myColor.a = (int)myColor.a * (64 + (db%6==0)*128 + (db%3==0)*63) / 255;
		int yOffset = i * bounds.h / dbRange;
		azaDrawLine(bounds.x, bounds.y+yOffset, bounds.x+bounds.w, bounds.y+yOffset, myColor);
	}
}

static inline void azaDrawMeterBackground(azaRect bounds, int dbRange, int dbHeadroom) {
	azaDrawRectGradientV(bounds, colorMeterBGTop, colorMeterBGBot);
	azaRectShrinkMarginV(&bounds, margin);
	azaDrawDBTicks(bounds, dbRange, dbHeadroom, colorMeterDBTick, colorMeterDBTickUnity);
}

// returns used width
static int azaDrawFader(azaRect bounds, float *gain, bool *mute, const char *label, int dbRange, int dbHeadroom) {
	bounds.w = faderDrawWidth;
	bool mouseover = azaMouseInRect(bounds);
	if (mouseover) {
		azaTooltipAdd(label, bounds.x, bounds.y - (10 * TextCountLines(label) + textMargin*2), false);
	}
	if (mute) {
		azaRect muteRect = bounds;
		muteRect.h = muteRect.w;
		azaDrawRect(muteRect, colorMeterBGTop);
		azaRectShrinkMargin(&muteRect, margin);
		if (azaMouseInRect(muteRect)) {
			azaTooltipAdd("Mute", muteRect.x + muteRect.w, muteRect.y, false);
			if (azaMouseButtonPressed(MOUSE_BUTTON_LEFT, 0)) {
				*mute = !*mute;
			}
		}
		if (*mute) {
			azaDrawRect(muteRect, colorFaderMuteButton);
		} else {
			azaDrawRectLines(muteRect, colorFaderMuteButton);
		}
		azaRectShrinkTop(&bounds, muteRect.h + margin);
	}
	azaDrawMeterBackground(bounds, dbRange, dbHeadroom);
	azaRectShrinkMargin(&bounds, margin);
	int yOffset = azaDBToYOffsetClamped((float)dbHeadroom - *gain, bounds.h, 0, dbRange);
	if (mouseover) {
		azaDrawRect(bounds, colorFaderHighlight);
		azaTooltipAdd(TextFormat("%+.1fdb", *gain), bounds.x + bounds.w + margin, bounds.y + yOffset - (textMargin + 5), false);
		float delta = GetMouseWheelMoveV().y;
		if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) delta /= 10.0f;
		*gain += delta;
		if (azaDidDoubleClick(0)) {
			*gain = 0.0f;
		}
	}
	PushScissor(bounds);
	azaDrawRectGradientV((azaRect) {
		bounds.x,
		bounds.y + yOffset - 6,
		bounds.w,
		12
	}, colorFaderKnobTop, colorFaderKnobBot);
	azaDrawLine(bounds.x, bounds.y + yOffset, bounds.x + bounds.w, bounds.y + yOffset, Fade(BLACK, 0.5));
	PopScissor();
	return faderDrawWidth;
}

// Returns used width
static int azaDrawMeters(azaMeters *meters, azaRect bounds, int dbRange) {
	int usedWidth = meterDrawWidth * meters->activeMeters + margin * (meters->activeMeters+1);
	bounds.w = usedWidth;
	bool resetPeaks = azaMousePressedInRect(MOUSE_BUTTON_LEFT, 0, bounds);
	azaDrawMeterBackground(bounds, dbRange, 0);
	azaRectShrinkMarginV(&bounds, margin);
	bounds.x += margin;
	bounds.w = meterDrawWidth;
	for (uint32_t c = 0; c < meters->activeMeters; c++) {
		float peakDB = aza_amp_to_dbf(meters->peaks[c]);
		int yOffset = azaDBToYOffsetClamped(-peakDB, bounds.h, -2, dbRange);
		Color peakColor = colorMeterPeak;
		if (meters->peaks[c] == 1.0f) {
			peakColor = colorMeterPeakUnity;
		} else if (meters->peaks[c] > 1.0f) {
			peakColor = colorMeterPeakOver;
		}
		azaDrawLine(bounds.x, bounds.y + yOffset, bounds.x+bounds.w, bounds.y + yOffset, peakColor);
		if (true /* meters->processed */) {
			float rmsDB = aza_amp_to_dbf(sqrtf(meters->rmsSquaredAvg[c]));
			float peakShortTermDB = aza_amp_to_dbf(meters->peaksShortTerm[c]);
			yOffset = azaDBToYOffsetClamped(-peakShortTermDB, bounds.h, 0, dbRange);
			azaDrawRect((azaRect){
				bounds.x+bounds.w/4,
				bounds.y + yOffset,
				bounds.w/2,
				bounds.h - yOffset
			}, colorMeterPeak);

			yOffset = azaDBToYOffsetClamped(-rmsDB, bounds.h, 0, dbRange);
			azaDrawRect((azaRect){
				bounds.x,
				bounds.y + yOffset,
				bounds.w,
				bounds.h - yOffset
			}, colorMeterRMS);
			if (rmsDB > 0.0f) {
				yOffset = azaDBToYOffsetClamped(rmsDB, bounds.h, 0, dbRange);
				azaDrawRect((azaRect){
					bounds.x,
					bounds.y,
					bounds.w,
					yOffset
				}, colorMeterRMSOver);
			}
			if (peakShortTermDB > 0.0f) {
				yOffset = azaDBToYOffsetClamped(peakShortTermDB, bounds.h, 0, dbRange);
				azaDrawRect((azaRect){
					bounds.x+bounds.w/4,
					bounds.y,
					bounds.w/2,
					yOffset
				}, colorMeterPeakOver);
			}
		}
		bounds.x += bounds.w + margin;
		if (resetPeaks) {
			meters->peaks[c] = 0.0f;
		}
	}
	return usedWidth;
}

// Logarithmic slider allowing values between min and max.
// Scrolling up multiplies the value by (1.0f + step) and scrolling down divides it by the same value.
// Double clicking will set value to def.
// valueFormat is a printf-style format string, used for formatting the tooltip string, where the argument is *value. If NULL, defaults to "%+.1f"
// returns used width
static int azaDrawSliderFloatLog(azaRect bounds, float *value, float min, float max, float step, float def, const char *label, const char *valueFormat) {
	assert(min > 0.0f);
	assert(max > min);
	bounds.w = sliderDrawWidth;
	bool mouseover = azaMouseInRect(bounds);
	if (mouseover) {
		azaTooltipAdd(label, bounds.x, bounds.y - (10 * TextCountLines(label) + textMargin*2), false);
	}
	azaDrawRectGradientV(bounds, colorSliderBGTop, colorSliderBGBot);
	azaRectShrinkMargin(&bounds, margin);
	float logValue = logf(*value);
	float logMin = logf(min);
	float logMax = logf(max);
	int yOffset = (int)((float)bounds.h * (1.0f - (logValue - logMin) / (logMax - logMin)));
	if (mouseover) {
		azaDrawRect(bounds, colorFaderHighlight);
		if (!valueFormat) {
			valueFormat = "%+.1f";
		}
		azaTooltipAdd(TextFormat(valueFormat, *value), bounds.x + bounds.w + margin, bounds.y + yOffset - (textMargin + 5), false);
		float delta = GetMouseWheelMoveV().y;
		if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) delta /= 10.0f;
		if (delta > 0.0f) {
			*value *= (1.0f + delta*step);
		} else if (delta < 0.0f) {
			*value /= (1.0f - delta*step);
		}
		if (azaDidDoubleClick(0)) {
			*value = def;
		}
		*value = azaClampf(*value, min, max);
	}
	PushScissor(bounds);
	azaDrawRectGradientV((azaRect) {
		bounds.x,
		bounds.y + yOffset - 6,
		bounds.w,
		12
	}, colorFaderKnobTop, colorFaderKnobBot);
	azaDrawLine(bounds.x, bounds.y + yOffset, bounds.x + bounds.w, bounds.y + yOffset, Fade(BLACK, 0.5));
	PopScissor();
	return sliderDrawWidth;
}

// Linear slider allowing values between min and max.
// Scrolling up adds step and scrolling down subtracts step.
// Double clicking will set value to def.
// valueFormat is a printf-style format string, used for formatting the tooltip string, where the argument is *value. If NULL, defaults to "%+.1f"
// returns used width
static int azaDrawSliderFloat(azaRect bounds, float *value, float min, float max, float step, float def, const char *label, const char *valueFormat) {
	assert(max > min);
	bounds.w = sliderDrawWidth;
	bool mouseover = azaMouseInRect(bounds);
	if (mouseover) {
		azaTooltipAdd(label, bounds.x, bounds.y - (10 * TextCountLines(label) + textMargin*2), false);
	}
	azaDrawRectGradientV(bounds, colorSliderBGTop, colorSliderBGBot);
	azaRectShrinkMargin(&bounds, margin);
	int yOffset = (int)((float)bounds.h * (1.0f - (*value - min) / (max - min)));
	if (mouseover) {
		azaDrawRect(bounds, colorFaderHighlight);
		if (!valueFormat) {
			valueFormat = "%+.1f";
		}
		azaTooltipAdd(TextFormat(valueFormat, *value), bounds.x + bounds.w + margin, bounds.y + yOffset - (textMargin + 5), false);
		float delta = GetMouseWheelMoveV().y;
		if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) delta /= 10.0f;
		*value += delta*step;
		if (azaDidDoubleClick(0)) {
			*value = def;
		}
		*value = azaClampf(*value, min, max);
	}
	PushScissor(bounds);
	azaDrawRectGradientV((azaRect) {
		bounds.x,
		bounds.y + yOffset - 6,
		bounds.w,
		12
	}, colorFaderKnobTop, colorFaderKnobBot);
	azaDrawLine(bounds.x, bounds.y + yOffset, bounds.x + bounds.w, bounds.y + yOffset, Fade(BLACK, 0.5));
	PopScissor();
	return sliderDrawWidth;
}

static void azaTextCharInsert(char c, uint32_t index, char *text, uint32_t len, uint32_t capacity) {
	assert(index < capacity-1);
	for (uint32_t i = len; i > index; i--) {
		text[i] = text[i-1];
	}
	text[index] = c;
	text[len+1] = 0;
}

static void azaTextCharErase(uint32_t index, char *text, uint32_t len) {
	assert(index < len);
	for (uint32_t i = index; i < len; i++) {
		text[i] = text[i+1];
	}
	text[len] = 0;
}

static bool azaIsWhitespace(char c) {
	return c == ' ' || c == '\r' || c == '\n' || c == '\t';
}

static char *textboxTextBeingEdited = NULL;
static uint32_t textboxCursor = 0;
static bool textboxSelected = false;
static azaRect textboxBounds;

// If textCapacity is specified, this textbox will be editable.
static void azaDrawTextBox(azaRect bounds, char *text, uint32_t textCapacity) {
	bool mouseover = azaMouseInRect(bounds);
	uint32_t textLen = (uint32_t)strlen(text);
	if (textCapacity && mouseover && azaDidDoubleClick(0)) {
		textboxTextBeingEdited = text;
		textboxSelected = true;
		textboxCursor = textLen;
	}
	if (textboxTextBeingEdited == text) {
		if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)
		|| (!mouseover && azaMouseButtonPressed(MOUSE_BUTTON_LEFT, 0))) {
			textboxTextBeingEdited = NULL;
		}
	}
	if (textboxTextBeingEdited == text) {
		if (IsKeyRepeated(KEY_LEFT)) {
			if (textboxSelected) {
				textboxCursor = 0;
				textboxSelected = false;
			} else if (textboxCursor > 0) {
				textboxCursor--;
				if (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) {
					while (textboxCursor > 0) {
						textboxCursor--;
						if (textboxCursor > 0 && azaIsWhitespace(text[textboxCursor-1])) break;
					}
				}
			}
		}
		if (IsKeyRepeated(KEY_RIGHT)) {
			if (textboxSelected) {
				textboxCursor = textLen;
				textboxSelected = false;
			} else if (textboxCursor < textLen) {
				textboxCursor++;
				if (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) {
					while (textboxCursor < textLen) {
						textboxCursor++;
						if (azaIsWhitespace(text[textboxCursor-1])) break;
					}
				}
			}
		}
		if (IsKeyPressed(KEY_END)) {
			textboxCursor = textLen;
			textboxSelected = false;
		}
		if (IsKeyPressed(KEY_HOME)) {
			textboxCursor = 0;
			textboxSelected = false;
		}
		if (IsKeyRepeated(KEY_BACKSPACE)) {
			if (textboxSelected) {
				textLen = 0;
				textboxCursor = 0;
				textboxSelected = false;
				text[0] = 0;
			} else if (textboxCursor > 0) {
				azaTextCharErase(textboxCursor-1, text, textLen);
				textboxCursor--;
				textLen--;
			}
		}
		if (IsKeyRepeated(KEY_DELETE)) {
			if (textboxSelected) {
				textLen = 0;
				textboxCursor = 0;
				textboxSelected = false;
				text[0] = 0;
			} else if (textboxCursor < textLen) {
				azaTextCharErase(textboxCursor, text, textLen);
				textLen--;
			}
		}
		int c;
		while ((c = GetCharPressed()) && (textLen < textCapacity-1 || textboxSelected)) {
			if (c < 128) {
				if (textboxSelected) {
					textLen = 0;
					textboxCursor = 0;
					textboxSelected = false;
					text[0] = 0;
				}
				char cToAdd = (char)c;
				azaTextCharInsert(cToAdd, textboxCursor, text, textLen, textCapacity);
				textboxCursor++;
				textLen++;
			}
		}
		int textWidth = MeasureText(text, 10);
		if (textWidth > (bounds.w - textMargin * 2)) {
			bounds.w = textWidth + textMargin * 2;
			azaRectFitOnScreen(&bounds);
		}
		textboxBounds = bounds;
	} else {
		if (mouseover && MeasureText(text, 10) > (bounds.w - textMargin * 2)) {
			azaTooltipAdd(text, bounds.x, bounds.y, false);
		}
		PushScissor(bounds);
		azaDrawText(text, bounds.x + textMargin, bounds.y + textMargin, 10, WHITE);
		PopScissor();
	}
}

static void azaDrawTextboxBeingEdited() {
	if (!textboxTextBeingEdited) return;
	char *text = textboxTextBeingEdited;
	char holdover = text[textboxCursor];
	text[textboxCursor] = 0;
	int cursorX = MeasureText(text, 10) + textboxBounds.x + textMargin;
	text[textboxCursor] = holdover;
	int cursorY = textboxBounds.y + textMargin;
	azaDrawRect(textboxBounds, BLACK);
	azaDrawRectLines(textboxBounds, WHITE);
	azaRectShrinkMargin(&textboxBounds, textMargin);
	if (textboxSelected) {
		azaRect selectionRect = textboxBounds;
		selectionRect.w = MeasureText(text, 10);
		azaDrawRect(selectionRect, DARKGRAY);
	} else {
		azaDrawLine(cursorX, cursorY, cursorX, cursorY + 10, LIGHTGRAY);
	}
	azaDrawText(text, textboxBounds.x, textboxBounds.y, 10, WHITE);
}

// needs an id for mouse capture
static void azaDrawScrollbarHorizontal(azaRect bounds, int *value, int min, int max, int step, void *id) {
	assert(max >= min);
	bool mouseover = azaMouseInRect(bounds);
	int scrollbarWidth = bounds.w / 4;
	int useableWidth = bounds.w - scrollbarWidth;
	int mouseX = (int)azaMousePosition().x - bounds.x;
	azaDrawRect(bounds, colorScrollbarBG);
	if (mouseover) {
		int scroll = (int)GetMouseWheelMoveV().y;
		int click = (int)azaMouseButtonPressed(MOUSE_BUTTON_LEFT, 0) * ((int)(mouseX >= bounds.w/2) * 2 - 1);
		*value += step * (scroll + click);
		*value = AZA_CLAMP(*value, min, max);
	}
	if (min == max) return;
	azaRect knobRect = {
		bounds.x,
		bounds.y,
		scrollbarWidth,
		bounds.h,
	};
	int offset = useableWidth * (*value - min) / AZA_MAX(max - min, 1);
	if (step >= 0) {
		knobRect.x += offset;
	} else {
		knobRect.x += (bounds.w - scrollbarWidth) - offset;
	}
	azaDrawRect(knobRect, colorScrollbarFG);
	azaPoint delta;
	if (azaCaptureMouseDelta(knobRect, &delta, id)) {
		// TODO: Dragging the knob
	}
}




// Context Menus




static const int contextMenuItemWidth = 100;
// textMargin*2 + 10
static const int contextMenuItemHeight = 20;

typedef void (*fp_ContextMenuAction)();

typedef enum azaContextMenuKind {
	AZA_CONTEXT_MENU_NONE=0,
	AZA_CONTEXT_MENU_TRACK,
	AZA_CONTEXT_MENU_SEND_ADD,
	AZA_CONTEXT_MENU_SEND_REMOVE,
	AZA_CONTEXT_MENU_TRACK_FX,
	AZA_CONTEXT_MENU_TRACK_FX_ADD,
	AZA_CONTEXT_MENU_KIND_COUNT,
} azaContextMenuKind;

static void azaContextMenuOpen(azaContextMenuKind kind);



// Context Menu layout state



static azaContextMenuKind contextMenuKind = AZA_CONTEXT_MENU_NONE;
static azaRect contextMenuRect = {0};



// Specific context menu state



static int contextMenuTrackIndex = 0;
static azaDSP *contextMenuTrackFXDSP = NULL;

static azaTrack* azaContextMenuTrackFromIndex() {
	if (contextMenuTrackIndex <= 0) {
		return &currentMixer->master;
	} else {
		return currentMixer->tracks.data[contextMenuTrackIndex-1];
	}
}

static void azaContextMenuSetIndexFromTrack(azaTrack *track) {
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



// AZA_CONTEXT_MENU_TRACK



static void azaContextMenuTrackAdd() {
	AZA_LOG_TRACE("Track Add at index %d!\n", contextMenuTrackIndex);
	azaTrack *track;
	azaMixerAddTrack(currentMixer, contextMenuTrackIndex, &track, currentMixer->master.buffer.channelLayout, true);
	azaTrackSetName(track, TextFormat("Track %d", contextMenuTrackIndex));
	AZA_DA_INSERT(azaTrackGUIMetadatas, contextMenuTrackIndex+1, (azaTrackGUIMetadata){0}, do{}while(0));
}

static void azaContextMenuTrackRemove() {
	int toRemove = contextMenuTrackIndex-1;
	if (toRemove >= 0) {
		AZA_LOG_TRACE("Track Remove at index %d!\n", toRemove);
		azaMixerRemoveTrack(currentMixer, toRemove);
		AZA_DA_ERASE(azaTrackGUIMetadatas, contextMenuTrackIndex, 1);
	}
}

static void azaContextMenuTrackAddSend() {
	azaContextMenuOpen(AZA_CONTEXT_MENU_SEND_ADD);
}

static void azaContextMenuTrackRemoveSend() {
	azaContextMenuOpen(AZA_CONTEXT_MENU_SEND_REMOVE);
}

static const fp_ContextMenuAction contextMenuTrackActions[] = {
	azaContextMenuTrackAdd,
	azaContextMenuTrackRemove,
	azaContextMenuTrackAddSend,
	azaContextMenuTrackRemoveSend,
};
static const char *contextMenuTrackLabels[] = {
	"Add Track",
	"Remove Track",
	"Add Send",
	"Remove Send",
};



// Tables for generic context menus (non-dynamic ones)



static const uint32_t contextMenuActionCounts[] = {
	0,
	sizeof(contextMenuTrackActions) / sizeof(fp_ContextMenuAction),
	0, // Dynamic
	0, // Dynamic
	0, // Dynamic
	0, // Dynamic
};
static_assert((sizeof(contextMenuActionCounts) / sizeof(contextMenuActionCounts[0])) == AZA_CONTEXT_MENU_KIND_COUNT, "Pls update contextMenuActionCounts");
static const fp_ContextMenuAction *contextMenuActions[] = {
	NULL,
	contextMenuTrackActions,
	NULL, // Dynamo
	NULL, // Dynamo
	NULL, // Dynamo
	NULL, // Dynamo
};
static_assert((sizeof(contextMenuActions) / sizeof(contextMenuActions[0])) == AZA_CONTEXT_MENU_KIND_COUNT, "Pls update contextMenuActions");
static const char **contextMenuLabels[] = {
	NULL,
	contextMenuTrackLabels,
	NULL, // Hydrodynamic
	NULL, // Hydrodynamic
	NULL, // Hydrodynamic
	NULL, // Hydrodynamic
};
static_assert((sizeof(contextMenuLabels) / sizeof(contextMenuLabels[0])) == AZA_CONTEXT_MENU_KIND_COUNT, "Pls update contextMenuLabels");


// Context menu interface


static void azaContextMenuOpen(azaContextMenuKind kind) {
	assert(kind != AZA_CONTEXT_MENU_NONE);
	contextMenuKind = kind;
	contextMenuRect.xy = azaMousePosition();
	contextMenuRect.w = contextMenuItemWidth;
	contextMenuRect.h = contextMenuItemHeight * contextMenuActionCounts[(uint32_t)kind];
	azaRectFitOnScreen(&contextMenuRect);
	mouseDepth = 1;
}

static void azaContextMenuClose() {
	contextMenuKind = AZA_CONTEXT_MENU_NONE;
	mouseDepth = 0;
}

// Returns whether the button was pressed
static bool azaDrawContextMenuButton(azaRect bounds, const char *label) {
	bool result = false;
	if (azaMouseInRect(bounds)) {
		azaDrawRectGradientH(bounds, colorTooltipBGLeft, colorTooltipBGRight);
		if (azaMouseButtonPressed(MOUSE_BUTTON_LEFT, 1)) {
			result = true;
		}
	}
	if (label) {
		azaDrawText(label, bounds.x + textMargin, bounds.y + textMargin, 10, WHITE);
	}
	return result;
}

static void azaDrawContextMenu() {
	if (contextMenuKind == AZA_CONTEXT_MENU_NONE) return;
	if (IsKeyPressed(KEY_ESCAPE) || (azaMouseButtonPressed(MOUSE_BUTTON_LEFT, 1) && !azaMouseInRect(contextMenuRect))) {
		azaContextMenuClose();
		return;
	}
	azaRect choiceRect = contextMenuRect;
	choiceRect.h = contextMenuItemHeight;
	switch (contextMenuKind) {
		case AZA_CONTEXT_MENU_SEND_ADD: {
			uint32_t count = currentMixer->tracks.count; // +1 for Master, -1 for self
			if (count == 0) {
				azaContextMenuClose();
			}
			contextMenuRect.h = count * contextMenuItemHeight;
			azaRectFitOnScreen(&contextMenuRect);
			azaDrawRect(contextMenuRect, BLACK);
			PushScissor(contextMenuRect);
			azaTrack *target = &currentMixer->master;
			azaTrack *track = azaContextMenuTrackFromIndex();
			for (int32_t i = 0; i < (int32_t)currentMixer->tracks.count+1; target = currentMixer->tracks.data[i++]) {
				if (i == contextMenuTrackIndex) continue; // Skip self
				if (azaDrawContextMenuButton(choiceRect, target->name)) {
					azaContextMenuClose();
					azaTrackConnect(track, target, 0.0f, NULL, 0);
				}
				choiceRect.y += choiceRect.h;
			}
		} break;
		case AZA_CONTEXT_MENU_SEND_REMOVE: {
			azaTrack *track = azaContextMenuTrackFromIndex();
			uint32_t count = (azaTrackGetReceive(track, &currentMixer->master) != NULL);
			for (uint32_t i = 0; i < currentMixer->tracks.count; i++) {
				if (azaTrackGetReceive(track, currentMixer->tracks.data[i])) count++;
			}
			if (count == 0) {
				azaContextMenuClose();
			}
			contextMenuRect.h = count * contextMenuItemHeight;
			azaRectFitOnScreen(&contextMenuRect);
			azaDrawRect(contextMenuRect, BLACK);
			PushScissor(contextMenuRect);
			azaTrack *target = &currentMixer->master;
			for (int32_t i = 0; i < (int32_t)currentMixer->tracks.count+1; target = currentMixer->tracks.data[i++]) {
				if (azaTrackGetReceive(track, target) == NULL) continue;
				if (azaDrawContextMenuButton(choiceRect, target->name)) {
					azaContextMenuClose();
					azaTrackDisconnect(track, target);
				}
				choiceRect.y += choiceRect.h;
			}
		} break;
		case AZA_CONTEXT_MENU_TRACK_FX: {
			uint32_t count = 1 + (contextMenuTrackFXDSP != NULL);
			contextMenuRect.h = count * contextMenuItemHeight;
			azaRectFitOnScreen(&contextMenuRect);
			azaDrawRect(contextMenuRect, BLACK);
			PushScissor(contextMenuRect);
			if (contextMenuTrackFXDSP) {
				if (azaDrawContextMenuButton(choiceRect, TextFormat("Remove %s", azaGetDSPName(contextMenuTrackFXDSP)))) {
					azaTrackRemoveDSP(azaContextMenuTrackFromIndex(), contextMenuTrackFXDSP);
					if (azaDSPMetadataGetOwned(contextMenuTrackFXDSP->metadata)) {
						if (selectedDSP == contextMenuTrackFXDSP) {
							selectedDSP = NULL;
						}
						azaFreeDSP(contextMenuTrackFXDSP);
						contextMenuTrackFXDSP = NULL;
					}
					azaContextMenuClose();
				}
				choiceRect.y += choiceRect.h;
			}
			if (azaDrawContextMenuButton(choiceRect, "Add Plugin")) {
				azaContextMenuOpen(AZA_CONTEXT_MENU_TRACK_FX_ADD);
			}
		} break;
		case AZA_CONTEXT_MENU_TRACK_FX_ADD: {
			azaTrack *track = azaContextMenuTrackFromIndex();
			uint32_t count = 0;
			for (uint32_t i = 0; i < azaDSPRegistry.count; i++) {
				count += (uint32_t)(azaDSPRegistry.data[i].fp_makeDSP != NULL);
			}
			contextMenuRect.h = count * contextMenuItemHeight;
			azaRectFitOnScreen(&contextMenuRect);
			azaDrawRect(contextMenuRect, BLACK);
			PushScissor(contextMenuRect);
			for (uint32_t i = 0; i < azaDSPRegistry.count; i++) {
				if (azaDSPRegistry.data[i].fp_makeDSP == NULL) continue;
				if (azaDrawContextMenuButton(choiceRect, azaDSPRegistry.data[i].name)) {
					azaDSP *newDSP = azaDSPRegistry.data[i].fp_makeDSP(track->buffer.channelLayout.count);
					azaTrackInsertDSP(track, newDSP, contextMenuTrackFXDSP);
					azaContextMenuClose();
				}
				choiceRect.y += choiceRect.h;
			}
		} break;
		default: { // Simple non-dynamic
			azaDrawRect(contextMenuRect, BLACK);
			PushScissor(contextMenuRect);
			uint32_t count = contextMenuActionCounts[(uint32_t)contextMenuKind];
			const char **labels = contextMenuLabels[(uint32_t)contextMenuKind];
			const fp_ContextMenuAction *actions = contextMenuActions[(uint32_t)contextMenuKind];
			for (uint32_t i = 0; i < count; i++) {
				if (azaDrawContextMenuButton(choiceRect, labels[i])) {
					azaContextMenuClose();
					actions[i]();
				}
				choiceRect.y += choiceRect.h;
			}
		} break;
	}
	azaDrawRectLines(contextMenuRect, WHITE);
	PopScissor();
}




// Tracks




static void azaDrawTrackFX(azaTrack *track, uint32_t metadataIndex, azaRect bounds) {
	azaDrawRectGradientV(bounds, colorTrackFXTop, colorTrackFXBot);
	azaRectShrinkMargin(&bounds, margin);
	PushScissor(bounds);
	azaTrackGUIMetadata *metadata = &azaTrackGUIMetadatas.data[metadataIndex];
	azaRect pluginRect = bounds;
	pluginRect.y += metadata->scrollFXY;
	pluginRect.h = 10 + margin * 2;
	azaRect muteRect = pluginRect;
	azaRectShrinkRight(&pluginRect, pluginRect.h + margin);
	azaRectShrinkLeft(&muteRect, pluginRect.w + margin);
	azaDSP *mouseoverDSP = NULL;
	azaDSP *dsp = track->dsp;
	while (dsp) {
		bool mouseover = azaMouseInRect(pluginRect);
		if (mouseover) {
			azaDrawRectGradientV(pluginRect, colorPluginBorderSelected, colorPluginBorder);
			if (azaMouseButtonPressed(MOUSE_BUTTON_LEFT, 0)) {
				selectedDSP = dsp;
			}
			mouseoverDSP = dsp;
		} else if (azaMouseInRect(muteRect)) {
			azaTooltipAdd("Bypass", muteRect.x + muteRect.w, muteRect.y, false);
			if (azaMouseButtonPressed(MOUSE_BUTTON_LEFT, 0)) {
				azaDSPMetadataToggleBypass(&dsp->metadata);
			}
		}
		azaDrawRectLines(pluginRect, dsp == selectedDSP ? colorPluginBorderSelected : colorPluginBorder);
		azaDrawText(azaGetDSPName(dsp), pluginRect.x + margin, pluginRect.y + margin, 10, WHITE);
		if (azaDSPMetadataGetBypass(dsp->metadata)) {
			azaDrawRect(muteRect, colorFaderMuteButton);
		} else {
			azaDrawRectLines(muteRect, colorFaderMuteButton);
		}
		dsp = dsp->pNext;
		pluginRect.y += pluginRect.h + margin;
		muteRect.y += pluginRect.h + margin;
	}
	if (azaMouseInRect(bounds)) {
		bool trackFXCanScrollDown = (pluginRect.y + pluginRect.h > bounds.y + bounds.h);
		int scroll = (int)GetMouseWheelMoveV().y * 8;
		if (!trackFXCanScrollDown && scroll < 0) scroll = 0;
		metadata->scrollFXY += scroll;
		if (metadata->scrollFXY > 0) metadata->scrollFXY = 0;
		trackFXCanScrollDown = false;
	}
	PopScissor();
	if (azaMousePressedInRect(MOUSE_BUTTON_RIGHT, 1, bounds)) {
		azaContextMenuSetIndexFromTrack(track);
		contextMenuTrackFXDSP = mouseoverDSP;
		azaContextMenuOpen(AZA_CONTEXT_MENU_TRACK_FX);
	}
}

static void azaDrawTrackControls(azaTrack *track, uint32_t metadataIndex, azaRect bounds) {
	if (azaMousePressedInRect(MOUSE_BUTTON_RIGHT, 1, bounds)) {
		azaContextMenuSetIndexFromTrack(track);
		azaContextMenuOpen(AZA_CONTEXT_MENU_TRACK);
	}
	azaDrawRectGradientV(bounds, colorTrackControlsTop, colorTrackControlsBot);
	azaRectShrinkMargin(&bounds, margin);
	// Fader
	azaDrawFader(bounds, &track->gain, &track->mute, "Track Gain", meterDBRange, faderDBHeadroom);
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
		int faderWidth = azaDrawFader(bounds, &receive->gain, &receive->mute, "Master Send", meterDBRange, faderDBHeadroom);
		azaRectShrinkLeft(&bounds, faderWidth + margin);
	}
	for (uint32_t i = 0; i < currentMixer->tracks.count; i++) {
		receive = azaTrackGetReceive(track, currentMixer->tracks.data[i]);
		if (receive) {
			int faderWidth = azaDrawFader(bounds, &receive->gain, &receive->mute, TextFormat("%s Send", currentMixer->tracks.data[i]->name), meterDBRange, faderDBHeadroom);
			azaRectShrinkLeft(&bounds, faderWidth + margin);
		}
	}
	PopScissor();
}

static void azaDrawTrack(azaTrack *track, uint32_t metadataIndex, azaRect bounds) {
	azaRectShrinkMargin(&bounds, margin);
	PushScissor(bounds);
	azaRect nameRect = {
		bounds.x,
		bounds.y,
		bounds.w,
		textMargin * 2 + 10,
	};
	azaDrawTextBox(nameRect, track->name, sizeof(track->name));
	azaRectShrinkTop(&bounds, trackLabelDrawHeight);
	azaDrawTrackFX(track, metadataIndex, (azaRect) { bounds.x, bounds.y, bounds.w, trackFXDrawHeight });
	azaRectShrinkTop(&bounds, trackFXDrawHeight + margin*2);
	azaDrawTrackControls(track, metadataIndex, bounds);
	PopScissor();
}




// Mixer




static void azaDrawMixer() {
	// TODO: This granularity might get in the way of audio processing. Probably only lock the mutex when it matters most.
	azaMutexLock(&currentMixer->mutex);
	azaRect trackRect = {
		scrollTracksX + margin,
		pluginDrawHeight,
		trackDrawWidth,
		trackDrawHeight - margin,
	};
	AZA_DA_RESERVE_COUNT(azaTrackGUIMetadatas, currentMixer->tracks.count+1, do{}while(0));
	azaTrackGUIMetadatas.count = currentMixer->tracks.count+1;
	azaDrawTrack(&currentMixer->master, 0, trackRect);
	for (uint32_t i = 0; i < currentMixer->tracks.count; i++) {
		trackRect.x += trackDrawWidth;
		azaDrawTrack(currentMixer->tracks.data[i], i+1, trackRect);
	}
	int screenWidth = GetLogicalWidth();
	azaRect scrollbarRect = {
		0,
		pluginDrawHeight + trackDrawHeight,
		screenWidth,
		scrollbarSize,
	};
	int scrollableWidth = screenWidth - (trackDrawWidth * (currentMixer->tracks.count+1) + margin*2);
	azaDrawScrollbarHorizontal(scrollbarRect, &scrollTracksX, AZA_MIN(scrollableWidth, 0), 0, -trackDrawWidth / 3, "Mixer Scrollbar :)");
	azaTooltipAdd(TextFormat("CPU: %.2f%%", currentMixer->cpuPercentSlow), screenWidth, 0, false);
	if (currentMixer->hasCircularRouting) {
		azaTooltipAdd("Circular Routing Detected!!!", 0, pluginDrawHeight - (textMargin*2 + 10), true);
	}
	azaMutexUnlock(&currentMixer->mutex);
}




// Controls for selected DSP




static void azaDrawLookaheadLimiter(azaLookaheadLimiter *data, azaRect bounds) {
	int faderWidth = azaDrawFader(bounds, &data->config.gainInput, NULL, "Input Gain", lookaheadLimiterMeterDBRange, faderDBHeadroom);
	azaRectShrinkLeft(&bounds, faderWidth + margin);
	int metersWidth = azaDrawMeters(&data->metersInput, bounds, lookaheadLimiterMeterDBRange);
	azaRectShrinkLeft(&bounds, metersWidth + margin);

	azaRect attenuationRect = {
		bounds.x,
		bounds.y,
		meterDrawWidth*2 + margin*3,
		bounds.h,
	};
	azaRectShrinkLeft(&bounds, attenuationRect.w + margin);
	bool attenuationMouseover = azaMouseInRect(attenuationRect);
	if (attenuationMouseover) {
		azaTooltipAdd("Attenuation", attenuationRect.x, attenuationRect.y - (textMargin*2 + 10), false);
	}
	azaDrawMeterBackground(attenuationRect, lookaheadLimiterAttenuationMeterDBRange, 0);
	azaRectShrinkMargin(&attenuationRect, margin);
	int yOffset;
	yOffset = azaDBToYOffsetClamped(-aza_amp_to_dbf(data->minAmpShort), attenuationRect.h, 0, lookaheadLimiterAttenuationMeterDBRange);
	azaDrawRect((azaRect) {
		attenuationRect.x,
		attenuationRect.y,
		attenuationRect.w,
		yOffset
	}, colorLookaheadLimiterAttenuation);
	float attenuationPeakDB = aza_amp_to_dbf(data->minAmp);
	yOffset = azaDBToYOffsetClamped(-attenuationPeakDB, attenuationRect.h, 0, lookaheadLimiterAttenuationMeterDBRange);
	if (attenuationMouseover) {
		azaTooltipAdd(TextFormat("%+.1fdb", attenuationPeakDB), attenuationRect.x + attenuationRect.w + margin, attenuationRect.y + yOffset - (textMargin + 5), false);
	}
	azaDrawLine(attenuationRect.x, attenuationRect.y + yOffset, attenuationRect.x + attenuationRect.w, attenuationRect.y + yOffset, colorLookaheadLimiterAttenuation);
	if (azaMousePressedInRect(MOUSE_BUTTON_LEFT, 0, attenuationRect)) {
		data->minAmp = 1.0f;
	}
	data->minAmpShort = 1.0f;

	azaDrawFader(bounds, &data->config.gainOutput, NULL, "Output Gain", lookaheadLimiterMeterDBRange, faderDBHeadroom);
	azaRectShrinkLeft(&bounds, faderWidth + margin);
	azaDrawMeters(&data->metersOutput, bounds, lookaheadLimiterMeterDBRange);
	azaRectShrinkLeft(&bounds, metersWidth + margin);
}

static void azaDrawFilter(azaFilter *data, azaRect bounds) {
	azaRect kindRect = bounds;
	kindRect.w = 80;
	kindRect.h = textMargin*2 + 10;
	for (int i = 0; i < AZA_FILTER_KIND_COUNT; i++) {
		if (azaMousePressedInRect(MOUSE_BUTTON_LEFT, 0, kindRect)) {
			data->config.kind = (azaFilterKind)i;
		}
		bool selected = ((int)data->config.kind == i);
		azaDrawRect(kindRect, colorMeterBGBot);
		if (selected) {
			azaDrawRectLines(kindRect, colorPluginBorderSelected);
		}
		azaDrawText(azaFilterKindString[i], kindRect.x + textMargin, kindRect.y + textMargin, 10, WHITE);
		kindRect.y += kindRect.h + margin;
	}
	azaRectShrinkTop(&kindRect, (kindRect.h + margin) * AZA_FILTER_KIND_COUNT);
	azaRectShrinkLeft(&bounds, kindRect.w + margin);
	int usedWidth = azaDrawSliderFloatLog(bounds, &data->config.frequency, 5.0f, 24000.0f, 0.1f, 500.0f, "Cutoff Frequency", "%.1fHz");
	azaRectShrinkLeft(&bounds, usedWidth + margin);
	usedWidth = azaDrawSliderFloat(bounds, &data->config.dryMix, 0.0f, 1.0f, 0.1f, 0.0f, "Dry Mix", "%.2f");
	azaRectShrinkLeft(&bounds, usedWidth + margin);
}

static void azaDrawCompressor(azaCompressor *data, azaRect bounds) {
	int usedWidth = azaDrawMeters(&data->metersInput, bounds, compressorMeterDBRange);
	azaRectShrinkLeft(&bounds, usedWidth + margin);

	usedWidth = azaDrawFader(bounds, &data->config.threshold, NULL, "Threshold", 72, 0);
	azaRectShrinkLeft(&bounds, usedWidth + margin);
	usedWidth = azaDrawSliderFloat(bounds, &data->config.ratio, 1.0f, 10.0f, 0.2f, 10.0f, "Ratio", "%.2f");
	azaRectShrinkLeft(&bounds, usedWidth + margin);
	usedWidth = azaDrawSliderFloatLog(bounds, &data->config.attack, 1.0f, 1000.0f, 0.2f, 50.0f, "Attack", "%.1fms");
	azaRectShrinkLeft(&bounds, usedWidth + margin);
	usedWidth = azaDrawSliderFloatLog(bounds, &data->config.decay, 1.0f, 1000.0f, 0.2f, 200.0f, "Release", "%.1fms");
	azaRectShrinkLeft(&bounds, usedWidth + margin);

	azaRect attenuationRect = {
		bounds.x,
		bounds.y,
		meterDrawWidth*2 + margin*3,
		bounds.h,
	};
	azaRectShrinkLeft(&bounds, attenuationRect.w + margin);
	bool attenuationMouseover = azaMouseInRect(attenuationRect);
	if (attenuationMouseover) {
		azaTooltipAdd("Attenuation", attenuationRect.x, attenuationRect.y - (textMargin*2 + 10), false);
	}
	azaDrawMeterBackground(attenuationRect, compressorAttenuationMeterDBRange, 0);
	azaRectShrinkMargin(&attenuationRect, margin);
	int yOffset;
	yOffset = azaDBToYOffsetClamped(-data->minGainShort, attenuationRect.h, 0, compressorAttenuationMeterDBRange);
	azaDrawRect((azaRect) {
		attenuationRect.x,
		attenuationRect.y,
		attenuationRect.w,
		yOffset
	}, colorCompressorAttenuation);
	yOffset = azaDBToYOffsetClamped(-data->minGain, attenuationRect.h, 0, compressorAttenuationMeterDBRange);
	if (attenuationMouseover) {
		azaTooltipAdd(TextFormat("%+.1fdb", data->minGain), attenuationRect.x + attenuationRect.w + margin, attenuationRect.y + yOffset - (textMargin + 5), false);
	}
	azaDrawLine(attenuationRect.x, attenuationRect.y + yOffset, attenuationRect.x + attenuationRect.w, attenuationRect.y + yOffset, colorCompressorAttenuation);
	if (azaMousePressedInRect(MOUSE_BUTTON_LEFT, 0, attenuationRect)) {
		data->minGain = 1.0f;
	}

	usedWidth = azaDrawFader(bounds, &data->config.gain, NULL, "Output Gain", 72, 36);
	azaRectShrinkLeft(&bounds, usedWidth + margin);

	usedWidth = azaDrawMeters(&data->metersOutput, bounds, compressorMeterDBRange);
	// azaRectShrinkLeft(&bounds, usedWidth + margin);
}

static void azaDrawDelay(azaDelay *data, azaRect bounds) {
	int usedWidth = azaDrawMeters(&data->metersInput, bounds, meterDBRange);
	azaRectShrinkLeft(&bounds, usedWidth + margin);

	usedWidth = azaDrawFader(bounds, &data->config.gain, &data->config.muteWet, "Wet Gain", meterDBRange, faderDBHeadroom);
	azaRectShrinkLeft(&bounds, usedWidth + margin);
	usedWidth = azaDrawFader(bounds, &data->config.gainDry, &data->config.muteDry, "Dry Gain", meterDBRange, faderDBHeadroom);
	azaRectShrinkLeft(&bounds, usedWidth + margin);

	usedWidth = azaDrawSliderFloatLog(bounds, &data->config.delay, 0.1f, 10000.0f, 0.1f, 300.0f, "Delay", "%.1fms");
	azaRectShrinkLeft(&bounds, usedWidth + margin);

	usedWidth = azaDrawSliderFloat(bounds, &data->config.feedback, 0.0f, 1.0f, 0.02f, 0.5f, "Feedback", "%.3f");
	azaRectShrinkLeft(&bounds, usedWidth + margin);

	usedWidth = azaDrawSliderFloat(bounds, &data->config.pingpong, 0.0f, 1.0f, 0.02f, 0.0f, "PingPong", "%.3f");
	azaRectShrinkLeft(&bounds, usedWidth + margin);

	for (uint32_t c = 0; c < (uint32_t)(data->channelData.capInline + data->channelData.capAdditional); c++) {
		azaDelayChannelConfig *channel = azaDelayGetChannelConfig(data, c);
		usedWidth = azaDrawSliderFloatLog(bounds, &channel->delay, 0.1f, 10000.0f, 0.1f, 0.0f, TextFormat("Ch %d Delay", (int)c), "%.1fms");
		azaRectShrinkLeft(&bounds, usedWidth + margin);
	}

	// TODO: wet effects list (I guess with some kinda navigation?)

	usedWidth = azaDrawMeters(&data->metersOutput, bounds, meterDBRange);
	azaRectShrinkLeft(&bounds, usedWidth + margin);
}



static const Color colorMonitorSpectrumBGTop       = {  20,  30,  40, 255 };
static const Color colorMonitorSpectrumBGBot       = {  10,  15,  20, 255 };
static const Color colorMonitorSpectrumFG          = { 100, 150, 200, 255 };
static const Color colorMonitorSpectrumDBTick      = {  50,  70, 100,  96 };
static const Color colorMonitorSpectrumDBTickUnity = { 100,  70,  50, 128 };

static const Color colorMonitorSpectrumWindowControl = { 10, 15, 20, 255 };
static const Color colorMonitorSpectrumWindowControlHighlight = { 50, 70, 100, 255 };
static const int monitorSpectrumWindowControlWidth = 35;
static const int monitorSpectrumMaxWindow = 8192;
static const int monitorSpectrumMinWindow = 64;
static const int monitorSpectrumMaxSmoothing = 63;
static const int monitorSpectrumMinSmoothing = 0;

static void azaDrawReverb(azaReverb *data, azaRect bounds) {
	int usedWidth = azaDrawFader(bounds, &data->config.gain, &data->config.muteWet, "Wet Gain", 36, 6);
	azaRectShrinkLeft(&bounds, usedWidth + margin);
	usedWidth = azaDrawFader(bounds, &data->config.gainDry, &data->config.muteDry, "Dry Gain", 36, 6);
	azaRectShrinkLeft(&bounds, usedWidth + margin);
	usedWidth = azaDrawSliderFloat(bounds, &data->config.roomsize, 1.0f, 100.0f, 1.0f, 10.0f, "Room Size", "%.0f");
	azaRectShrinkLeft(&bounds, usedWidth + margin);
	usedWidth = azaDrawSliderFloat(bounds, &data->config.color, 1.0f, 5.0f, 0.25f, 2.0f, "Color", "%.2f");
	azaRectShrinkLeft(&bounds, usedWidth + margin);
	usedWidth = azaDrawSliderFloat(bounds, &data->config.delay, 0.0f, 500.0f, 1.0f, 10.0f, "Early Delay", "%.1fms");
}

static int azaMonitorSpectrumBarXFromIndex(azaMonitorSpectrum *data, uint32_t width, uint32_t i) {
	// float nyquist = (float)data->samplerate / 2.0f;
	// float baseFreq = (float)data->samplerate / (float)data->config.window;
	uint32_t window = (data->config.window >> 1)+2;
	float baseLog = log2f((float)window);
	if (i) {
		return (int)roundf((float)width * (log2f((float)(i+1) / (float)window) + baseLog) / baseLog);
	} else {
		return 0;
	}
}

static void azaDrawMonitorSpectrum(azaMonitorSpectrum *data, azaRect bounds) {
	azaRect controlRect = bounds;
	controlRect.x += margin;
	controlRect.y += margin;
	controlRect.w = monitorSpectrumWindowControlWidth;
	controlRect.h = textMargin*2 + 10;
	int vMove = (int)GetMouseWheelMoveV().y;

	// Mode

	Color colorWindowControlRect = colorMonitorSpectrumWindowControl;
	bool hover = azaMouseInRect(controlRect);
	if (hover) {
		colorWindowControlRect = colorMonitorSpectrumWindowControlHighlight;
		if (azaMouseButtonPressed(MOUSE_BUTTON_LEFT, 0)) {
			data->config.mode++;
			if (data->config.mode >= AZA_MONITOR_SPECTRUM_MODE_COUNT) {
				data->config.mode = 0;
			}
		}
		if (data->config.mode == AZA_MONITOR_SPECTRUM_MODE_ONE_CHANNEL) {
			if (vMove > 0) {
				if (data->config.channelChosen < data->inputBufferChannelCount-1) {
					data->config.channelChosen++;
				} else {
					data->config.channelChosen = 0;
				}
			} else if (vMove < 0) {
				if (data->config.channelChosen > 0) {
					data->config.channelChosen--;
				} else {
					data->config.channelChosen = data->inputBufferChannelCount-1;
				}
			}
		}
	}
	azaDrawRect(controlRect, colorWindowControlRect);
	switch (data->config.mode) {
		case AZA_MONITOR_SPECTRUM_MODE_ONE_CHANNEL: {
			if (hover) {
				azaTooltipAdd("Single-Channel Mode", controlRect.x + controlRect.w, controlRect.y, false);
			}
			azaDrawText(TextFormat("Ch %d", (int)data->config.channelChosen), controlRect.x + textMargin, controlRect.y + textMargin, 10, WHITE);
		} break;
		case AZA_MONITOR_SPECTRUM_MODE_AVG_CHANNELS: {
			if (hover) {
				azaTooltipAdd("Channel-Average Mode", controlRect.x + controlRect.w, controlRect.y, false);
			}
			azaDrawText("Avg", controlRect.x + textMargin, controlRect.y + textMargin, 10, WHITE);
		} break;
		case AZA_MONITOR_SPECTRUM_MODE_COUNT: break;
	}

	// FFT Window

	controlRect.y += controlRect.h + margin*2;
	colorWindowControlRect = colorMonitorSpectrumWindowControl;
	hover = azaMouseInRect(controlRect);
	if (hover) {
		colorWindowControlRect = colorMonitorSpectrumWindowControlHighlight;
		if (azaMouseButtonPressed(MOUSE_BUTTON_LEFT, 0) || vMove > 0) {
			if (data->config.window < monitorSpectrumMaxWindow) {
				data->config.window <<= 1;
			}
		}
		if (azaMouseButtonPressed(MOUSE_BUTTON_RIGHT, 0) || vMove < 0) {
			if (data->config.window > monitorSpectrumMinWindow) {
				data->config.window >>= 1;
			}
		}
		float ups = (float)data->samplerate / (float)data->config.window;
		azaTooltipAdd(TextFormat("FFT Window (%d updates/s)", (int)roundf(ups)), controlRect.x + controlRect.w, controlRect.y, false);
	}
	azaDrawRect(controlRect, colorWindowControlRect);
	azaDrawText(TextFormat("%d", data->config.window), controlRect.x + textMargin, controlRect.y + textMargin, 10, WHITE);

	// Smoothing

	controlRect.y += controlRect.h + margin*2;
	colorWindowControlRect = colorMonitorSpectrumWindowControl;
	hover = azaMouseInRect(controlRect);
	if (hover) {
		colorWindowControlRect = colorMonitorSpectrumWindowControlHighlight;
		if (azaMouseButtonPressed(MOUSE_BUTTON_LEFT, 0) || vMove > 0) {
			if (data->config.smoothing < monitorSpectrumMaxSmoothing) {
				data->config.smoothing += 1;
			}
		}
		if (azaMouseButtonPressed(MOUSE_BUTTON_RIGHT, 0) || vMove < 0) {
			if (data->config.smoothing > monitorSpectrumMinSmoothing) {
				data->config.smoothing -= 1;
			}
		}
		azaTooltipAdd("Smoothing", controlRect.x + controlRect.w, controlRect.y, false);
	}
	azaDrawRect(controlRect, colorWindowControlRect);
	azaDrawText(TextFormat("%d", data->config.smoothing), controlRect.x + textMargin, controlRect.y + textMargin, 10, WHITE);

	azaRect spectrumRect = bounds;
	azaRectShrinkLeft(&spectrumRect, monitorSpectrumWindowControlWidth + margin*2);
	azaRectShrinkMargin(&spectrumRect, margin);
	azaRectShrinkBottom(&spectrumRect, textMargin*2 + 10);
	float baseFreq = (float)data->samplerate / (float)data->config.window;
	azaDrawRectGradientV(spectrumRect, colorMonitorSpectrumBGTop, colorMonitorSpectrumBGBot);
	if (!data->outputBuffer) return;
	azaRect bar;
	uint32_t window = data->config.window >> 1;
	float lastFreq = 1.0f;
	uint32_t lastX = 0, lastWidth = 0;
	for (uint32_t i = 0; i <= window; i++) {
		float magnitude = data->outputBuffer[i];
		// float phase = data->outputBuffer[i + data->config.window] / AZA_TAU + 0.5f;
		float magDB = aza_amp_to_dbf(magnitude);
		int yOffset = azaDBToYOffsetClamped(12.0f-magDB, spectrumRect.h, 0, 96+12);
		bar.x = azaMonitorSpectrumBarXFromIndex(data, spectrumRect.w-1, i);
		int right = azaMonitorSpectrumBarXFromIndex(data, spectrumRect.w-1, i+1);
		bar.w = AZA_MAX(right - bar.x, 1);
		bar.y = yOffset;
		bar.h = spectrumRect.h - bar.y;
		bar.x += spectrumRect.x;
		bar.y += spectrumRect.y;
		azaDrawRect(bar, colorMonitorSpectrumFG);
		// This is atrocious to look at
		// azaDrawRect(bar, ColorHSV(phase, 0.5f, 0.8f, 255));
		float freq = baseFreq * i;
		if (freq / lastFreq >= 2.0f) {
			azaDrawLine(bar.x, spectrumRect.y, bar.x, spectrumRect.y + spectrumRect.h + textMargin, (Color) {0,0,0,128});
			if (bar.x - lastX >= lastWidth+10) {
				int intFreq = (int)roundf(freq);
				const char *str;
				if (intFreq % 1000 == 0) {
					str = TextFormat("%dk", intFreq/1000);
				} else {
					str = TextFormat("%d", intFreq);
				}
				int width = MeasureText(str, 10);
				int x = bar.x-width/2;
				x = AZA_MAX(spectrumRect.x, x);
				x = AZA_MIN(x, spectrumRect.x + spectrumRect.w - width);
				azaDrawText(str, x, spectrumRect.y + spectrumRect.h + margin + textMargin, 10, WHITE);
				lastWidth = width;
				lastX = x;
			}
			lastFreq = freq;
		}
	}
	azaDrawDBTicks(spectrumRect, 96+12, 12, colorMonitorSpectrumDBTick, colorMonitorSpectrumDBTickUnity);
}

static void azaDrawSelectedDSP() {
	pluginDrawHeight = GetLogicalHeight() - (trackDrawHeight + scrollbarSize);
	azaRect bounds = {
		margin*2,
		margin*2,
		GetLogicalWidth() - margin*4,
		pluginDrawHeight - margin*4,
	};
	azaDrawRectGradientV(bounds, colorPluginSettingsTop, colorPluginSettingsBot);
	azaDrawRectLines(bounds, selectedDSP ? colorPluginBorderSelected : colorPluginBorder);
	if (!selectedDSP) return;

	azaRectShrinkMargin(&bounds, margin*2);
	azaDrawText(azaGetDSPName(selectedDSP), bounds.x + textMargin, bounds.y + textMargin, 20, WHITE);
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
			azaDrawFilter((azaFilter*)selectedDSP, bounds);
			break;
		case AZA_DSP_COMPRESSOR:
			azaDrawCompressor((azaCompressor*)selectedDSP, bounds);
			break;
		case AZA_DSP_DELAY:
			azaDrawDelay((azaDelay*)selectedDSP, bounds);
			break;
		case AZA_DSP_REVERB:
			azaDrawReverb((azaReverb*)selectedDSP, bounds);
			break;
		case AZA_DSP_SAMPLER:
		case AZA_DSP_GATE:
		case AZA_DSP_DELAY_DYNAMIC:
		case AZA_DSP_SPATIALIZE:
			break;
		case AZA_DSP_MONITOR_SPECTRUM:
			azaDrawMonitorSpectrum((azaMonitorSpectrum*)selectedDSP, bounds);
			break;
		case AZA_DSP_KIND_COUNT: break;
	}
}




// API




static AZA_THREAD_PROC_DEF(azaMixerGUIThreadProc, userdata) {
	unsigned int configFlags = FLAG_VSYNC_HINT | FLAG_WINDOW_RESIZABLE;
	if (isWindowTopmost) {
		configFlags |= FLAG_WINDOW_TOPMOST;
	}
	SetConfigFlags(configFlags);
	SetTraceLogLevel(LOG_ERROR);
	SetTraceLogCallback(azaRaylibTraceLogCallback);

	InitWindow(
		AZA_MIN(trackDrawWidth * (1 + currentMixer->tracks.count) + margin*2, 640),
		pluginDrawHeight + trackDrawHeight + scrollbarSize,
		"AzAudio Mixer"
	);
	isWindowOpen = true;
	SetExitKey(-1); // Don't let raylib use the ESC key to close

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
		azaHandleDPIChanges();
		azaMouseCaptureStartFrame();
		BeginDrawing();
			ClearBackground(colorBG);
			azaDrawMixer();
			azaDrawSelectedDSP();
			azaDrawTextboxBeingEdited();
			azaDrawContextMenu();
			azaDrawTooltips();
			if (azaMouseButtonPressed(MOUSE_BUTTON_LEFT, 100)) {
				lastClickTime = azaGetTimestamp();
			}
		EndDrawing();
		azaMouseCaptureEndFrame();
		mousePrev = azaMousePosition();
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