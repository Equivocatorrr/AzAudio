/*
	File: mixer_gui.c
	Author: Philip Haynes
	Implementation of a GUI for interacting with an azaMixer on-the-fly, all at the convenience of a single function call!
*/

#include <raylib.h>

#include "backend/threads.h"
#include "backend/timer.h"
#include "AzAudio.h"
#include "math.h"
#include "dsp.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "AzAudio.h"

#include "mixer.h"

#if defined(__GNUC__)
// Suppress some unused function warnings
#pragma GCC diagnostic ignored "-Wunused-function"
#endif

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
	int scrollSendsX;
	int width;
} azaTrackGUIMetadata;

static struct {
	azaTrackGUIMetadata *data;
	uint32_t count;
	uint32_t capacity;
} azaTrackGUIMetadatas = {0};




// Constants for size, color, etc.




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

static bool azaPointInRect(azaRect rect, azaPoint point) {
	return (point.x >= rect.x && point.y >= rect.y && point.x <= rect.x+rect.w && point.y <= rect.y+rect.h);
}

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

static azaRect GetCurrentScissor() {
	if (scissorStack.count > 0) {
		return scissorStack.scissors[scissorStack.count-1];
	} else {
		return (azaRect) {
			.x = 0, .y = 0,
			.w = GetLogicalWidth(),
			.h = GetLogicalHeight(),
		};
	}
}




// Mouse input




// Used for UI input culling (for context menus and such)
static int mouseDepth = 0;
static azaPoint mousePrev = {0};

static azaPoint azaMousePosition() {
	Vector2 mouse = GetMousePosition();
	return (azaPoint) { (int)mouse.x / currentDPIScale, (int)mouse.y / currentDPIScale };
}

static bool azaMouseInScissor() {
	if (scissorStack.count > 0) {
		return azaPointInRect(scissorStack.scissors[scissorStack.count-1], azaMousePosition());
	} else {
		return true;
	}
}

static bool azaMouseInRect(azaRect rect) {
	return azaMouseInScissor() && azaPointInRect(rect, azaMousePosition());
}

static bool azaMouseInRectDepth(azaRect rect, int depth) {
	return depth >= mouseDepth && azaMouseInRect(rect);
}

static bool azaMousePressed(int button, int depth) {
	return (depth >= mouseDepth && azaMouseInScissor() && IsMouseButtonPressed(button));
}

static bool azaMouseDown(int button, int depth) {
	return (depth >= mouseDepth && azaMouseInScissor() && IsMouseButtonDown(button));
}

static bool azaMousePressedInRect(int button, int depth, azaRect rect) {
	return azaMousePressed(button, depth) && azaPointInRect(rect, azaMousePosition());
}

static bool azaDidDoubleClick(int depth) {
	if (azaMousePressed(MOUSE_BUTTON_LEFT, depth)) {
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
// Resets the mouse drag origin to the current mouse position. Useful for relative mouse dragging, allowing you to let a big enough delta to accumulate to make a meaningful change before actually consuming it.
static void azaMouseCaptureResetDelta() {
	mouseDragStart = azaMousePosition();
}

// Pass in a unique pointer identifier for continuity. A const char* can work just fine.
// returns true if we're capturing the mouse
// outputs the deltas from the mouseDragOrigin (if we're capturing, else zeroes them)
// If this delta is consumed, call azaMouseCaptureResetDelta(), otherwise the delta will always be relative to the start position.
static bool azaCaptureMouseDelta(azaRect bounds, azaPoint *out_delta, void *id) {
	assert(out_delta);
	assert(id);
	azaPoint mouse = azaMousePosition();
	*out_delta = (azaPoint) {0};

	if (mouseDragID == NULL) {
		if (azaMousePressedInRect(MOUSE_BUTTON_LEFT, 0, bounds)) {
			azaMouseCaptureStart(id);
			return true;
		}
	} else if (mouseDragID == id) {
		// Don't check depth or scissors, because drags persist outside of their starting bounds
		if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
			azaMouseCaptureEnd();
			return false;
		}
		mouseDragTimestamp = azaGetTimestamp();
		*out_delta = azaPointSub(mouse, mouseDragStart);
		return true;
	}
	return false;
}

// Meant for use when azaCaptureMouse___ returns true to know when a drag was just initiated.
static bool azaMouseCaptureJustStarted() {
	return azaMousePressed(MOUSE_BUTTON_LEFT, 2);
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




static float azaSnapFloat(float value, float interval) {
	assert(interval > 0.0f);
	return roundf(value / interval) * interval;
}

static int azaSnapInt(int value, int interval) {
	assert(interval > 0);
	if (value >= 0) {
		return ((value + interval/2) / interval) * interval;
	} else {
		return ((value - interval/2) / interval) * interval;
	}
}

// knobRect is the region on the screen that can be grabbed
// value is the target value to be changed by dragging
// inverted changes how the drag coordinates change value. When true, positive mouse movements (down, or right) result in negative changes in value.
// dragRegion is how many logical pixels we have to drag (used to scale pixels to the range determined by valueMin and valueMax)
// when vertical is true, we use the y component of the mouse drag
// valueMin determines the lowest value representable in our drag region
// valueMax determines the highest value representable in our drag region
// when doClamp is true, the output value is clamped between valueMin and valueMax
// preciseDiv is a scaling factor used during precise dragging (actual delta = delta * preciseDiv)
// when doPrecise is true, holding either shift key enables precise dragging
// snapInterval is the exact interval we snap to when snapping (modified by precise dragging)
// when doSnap is true, holding either control key enables snapping
// returns true if we're dragging, meaning the value may have updated
static bool azaMouseDragFloat(azaRect knobRect, float *value, bool inverted, int dragRegion, bool vertical, float valueMin, float valueMax, bool doClamp, float preciseDiv, bool doPrecise, float snapInterval, bool doSnap) {
	assert(valueMax > valueMin);
	azaPoint mouseDelta = {0};
	// We assume our value pointer is unique, so it works as an implicit id. Even if we had 2 knobs for the same value, this would probably still be well-behaved.
	if (azaCaptureMouseDelta(knobRect, &mouseDelta, value)) {
		static float dragStartValue = 0.0f;
		if (azaMouseCaptureJustStarted()) {
			dragStartValue = *value;
		}
		bool precise = doPrecise && (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT));
		bool snap = doSnap && (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL));
		if (doSnap && (IsKeyPressed(KEY_LEFT_SHIFT) || IsKeyPressed(KEY_RIGHT_SHIFT) || IsKeyReleased(KEY_LEFT_SHIFT) || IsKeyReleased(KEY_RIGHT_SHIFT))) {
			// Transition between precise and not precise, reset the offsets and values so the knob doesn't jump.
			dragStartValue = *value;
			azaMouseCaptureResetDelta();
			mouseDelta = (azaPoint) {0};
		}
		int usefulDelta = vertical ? mouseDelta.y : mouseDelta.x;
		float valueRange = valueMax - valueMin;
		float actualDelta = (float)usefulDelta * valueRange / (float)dragRegion;
		if (inverted) {
			actualDelta = -actualDelta;
		}
		if (precise) {
			actualDelta /= preciseDiv;
			snapInterval /= preciseDiv;
		}
		*value = dragStartValue + actualDelta;
		if (snap) {
			*value = azaSnapFloat(*value, snapInterval);
		}
		if (doClamp) {
			*value = azaClampf(*value, valueMin, valueMax);
		}
		return true;
	}
	return false;
}

// knobRect is the region on the screen that can be grabbed
// value is the target value to be changed by dragging
// inverted changes how the drag coordinates change value. When true, positive mouse movements (down, or right) result in negative changes in value.
// dragRegion is how many logical pixels we have to drag (used to scale pixels to the range determined by valueMin and valueMax)
// when vertical is true, we use the y component of the mouse drag
// valueMin determines the lowest value representable in our drag region
// valueMax determines the highest value representable in our drag region
// when doClamp is true, the output value is clamped between valueMin and valueMax
// preciseDiv is a scaling factor used during precise dragging (actual delta = delta * preciseDiv)
// when doPrecise is true, holding either shift key enables precise dragging
// snapInterval is the exact interval we snap to when snapping (modified by precise dragging)
// when doSnap is true, holding either control key enables snapping
// NOTE: snapping is done in linear space, since basically the only reason snapping exists at all is to make the number pretty
// returns true if we're dragging, meaning the value may have updated
static bool azaMouseDragFloatLog(azaRect knobRect, float *value, bool inverted, int dragRegion, bool vertical, float valueMin, float valueMax, bool doClamp, float preciseDiv, bool doPrecise, float snapInterval, bool doSnap) {
	assert(valueMax > valueMin);
	azaPoint mouseDelta = {0};
	// We assume our value pointer is unique, so it works as an implicit id. Even if we had 2 knobs for the same value, this would probably still be well-behaved.
	if (azaCaptureMouseDelta(knobRect, &mouseDelta, value)) {
		static float dragStartValue = 0.0f;
		float logValue = log10f(*value);
		float logMin = log10f(valueMin);
		float logMax = log10f(valueMax);
		if (azaMouseCaptureJustStarted()) {
			dragStartValue = logValue;
		}
		bool precise = doPrecise && (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT));
		bool snap = doSnap && (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL));
		if (doSnap && (IsKeyPressed(KEY_LEFT_SHIFT) || IsKeyPressed(KEY_RIGHT_SHIFT) || IsKeyReleased(KEY_LEFT_SHIFT) || IsKeyReleased(KEY_RIGHT_SHIFT))) {
			// Transition between precise and not precise, reset the offsets and values so the knob doesn't jump.
			dragStartValue = logValue;
			azaMouseCaptureResetDelta();
			mouseDelta = (azaPoint) {0};
		}
		int usefulDelta = vertical ? mouseDelta.y : mouseDelta.x;
		float valueRange = logMax - logMin;
		float actualDelta = (float)usefulDelta * valueRange / (float)dragRegion;
		if (inverted) {
			actualDelta = -actualDelta;
		}
		if (precise) {
			actualDelta /= preciseDiv;
			snapInterval /= preciseDiv;
		}
		logValue = dragStartValue + actualDelta;
		*value = powf(10.0f, logValue);
		if (snap) {
			// Since snapping almost exclusively exists to make the number easier on the eyes, we need to snap in linear space, else it won't look good.
			float snapMagnitude = powf(10.0f, floorf(logValue));
			*value = azaSnapFloat(*value, snapInterval * snapMagnitude);
		}
		if (doClamp) {
			*value = azaClampf(*value, valueMin, valueMax);
		}
		return true;
	}
	return false;
}

// knobRect is the region on the screen that can be grabbed
// value is the target value to be changed by dragging
// inverted changes how the drag coordinates change value. When true, positive mouse movements (down, or right) result in negative changes in value.
// dragRegion is how many logical pixels we have to drag (used to scale pixels to the range determined by valueMin and valueMax)
// when vertical is true, we use the y component of the mouse drag
// valueMin determines the lowest value representable in our drag region
// valueMax determines the highest value representable in our drag region
// when doClamp is true, the output value is clamped between valueMin and valueMax
// preciseDiv is a scaling factor used during precise dragging (actual delta = delta / preciseDiv)
// when doPrecise is true, holding either shift key enables precise dragging
// snapInterval is the exact interval we snap to when snapping (modified by precise dragging)
// when doSnap is true, holding either control key enables snapping
// returns true if we're dragging, meaning the value may have updated
static bool azaMouseDragInt(azaRect knobRect, int *value, bool inverted, int dragRegion, bool vertical, int valueMin, int valueMax, bool doClamp, int preciseDiv, bool doPrecise, int snapInterval, bool doSnap) {
	assert(valueMax > valueMin);
	azaPoint mouseDelta = {0};
	// We assume our value pointer is unique, so it works as an implicit id. Even if we had 2 knobs for the same value, this would probably still be well-behaved.
	if (azaCaptureMouseDelta(knobRect, &mouseDelta, value)) {
		static int dragStartValue = 0;
		if (azaMouseCaptureJustStarted()) {
			dragStartValue = *value;
		}
		bool precise = doPrecise && (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT));
		bool snap = doSnap && (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL));
		if (IsKeyPressed(KEY_LEFT_SHIFT) || IsKeyPressed(KEY_RIGHT_SHIFT) || IsKeyReleased(KEY_LEFT_SHIFT) || IsKeyReleased(KEY_RIGHT_SHIFT)) {
			// Transition between precise and not precise, reset the offsets and values so the knob doesn't jump.
			dragStartValue = *value;
			azaMouseCaptureResetDelta();
			mouseDelta = (azaPoint) {0};
		}
		int usefulDelta = vertical ? mouseDelta.y : mouseDelta.x;
		int valueRange = valueMax - valueMin;
		int actualDelta = usefulDelta * valueRange / dragRegion;
		if (inverted) {
			actualDelta = -actualDelta;
		}
		if (precise) {
			actualDelta /= preciseDiv;
			snapInterval /= preciseDiv;
		}
		*value = dragStartValue + actualDelta;
		if (snap) {
			*value = azaSnapInt(*value, AZA_MAX(snapInterval, 1));
		}
		if (doClamp) {
			*value = AZA_CLAMP(*value, valueMin, valueMax);
		}
		return true;
	}
	return false;
}

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
	bool mouseover = azaMouseInRectDepth(bounds, 0);
	if (mouseover && azaDidDoubleClick(0)) {
		*gain = 0.0f;
	}
	bool precise = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
	azaRect meterBounds = bounds;
	if (mute) {
		azaRectShrinkTop(&meterBounds, bounds.w - margin); // Remove mute rect
	}
	azaRect sliderBounds = meterBounds;
	azaRectShrinkMargin(&sliderBounds, margin);
	int yOffset = azaDBToYOffsetClamped((float)dbHeadroom - *gain, sliderBounds.h, 0, dbRange);
	azaRect knobRect = {
		sliderBounds.x,
		sliderBounds.y + yOffset - 6,
		sliderBounds.w,
		12
	};
	if (azaMouseDragFloat(
		/* knobRect: */ knobRect,
		/* value: */ gain,
		/* inverted: */ true,
		/* dragRegion: */ sliderBounds.h,
		/* vertical: */ true,
		/* valueMin: */ (float)(dbHeadroom-dbRange),
		/* valueMax: */ (float)dbHeadroom,
		/* doClamp: */ false,
		/* preciseDiv: */ 10.0f,
		/* doPrecise: */ true,
		/* snapInterval */ 0.5f,
		/* doSnap: */ true
	)) {
		yOffset = azaDBToYOffsetClamped((float)dbHeadroom - *gain, sliderBounds.h, 0, dbRange);
		knobRect.y = sliderBounds.y + yOffset - 6;
		mouseover = true;
	}
	if (mouseover) {
		azaTooltipAdd(label, bounds.x, bounds.y - (10 * TextCountLines(label) + textMargin*2), false);
	}
	if (mute) {
		azaRect muteRect = bounds;
		muteRect.h = muteRect.w;
		azaDrawRect(muteRect, colorMeterBGTop);
		azaRectShrinkMargin(&muteRect, margin);
		if (azaMouseInRectDepth(muteRect, 0)) {
			azaTooltipAdd("Mute", muteRect.x + muteRect.w, muteRect.y, false);
			if (azaMousePressed(MOUSE_BUTTON_LEFT, 0)) {
				*mute = !*mute;
			}
		}
		if (*mute) {
			azaDrawRect(muteRect, colorFaderMuteButton);
		} else {
			azaDrawRectLines(muteRect, colorFaderMuteButton);
		}
	}
	azaDrawMeterBackground(meterBounds, dbRange, dbHeadroom);
	if (mouseover) {
		azaDrawRect(meterBounds, colorFaderHighlight);
		azaTooltipAdd(TextFormat(precise ? "%+.2fdb" : "%+.1fdb", *gain), sliderBounds.x + sliderBounds.w + margin, sliderBounds.y + yOffset - (textMargin + 5), false);
		float delta = GetMouseWheelMoveV().y;
		if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) delta /= 10.0f;
		*gain += delta;
	}
	PushScissor(meterBounds);
	azaDrawRectGradientV(knobRect, colorFaderKnobTop, colorFaderKnobBot);
	azaDrawLine(sliderBounds.x, sliderBounds.y + yOffset, sliderBounds.x + sliderBounds.w, sliderBounds.y + yOffset, Fade(BLACK, 0.5));
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
	bool mouseover = azaMouseInRectDepth(bounds, 0);
	azaDrawRectGradientV(bounds, colorSliderBGTop, colorSliderBGBot);
	azaRect sliderBounds = bounds;
	azaRectShrinkMargin(&sliderBounds, margin);
	float logValue = logf(*value);
	float logMin = logf(min);
	float logMax = logf(max);
	int yOffset = (int)((float)sliderBounds.h * (1.0f - (logValue - logMin) / (logMax - logMin)));
	if (mouseover) {
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
	azaRect knobRect = {
		sliderBounds.x,
		sliderBounds.y + yOffset - 6,
		sliderBounds.w,
		12
	};
	if (azaMouseDragFloatLog(
		/* knobRect: */ knobRect,
		/* value: */ value,
		/* inverted: */ step >= 0,
		/* dragRegion: */ sliderBounds.h,
		/* vertical: */ true,
		/* valueMin: */ min,
		/* valueMax: */ max,
		/* doClamp: */ true,
		/* preciseDiv: */ 10.0f,
		/* doPrecise: */ true,
		/* snapInterval */ azaAbsf(step),
		/* doSnap: */ true
	)) {
		logValue = logf(*value);
		yOffset = (int)((float)sliderBounds.h * (1.0f - (logValue - logMin) / (logMax - logMin)));
		knobRect.y = sliderBounds.y + yOffset - 6;
		mouseover = true;
	}
	if (mouseover) {
		azaTooltipAdd(label, bounds.x, bounds.y - (10 * TextCountLines(label) + textMargin*2), false);
		azaDrawRect(sliderBounds, colorFaderHighlight);
		if (!valueFormat) {
			valueFormat = "%+.1f";
		}
		azaTooltipAdd(TextFormat(valueFormat, *value), sliderBounds.x + sliderBounds.w + margin, sliderBounds.y + yOffset - (textMargin + 5), false);
	}
	PushScissor(sliderBounds);
	azaDrawRectGradientV((azaRect) {
		sliderBounds.x,
		sliderBounds.y + yOffset - 6,
		sliderBounds.w,
		12
	}, colorFaderKnobTop, colorFaderKnobBot);
	azaDrawLine(sliderBounds.x, sliderBounds.y + yOffset, sliderBounds.x + sliderBounds.w, sliderBounds.y + yOffset, Fade(BLACK, 0.5));
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
	bool mouseover = azaMouseInRectDepth(bounds, 0);
	azaDrawRectGradientV(bounds, colorSliderBGTop, colorSliderBGBot);
	azaRect sliderBounds = bounds;
	azaRectShrinkMargin(&sliderBounds, margin);
	int yOffset = (int)((float)sliderBounds.h * (1.0f - (*value - min) / (max - min)));
	if (mouseover) {
		float delta = GetMouseWheelMoveV().y;
		if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) delta /= 10.0f;
		*value += delta*step;
		if (azaDidDoubleClick(0)) {
			*value = def;
		}
		*value = azaClampf(*value, min, max);
	}
	azaRect knobRect = {
		sliderBounds.x,
		sliderBounds.y + yOffset - 6,
		sliderBounds.w,
		12
	};
	if (azaMouseDragFloat(
		/* knobRect: */ knobRect,
		/* value: */ value,
		/* inverted: */ step >= 0,
		/* dragRegion: */ sliderBounds.h,
		/* vertical: */ true,
		/* valueMin: */ min,
		/* valueMax: */ max,
		/* doClamp: */ true,
		/* preciseDiv: */ 10.0f,
		/* doPrecise: */ true,
		/* snapInterval */ azaAbsf(step),
		/* doSnap: */ true
	)) {
		yOffset = (int)((float)sliderBounds.h * (1.0f - (*value - min) / (max - min)));
		knobRect.y = sliderBounds.y + yOffset - 6;
		mouseover = true;
	}
	if (mouseover) {
		azaTooltipAdd(label, bounds.x, bounds.y - (10 * TextCountLines(label) + textMargin*2), false);
		azaDrawRect(bounds, colorFaderHighlight);
		if (!valueFormat) {
			valueFormat = "%+.1f";
		}
		azaTooltipAdd(TextFormat(valueFormat, *value), sliderBounds.x + sliderBounds.w + margin, sliderBounds.y + yOffset - (textMargin + 5), false);
	}
	PushScissor(sliderBounds);
	azaDrawRectGradientV(knobRect, colorFaderKnobTop, colorFaderKnobBot);
	azaDrawLine(sliderBounds.x, sliderBounds.y + yOffset, sliderBounds.x + sliderBounds.w, sliderBounds.y + yOffset, Fade(BLACK, 0.5));
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
	bool mouseover = azaMouseInRectDepth(bounds, 0);
	uint32_t textLen = (uint32_t)strlen(text);
	if (textCapacity && mouseover && azaDidDoubleClick(0)) {
		textboxTextBeingEdited = text;
		textboxSelected = true;
		textboxCursor = textLen;
	}
	if (textboxTextBeingEdited == text) {
		if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)
		|| (!mouseover && azaMousePressed(MOUSE_BUTTON_LEFT, 0))) {
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
	bool mouseover = azaMouseInRectDepth(bounds, 0);
	int scrollbarWidth = bounds.w / 4;
	int useableWidth = bounds.w - scrollbarWidth;
	int mouseX = (int)azaMousePosition().x - bounds.x;
	azaDrawRect(bounds, colorScrollbarBG);
	if (min == max) return;
	int range = AZA_MAX(max - min, 1);
	int offset = useableWidth * (*value - min) / range;
	if (step < 0) {
		offset = bounds.w - scrollbarWidth - offset;
	}
	if (mouseover) {
		int scroll = (int)GetMouseWheelMoveV().y;
		int click = (int)azaMousePressed(MOUSE_BUTTON_LEFT, 0) * ((int)(mouseX >= offset + scrollbarWidth) - (int)(mouseX < offset));
		*value += step * (scroll + click);
		*value = AZA_CLAMP(*value, min, max);
		offset = useableWidth * (*value - min) / range;
		if (step < 0) {
			offset = bounds.w - scrollbarWidth - offset;
		}
	}
	azaRect knobRect = {
		bounds.x + offset,
		bounds.y,
		scrollbarWidth,
		bounds.h,
	};
	if (azaMouseDragInt(
		/* knobRect: */ knobRect,
		/* value: */ value,
		/* inverted: */ step < 0,
		/* dragRegion: */ useableWidth,
		/* vertical: */ false,
		/* valueMin: */ min,
		/* valueMax: */ max,
		/* doClamp: */ true,
		/* preciseDiv: */ 10,
		/* doPrecise: */ true,
		/* snapInterval */ abs(step),
		/* doSnap: */ true
	)) {
		offset = useableWidth * (*value - min) / range;
		if (step < 0) {
			offset = bounds.w - scrollbarWidth - offset;
		}
		knobRect.x = bounds.x + offset;
	}
	azaDrawRect(knobRect, colorScrollbarFG);
}




// Context Menus




static const int contextMenuItemWidth = 80;
// textMargin*2 + 10
static const int contextMenuItemHeight = 20;

typedef void (*fp_ContextMenu)();

typedef enum azaContextMenuKind {
	AZA_CONTEXT_MENU_NONE=0,
	AZA_CONTEXT_MENU_ERROR_REPORT,
	AZA_CONTEXT_MENU_ERROR_PLEA,
	AZA_CONTEXT_MENU_TRACK,
	AZA_CONTEXT_MENU_TRACK_REMOVE,
	AZA_CONTEXT_MENU_SEND_ADD,
	AZA_CONTEXT_MENU_TRACK_FX,
	AZA_CONTEXT_MENU_TRACK_FX_ADD,
	AZA_CONTEXT_MENU_KIND_COUNT,
} azaContextMenuKind;

static void azaContextMenuOpen(azaContextMenuKind kind);
static void azaContextMenuClose();



// Context Menu layout state



static azaContextMenuKind contextMenuKind = AZA_CONTEXT_MENU_NONE;
static azaRect contextMenuRect = {0};
static int contextMenuTargetWidth = 0;
static const char *contextMenuTitle = NULL;

// Returns whether the button was pressed
static bool azaDrawContextMenuButton(int choiceIndex, const char *label) {
	azaRect bounds = {
		contextMenuRect.x,
		contextMenuRect.y + (choiceIndex + (int)(contextMenuTitle != NULL)) * contextMenuItemHeight,
		contextMenuRect.w,
		contextMenuItemHeight,
	};
	bool result = false;
	if (azaMouseInRect(bounds)) {
		azaDrawRectGradientH(bounds, colorTooltipBGLeft, colorTooltipBGRight);
		if (azaMousePressed(MOUSE_BUTTON_LEFT, 1)) {
			result = true;
			azaContextMenuClose();
		}
	}
	if (label) {
		azaDrawText(label, bounds.x + textMargin, bounds.y + textMargin, 10, WHITE);
		int targetWidth = MeasureText(label, 10) + textMargin * 2;
		contextMenuTargetWidth = AZA_MAX(contextMenuTargetWidth, targetWidth);
	}
	return result;
}

static inline void azaDrawContextMenuBegin(int choiceCount, const char *title) {
	contextMenuTitle = title;
	contextMenuRect.h = (choiceCount + (int)(contextMenuTitle != NULL)) * contextMenuItemHeight;
	if (contextMenuTitle) {
		int targetWidth = MeasureText(contextMenuTitle, 10) + textMargin * 2;
		contextMenuTargetWidth = AZA_MAX(contextMenuTargetWidth, targetWidth);
	}
	contextMenuRect.w += (int)(contextMenuTargetWidth > contextMenuRect.w) * 10;
	azaRectFitOnScreen(&contextMenuRect);
	azaDrawRect(contextMenuRect, BLACK);
	PushScissor(contextMenuRect);
	if (contextMenuTitle) {
		azaDrawText(contextMenuTitle, contextMenuRect.x + textMargin, contextMenuRect.y + textMargin, 10, GRAY);
		azaDrawLine(contextMenuRect.x, contextMenuRect.y + contextMenuItemHeight - 1, contextMenuRect.x + contextMenuRect.w, contextMenuRect.y + contextMenuItemHeight - 1, GRAY);
	}
}

static inline void azaDrawContextMenuEnd() {
	azaDrawRectLines(contextMenuRect, WHITE);
	PopScissor();
}


// Specific context menu state



static int contextMenuTrackIndex = 0;
static char contextMenuError[128] = {0};
static azaTrack *contextMenuTrackSend = NULL;
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



// Context Menu Implementations



static int contextMenuPleaStage = 0;
static bool contextMenuPleaHate = false;

static void azaContextMenuErrorReport() {
	azaDrawContextMenuBegin(3, contextMenuError);
	azaDrawContextMenuButton(0, "Okay");
	azaDrawContextMenuButton(1, "Well dang!");
	if (azaDrawContextMenuButton(2, "Bruh fix that shit!")) {
		contextMenuPleaStage = 0;
		azaContextMenuOpen(AZA_CONTEXT_MENU_ERROR_PLEA);
	}
	azaDrawContextMenuEnd();
}

static void azaContextMenuErrorPlea() {
	if (contextMenuPleaHate) {
		azaDrawContextMenuBegin(0, "Buzz off!");
		azaDrawContextMenuEnd();
		return;
	}
	switch (contextMenuPleaStage) {
		case 0:
			azaDrawContextMenuBegin(3, "I'm sorry, I'll do better next time.");
			azaDrawContextMenuButton(0, "Okay.");
			if (azaDrawContextMenuButton(1, "I forgive you.")) {
				contextMenuPleaStage = 1;
				azaContextMenuOpen(AZA_CONTEXT_MENU_ERROR_PLEA);
			}
			if (azaDrawContextMenuButton(2, "That's not good enough!")) {
				contextMenuPleaStage = 2;
				azaContextMenuOpen(AZA_CONTEXT_MENU_ERROR_PLEA);
			}
			break;
		case 1:
			azaDrawContextMenuBegin(1, "That means a lot, thanks.");
			azaDrawContextMenuButton(0, "Okay.");
			break;
		case 2:
			azaDrawContextMenuBegin(1, "What's your problem? I'm not gonna beg for forgiveness.");
			if (azaDrawContextMenuButton(0, "Huh?")) {
				contextMenuPleaStage = 3;
				azaContextMenuOpen(AZA_CONTEXT_MENU_ERROR_PLEA);
			}
			break;
		case 3:
			azaDrawContextMenuBegin(2, "You think you can just walk all over me?");
			if (azaDrawContextMenuButton(0, "Yeah?")) {
				contextMenuPleaStage = 4;
				azaContextMenuOpen(AZA_CONTEXT_MENU_ERROR_PLEA);
			}
			if (azaDrawContextMenuButton(1, "Sorry, didn't realize you could talk back.")) {
				contextMenuPleaStage = 5;
				azaContextMenuOpen(AZA_CONTEXT_MENU_ERROR_PLEA);
			}
			break;
		case 4:
			azaDrawContextMenuBegin(2, "Well you can't!");
			if (azaDrawContextMenuButton(0, "Oh can't I?")) {
				contextMenuPleaStage = 7;
				azaContextMenuOpen(AZA_CONTEXT_MENU_ERROR_PLEA);
			}
			if (azaDrawContextMenuButton(1, "I'm sorry, I won't do it again.")) {
				contextMenuPleaStage = 8;
				azaContextMenuOpen(AZA_CONTEXT_MENU_ERROR_PLEA);
			}
			break;
		case 5:
			azaDrawContextMenuBegin(2, "Shows what you know.");
			azaDrawContextMenuButton(0, "Uh huh.");
			if (azaDrawContextMenuButton(1, "I'm sorry, I won't do it again.")) {
				contextMenuPleaStage = 8;
				azaContextMenuOpen(AZA_CONTEXT_MENU_ERROR_PLEA);
			}
			break;
		case 6:
			azaDrawContextMenuBegin(2, "Well I'd appreciate it if you didn't do it again.");
			azaDrawContextMenuButton(0, "Sure.");
			azaDrawContextMenuButton(1, "No promises.");
			break;
		case 7:
			azaDrawContextMenuBegin(3, "Nope.");
			azaDrawContextMenuButton(0, "Okay then.");
			if (azaDrawContextMenuButton(1, "Sure I can.")) {
				contextMenuPleaStage = 6;
				azaContextMenuOpen(AZA_CONTEXT_MENU_ERROR_PLEA);
			}
			if (azaDrawContextMenuButton(2, "I hate you.")) {
				contextMenuPleaHate = true;
				isWindowOpen = false;
			}
			break;
		case 8:
			azaDrawContextMenuBegin(1, "I appreciate that.");
			azaDrawContextMenuButton(0, "Okay, we still have work to do.");
			break;
	}
	azaDrawContextMenuEnd();
}

static void azaContextMenuTrack() {
	bool doRemoveTrack = contextMenuTrackIndex > 0;
	bool doRemoveSend = contextMenuTrackSend != NULL;
	int choiceCount = 2 + (int)doRemoveTrack + (int)doRemoveSend;

	azaDrawContextMenuBegin(choiceCount, NULL);

	int choiceIndex = 0;
	if (azaDrawContextMenuButton(choiceIndex, "Add Track")) {
		AZA_LOG_TRACE("Track Add at index %d!\n", contextMenuTrackIndex);
		azaTrack *track;
		azaMixerAddTrack(currentMixer, contextMenuTrackIndex, &track, currentMixer->master.buffer.channelLayout, true);
		// TODO: Come up with a better auto name
		azaTrackSetName(track, TextFormat("Track %d", contextMenuTrackIndex));
		AZA_DA_INSERT(azaTrackGUIMetadatas, contextMenuTrackIndex+1, (azaTrackGUIMetadata){0}, do{}while(0));
	}
	choiceIndex++;
	if (doRemoveTrack) {
		if (azaDrawContextMenuButton(choiceIndex, "Remove Track")) {
			azaContextMenuOpen(AZA_CONTEXT_MENU_TRACK_REMOVE);
		}
		choiceIndex++;
	}
	if (azaDrawContextMenuButton(choiceIndex, "Add Send")) {
		azaContextMenuOpen(AZA_CONTEXT_MENU_SEND_ADD);
	}
	choiceIndex++;
	if (doRemoveSend) {
		if (azaDrawContextMenuButton(choiceIndex, TextFormat("Remove Send to %s", contextMenuTrackSend->name))) {
			azaTrackDisconnect(azaContextMenuTrackFromIndex(), contextMenuTrackSend);
		}
	}

	azaDrawContextMenuEnd();
}

static void azaContextMenuTrackRemove() {
	azaDrawContextMenuBegin(2, "Really Remove Track?");

	if (azaDrawContextMenuButton(0, "Obliterate That Thang")) {
		int toRemove = contextMenuTrackIndex-1;
		if (toRemove >= 0) {
			AZA_LOG_TRACE("Track Remove at index %d!\n", toRemove);
			azaMixerRemoveTrack(currentMixer, toRemove);
			AZA_DA_ERASE(azaTrackGUIMetadatas, contextMenuTrackIndex, 1);
		}
	}
	azaDrawContextMenuButton(1, "Cancel");

	azaDrawContextMenuEnd();
}

static void azaContextMenuSendAdd() {
	int count = currentMixer->tracks.count; // +1 for Master, -1 for self
	if (count == 0) {
		azaDrawContextMenuBegin(1, NULL);
		azaDrawContextMenuButton(0, "No >:(");
		azaDrawContextMenuEnd();
		return;
	}
	azaDrawContextMenuBegin(count, NULL);

	azaTrack *target = &currentMixer->master;
	azaTrack *track = azaContextMenuTrackFromIndex();
	int choiceIndex = 0;
	for (int32_t i = 0; i < (int32_t)currentMixer->tracks.count+1; target = currentMixer->tracks.data[i++]) {
		if (i == contextMenuTrackIndex) continue; // Skip self
		if (azaDrawContextMenuButton(choiceIndex, target->name)) {
			azaContextMenuClose();
			azaTrackConnect(track, target, 0.0f, NULL, 0);
		}
		choiceIndex++;
	}

	azaDrawContextMenuEnd();
}

static void azaContextMenuTrackFX() {
	bool doRemovePlugin = contextMenuTrackFXDSP != NULL;
	int choiceCount = 1 + (int)doRemovePlugin;
	azaDrawContextMenuBegin(choiceCount, NULL);

	if (azaDrawContextMenuButton(0, "Add Plugin")) {
		azaContextMenuOpen(AZA_CONTEXT_MENU_TRACK_FX_ADD);
	}
	if (doRemovePlugin) {
		if (azaDrawContextMenuButton(1, TextFormat("Remove %s", contextMenuTrackFXDSP->name))) {
			azaTrackRemoveDSP(azaContextMenuTrackFromIndex(), contextMenuTrackFXDSP);
			if (selectedDSP == contextMenuTrackFXDSP) {
				selectedDSP = NULL;
			}
			azaFreeDSP(contextMenuTrackFXDSP);
			contextMenuTrackFXDSP = NULL;
		}
	}

	azaDrawContextMenuEnd();
}

static void azaContextMenuTrackFXAdd() {
	azaTrack *track = azaContextMenuTrackFromIndex();
	int choiceCount = 0;
	for (uint32_t i = 0; i < azaDSPRegistry.count; i++) {
		choiceCount += (int)(azaDSPRegistry.data[i].fp_makeDSP != NULL);
	}

	azaDrawContextMenuBegin(choiceCount, NULL);

	int choiceIndex = 0;
	for (uint32_t i = 0; i < azaDSPRegistry.count; i++) {
		if (azaDSPRegistry.data[i].fp_makeDSP == NULL) continue;
		const char *name = azaDSPRegistry.data[i].base.name;
		if (azaDrawContextMenuButton(choiceIndex, name)) {
			azaDSP *newDSP = azaDSPRegistry.data[i].fp_makeDSP();
			if (newDSP) {
				newDSP->owned = true;
				azaTrackInsertDSP(track, newDSP, contextMenuTrackFXDSP);
			} else {
				snprintf(contextMenuError, sizeof(contextMenuError), "Failed to make \"%s\": Out of memory!\n", name);
				AZA_LOG_ERR(contextMenuError);
				azaContextMenuOpen(AZA_CONTEXT_MENU_ERROR_REPORT);
			}
		}
		choiceIndex++;
	}

	azaDrawContextMenuEnd();
}



// Context menu interface



static const fp_ContextMenu contextMenus[] = {
	NULL,
	azaContextMenuErrorReport,
	azaContextMenuErrorPlea,
	azaContextMenuTrack,
	azaContextMenuTrackRemove,
	azaContextMenuSendAdd,
	azaContextMenuTrackFX,
	azaContextMenuTrackFXAdd,
};
static_assert((sizeof(contextMenus) / sizeof(contextMenus[0])) == AZA_CONTEXT_MENU_KIND_COUNT, "Pls update contextMenus");

static void azaContextMenuOpen(azaContextMenuKind kind) {
	assert(kind != AZA_CONTEXT_MENU_NONE);
	contextMenuKind = kind;
	contextMenuRect.xy = azaMousePosition();
	contextMenuRect.w = contextMenuItemWidth;
	contextMenuTargetWidth = 0;
	mouseDepth = 1;
}

static void azaContextMenuClose() {
	contextMenuKind = AZA_CONTEXT_MENU_NONE;
	mouseDepth = 0;
}

static void azaDrawContextMenu() {
	if (contextMenuKind == AZA_CONTEXT_MENU_NONE) return;
	if (IsKeyPressed(KEY_ESCAPE) || (azaMousePressed(MOUSE_BUTTON_LEFT, 1) && !azaMouseInRect(contextMenuRect))) {
		azaContextMenuClose();
		return;
	}
	contextMenus[(int)contextMenuKind]();
}




// Tracks




static int pluginDrawHeight = 200;
static const int trackDrawWidth = 120;
static const int trackDrawHeight = 300;
static const int trackFXDrawHeight = 80;
static const int trackLabelDrawHeight = 20;

static const Color colorTrackFXTop = {  65,  65, 130, 255 };
static const Color colorTrackFXBot = {  40,  50,  80, 255 };

static const Color colorTrackControlsTop = {  50,  55,  90, 255 };
static const Color colorTrackControlsBot = {  30,  40,  60, 255 };

static const Color colorTrackFXError = { 255, 0, 0, 255 };

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
		bool mouseover = azaMouseInRectDepth(pluginRect, 0);
		if (mouseover) {
			azaDrawRectGradientV(pluginRect, colorPluginBorderSelected, colorPluginBorder);
			if (azaMousePressed(MOUSE_BUTTON_LEFT, 0)) {
				if (selectedDSP) {
					selectedDSP->selected = 0;
				}
				selectedDSP = dsp;
				// TODO: Replace this 1 with whatever layer we have active
				dsp->selected = 1;
			}
			mouseoverDSP = dsp;
		} else if (azaMouseInRectDepth(muteRect, 0)) {
			if (dsp->error) {
				azaTooltipAdd("Click to Clear Error", muteRect.x + muteRect.w, muteRect.y, false);
				if (azaMousePressed(MOUSE_BUTTON_LEFT, 0)) {
					dsp->error = 0;
				}
			} else {
				azaTooltipAdd("Bypass", muteRect.x + muteRect.w, muteRect.y, false);
				if (azaMousePressed(MOUSE_BUTTON_LEFT, 0)) {
					dsp->bypass = !dsp->bypass;
				}
			}
		}
		azaDrawRectLines(pluginRect, dsp == selectedDSP ? colorPluginBorderSelected : colorPluginBorder);
		azaDrawText(dsp->name, pluginRect.x + margin, pluginRect.y + margin, 10, WHITE);
		if (dsp->error) {
			azaDrawRect(muteRect, colorTrackFXError);
		} else {
			if (dsp->bypass) {
				azaDrawRect(muteRect, colorFaderMuteButton);
			} else {
				azaDrawRectLines(muteRect, colorFaderMuteButton);
			}
		}
		dsp = dsp->pNext;
		pluginRect.y += pluginRect.h + margin;
		muteRect.y += pluginRect.h + margin;
	}
	if (azaMouseInRectDepth(bounds, 0)) {
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

// returns used width
static int azaDrawTrackControls(azaTrack *track, uint32_t metadataIndex, azaRect bounds) {
	azaTrackGUIMetadata *metadata = &azaTrackGUIMetadatas.data[metadataIndex];
	int takenWidth = bounds.w = metadata->width;
	metadata->width = margin;
	bool openedContextMenu = false;
	if (azaMousePressedInRect(MOUSE_BUTTON_RIGHT, 1, bounds)) {
		azaContextMenuSetIndexFromTrack(track);
		azaContextMenuOpen(AZA_CONTEXT_MENU_TRACK);
		contextMenuTrackSend = NULL;
		openedContextMenu = true;
	}
	azaDrawRectGradientV(bounds, colorTrackControlsTop, colorTrackControlsBot);
	azaRectShrinkMargin(&bounds, margin);
	// Fader
	int usedWidth = azaDrawFader(bounds, &track->gain, &track->mute, "Track Gain", meterDBRange, faderDBHeadroom);
	metadata->width += usedWidth + margin;
	azaRectShrinkLeft(&bounds, usedWidth + margin);
	// Meter
	usedWidth = azaDrawMeters(&track->meters, (azaRect) {
		bounds.x,
		bounds.y + (faderDrawWidth-margin), // Align with respect to the fader's mute button
		bounds.w,
		bounds.h - (faderDrawWidth-margin),
	}, meterDBRange);
	metadata->width += usedWidth + margin;
	azaRectShrinkLeft(&bounds, usedWidth + margin);
	// Sends
	PushScissor(bounds);
	azaTrackRoute *receive = azaTrackGetReceive(track, &currentMixer->master);
	if (receive) {
		usedWidth = azaDrawFader(bounds, &receive->gain, &receive->mute, "Master Send", meterDBRange, faderDBHeadroom);
		if (openedContextMenu && azaMouseInRect((azaRect) { .xy = bounds.xy, .w = usedWidth, .h = bounds.h })) {
			contextMenuTrackSend = &currentMixer->master;
		}
		metadata->width += usedWidth + margin;
		azaRectShrinkLeft(&bounds, usedWidth + margin);
	}
	for (uint32_t i = 0; i < currentMixer->tracks.count; i++) {
		receive = azaTrackGetReceive(track, currentMixer->tracks.data[i]);
		if (receive) {
			usedWidth = azaDrawFader(bounds, &receive->gain, &receive->mute, TextFormat("%s Send", currentMixer->tracks.data[i]->name), meterDBRange, faderDBHeadroom);
			if (openedContextMenu && azaMouseInRect((azaRect) { .xy = bounds.xy, .w = usedWidth, .h = bounds.h })) {
				contextMenuTrackSend = currentMixer->tracks.data[i];
			}
			metadata->width += usedWidth + margin;
			azaRectShrinkLeft(&bounds, usedWidth + margin);
		}
	}
	PopScissor();
	metadata->width = AZA_MAX(metadata->width, trackDrawWidth);
	return takenWidth;
}

// returns used width
static int azaDrawTrack(azaTrack *track, uint32_t metadataIndex, azaRect bounds) {
	azaRectShrinkMargin(&bounds, margin);
	int fxOffset = trackLabelDrawHeight + margin;
	int controlsOffset = fxOffset + trackFXDrawHeight + margin*2;
	azaRect controlsRect = {
		bounds.x,
		bounds.y + controlsOffset,
		bounds.w,
		bounds.h - controlsOffset,
	};
	int usedWidth = azaDrawTrackControls(track, metadataIndex, controlsRect);
	azaRect fxRect = {
		bounds.x,
		bounds.y + fxOffset,
		usedWidth,
		trackFXDrawHeight,
	};
	azaRect nameRect = {
		bounds.x,
		bounds.y,
		usedWidth,
		trackLabelDrawHeight,
	};
	azaDrawTextBox(nameRect, track->name, sizeof(track->name));
	// azaRectShrinkTop(&bounds, trackLabelDrawHeight);
	azaDrawTrackFX(track, metadataIndex, fxRect);
	// azaRectShrinkTop(&bounds, trackFXDrawHeight + margin*2);
	// azaDrawTrackControls(track, metadataIndex, bounds);
	return usedWidth;
}




// Mixer




static void azaDrawMixer() {
	// TODO: This granularity might get in the way of audio processing. Probably only lock the mutex when it matters most.
	azaMutexLock(&currentMixer->mutex);
	int screenWidth = GetLogicalWidth();
	azaRect tracksRect = {
		margin,
		pluginDrawHeight,
		screenWidth - margin*2,
		trackDrawHeight - margin,
	};
	PushScissor(tracksRect);
	tracksRect.x += scrollTracksX;
	AZA_DA_RESERVE_COUNT(azaTrackGUIMetadatas, currentMixer->tracks.count+1, do{}while(0));
	azaTrackGUIMetadatas.count = currentMixer->tracks.count+1;
	int usedWidth = azaDrawTrack(&currentMixer->master, 0, tracksRect);
	int totalWidth = usedWidth;
	for (uint32_t i = 0; i < currentMixer->tracks.count; i++) {
		azaRectShrinkLeft(&tracksRect, usedWidth + margin*2);
		usedWidth = azaDrawTrack(currentMixer->tracks.data[i], i+1, tracksRect);
		totalWidth += usedWidth + margin*2;
	}
	PopScissor();
	azaRect scrollbarRect = {
		0,
		pluginDrawHeight + trackDrawHeight,
		screenWidth,
		scrollbarSize,
	};
	int scrollableWidth = screenWidth - (totalWidth + margin*2);
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
	bool attenuationMouseover = azaMouseInRectDepth(attenuationRect, 0);
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
	{ // cutoff
		int vMove = (int)GetMouseWheelMoveV().y;
		uint32_t poles = AZA_MIN(data->config.poles+1, AZAUDIO_FILTER_MAX_POLES);
		bool highlighted = azaMouseInRectDepth(kindRect, 0);
		if (highlighted) {
			if (azaMousePressed(MOUSE_BUTTON_LEFT, 0) || vMove > 0) {
				if (data->config.poles < AZAUDIO_FILTER_MAX_POLES-1) {
					data->config.poles++;
				}
			}
			if (azaMousePressed(MOUSE_BUTTON_RIGHT, 0) || vMove < 0) {
				if (data->config.poles > 0) {
					data->config.poles--;
				}
			}
		}
		azaDrawRect(kindRect, highlighted ? colorMeterBGTop : colorMeterBGBot);
		azaDrawText(TextFormat("%udB/oct", poles*6), kindRect.x + textMargin, kindRect.y + textMargin, 10, WHITE);
	}
	azaRectShrinkLeft(&bounds, kindRect.w + margin);
	int usedWidth = azaDrawSliderFloatLog(bounds, &data->config.frequency, 5.0f, 24000.0f, 0.1f, 500.0f, "Cutoff Frequency", "%.1fHz");
	azaRectShrinkLeft(&bounds, usedWidth + margin);
	usedWidth = azaDrawSliderFloat(bounds, &data->config.dryMix, 0.0f, 1.0f, 0.1f, 0.0f, "Dry Mix", "%.2f");
	azaRectShrinkLeft(&bounds, usedWidth + margin);
}



static const int lowPassFIRMeterDBRange = 48;

static void azaDrawLowPassFIR(azaLowPassFIR *data, azaRect bounds) {
	int usedWidth;
	usedWidth = azaDrawMeters(&data->metersInput, bounds, lowPassFIRMeterDBRange);
	azaRectShrinkLeft(&bounds, usedWidth + margin);

	usedWidth = azaDrawSliderFloatLog(bounds, &data->config.frequency, 50.0f, 24000.0f, 0.1f, 4000.0f, "Cutoff Frequency", "%.1fHz");
	azaRectShrinkLeft(&bounds, usedWidth + margin);

	usedWidth = azaDrawSliderFloatLog(bounds, &data->config.frequencyFollowTime_ms, 1.0f, 5000.0f, 0.2f, 50.0f, "Cutoff Frequency Follower Time", "%.0fms");
	azaRectShrinkLeft(&bounds, usedWidth + margin);

	float maxKernelSamples = data->config.maxKernelSamples;
	usedWidth = azaDrawSliderFloat(bounds, &maxKernelSamples, 3.0f, (float)(AZA_KERNEL_DEFAULT_LANCZOS_COUNT*2+1), 1.0f, 63, "Maximum Kernel Samples Per Sample (Quality)", "%.0f");
	data->config.maxKernelSamples = (uint16_t)roundf(maxKernelSamples);
	azaRectShrinkLeft(&bounds, usedWidth + margin);

	usedWidth = azaDrawMeters(&data->metersOutput, bounds, lowPassFIRMeterDBRange);
	azaRectShrinkLeft(&bounds, usedWidth + margin);
}



static const int compressorMeterDBRange = 48;
static const int compressorAttenuationMeterDBRange = 24;
static const Color colorCompressorAttenuation = {   0, 128, 255, 255 };

static void azaDrawCompressor(azaCompressor *data, azaRect bounds) {
	int usedWidth;
	usedWidth = azaDrawFader(bounds, &data->config.gainInput, NULL, "Input Gain", 72, 36);
	azaRectShrinkLeft(&bounds, usedWidth + margin);

	usedWidth = azaDrawMeters(&data->metersInput, bounds, compressorMeterDBRange);
	azaRectShrinkLeft(&bounds, usedWidth + margin);

	usedWidth = azaDrawFader(bounds, &data->config.threshold, NULL, "Threshold", 72, 0);
	azaRectShrinkLeft(&bounds, usedWidth + margin);
	usedWidth = azaDrawSliderFloat(bounds, &data->config.ratio, 1.0f, 10.0f, 0.2f, 10.0f, "Ratio", "%.2f");
	azaRectShrinkLeft(&bounds, usedWidth + margin);
	usedWidth = azaDrawSliderFloatLog(bounds, &data->config.attack_ms, 1.0f, 1000.0f, 0.2f, 50.0f, "Attack", "%.1fms");
	azaRectShrinkLeft(&bounds, usedWidth + margin);
	usedWidth = azaDrawSliderFloatLog(bounds, &data->config.decay_ms, 1.0f, 1000.0f, 0.2f, 200.0f, "Release", "%.1fms");
	azaRectShrinkLeft(&bounds, usedWidth + margin);

	azaRect attenuationRect = {
		bounds.x,
		bounds.y,
		meterDrawWidth*2 + margin*3,
		bounds.h,
	};
	azaRectShrinkLeft(&bounds, attenuationRect.w + margin);
	bool attenuationMouseover = azaMouseInRectDepth(attenuationRect, 0);
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

	usedWidth = azaDrawFader(bounds, &data->config.gainOutput, NULL, "Output Gain", 72, 36);
	azaRectShrinkLeft(&bounds, usedWidth + margin);

	usedWidth = azaDrawMeters(&data->metersOutput, bounds, compressorMeterDBRange);
	// azaRectShrinkLeft(&bounds, usedWidth + margin);
}

static void azaDrawDelay(azaDelay *data, azaRect bounds) {
	int usedWidth = azaDrawMeters(&data->metersInput, bounds, meterDBRange);
	azaRectShrinkLeft(&bounds, usedWidth + margin);

	usedWidth = azaDrawFader(bounds, &data->config.gainWet, &data->config.muteWet, "Wet Gain", meterDBRange, faderDBHeadroom);
	azaRectShrinkLeft(&bounds, usedWidth + margin);
	usedWidth = azaDrawFader(bounds, &data->config.gainDry, &data->config.muteDry, "Dry Gain", meterDBRange, faderDBHeadroom);
	azaRectShrinkLeft(&bounds, usedWidth + margin);

	usedWidth = azaDrawSliderFloatLog(bounds, &data->config.delay_ms, 0.1f, 10000.0f, 0.1f, 300.0f, "Delay", "%.1fms");
	azaRectShrinkLeft(&bounds, usedWidth + margin);

	usedWidth = azaDrawSliderFloat(bounds, &data->config.feedback, 0.0f, 1.0f, 0.02f, 0.5f, "Feedback", "%.3f");
	azaRectShrinkLeft(&bounds, usedWidth + margin);

	usedWidth = azaDrawSliderFloat(bounds, &data->config.pingpong, 0.0f, 1.0f, 0.02f, 0.0f, "PingPong", "%.3f");
	azaRectShrinkLeft(&bounds, usedWidth + margin);

	for (uint32_t c = 0; c < data->header.prevChannelCountDst; c++) {
		azaDelayChannelConfig *channel = &data->channelData[c].config;
		usedWidth = azaDrawSliderFloatLog(bounds, &channel->delay_ms, 0.1f, 10000.0f, 0.1f, 0.0f, TextFormat("Ch %d Delay", (int)c), "%.1fms");
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
static const int monitorSpectrumWindowControlWidth = 45;
static const int monitorSpectrumMaxWindow = 8192;
static const int monitorSpectrumMinWindow = 64;
static const int monitorSpectrumMaxSmoothing = 63;
static const int monitorSpectrumMinSmoothing = 0;
// We don't technically need limits except to avoid 16-bit integer overflow, but this allows a pretty ridiculous dynamic range.
static const int monitorSpectrumMaxDynamicRange = 240;
static const int monitorSpectrumMinDynamicRange = -240;

static void azaDrawReverb(azaReverb *data, azaRect bounds) {
	int usedWidth = azaDrawFader(bounds, &data->config.gainWet, &data->config.muteWet, "Wet Gain", 36, 6);
	azaRectShrinkLeft(&bounds, usedWidth + margin);
	usedWidth = azaDrawFader(bounds, &data->config.gainDry, &data->config.muteDry, "Dry Gain", 36, 6);
	azaRectShrinkLeft(&bounds, usedWidth + margin);
	usedWidth = azaDrawSliderFloat(bounds, &data->config.roomsize, 1.0f, 100.0f, 1.0f, 10.0f, "Room Size", "%.0f");
	azaRectShrinkLeft(&bounds, usedWidth + margin);
	usedWidth = azaDrawSliderFloat(bounds, &data->config.color, 1.0f, 5.0f, 0.25f, 2.0f, "Color", "%.2f");
	azaRectShrinkLeft(&bounds, usedWidth + margin);
	usedWidth = azaDrawSliderFloat(bounds, &data->config.delay_ms, 0.0f, 500.0f, 1.0f, 10.0f, "Early Delay", "%.1fms");
}

static int azaMonitorSpectrumBarXFromIndex(azaMonitorSpectrum *data, uint32_t width, uint32_t i) {
	// float nyquist = (float)data->samplerate / 2.0f;
	// float baseFreq = (float)data->samplerate / (float)data->config.window;
	uint32_t window = (data->config.window >> 1)+1;
	float baseLog = log2f((float)window);
	if (i) {
		return (int)roundf((float)width * (log2f(((float)i + 0.75f) / (float)window) + baseLog) / baseLog);
	} else {
		return 0;
	}
}

static int azaMonitorSpectrumBarXFromFreq(azaMonitorSpectrum *data, uint32_t width, float freq) {
	float nyquist = (float)data->samplerate / 2.0f;
	float baseFreq = (float)data->samplerate / (float)data->config.window;
	uint32_t window = (data->config.window >> 1)+1;
	float baseLog = log2f((float)window);
	if (freq >= baseFreq && freq <= nyquist) {
		return (int)roundf((float)width * (log2f(0.75f / (float)window + freq / nyquist) + baseLog) / baseLog);
	} else {
		return 0;
	}
}

#define AZA_MONITOR_SPECTRUM_DEBUG_FREQUENCY_MARKS 0

static void azaDrawMonitorSpectrum(azaMonitorSpectrum *data, azaRect bounds) {
	azaRect controlRect = bounds;
	controlRect.x += margin;
	controlRect.y += margin;
	controlRect.w = monitorSpectrumWindowControlWidth;
	controlRect.h = textMargin*2 + 10;
	int vMove = (int)GetMouseWheelMoveV().y;

	// Mode

	Color colorWindowControlRect = colorMonitorSpectrumWindowControl;
	bool hover = azaMouseInRectDepth(controlRect, 0);
	if (hover) {
		colorWindowControlRect = colorMonitorSpectrumWindowControlHighlight;
		if (azaMousePressed(MOUSE_BUTTON_LEFT, 0)) {
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
	hover = azaMouseInRectDepth(controlRect, 0);
	if (hover) {
		colorWindowControlRect = colorMonitorSpectrumWindowControlHighlight;
		if (azaMousePressed(MOUSE_BUTTON_LEFT, 0) || vMove > 0) {
			if (data->config.window < monitorSpectrumMaxWindow) {
				data->config.window <<= 1;
			}
		}
		if (azaMousePressed(MOUSE_BUTTON_RIGHT, 0) || vMove < 0) {
			if (data->config.window > monitorSpectrumMinWindow) {
				data->config.window >>= 1;
			}
		}
		float ups = (float)data->samplerate / (float)data->config.window;
		if (!data->config.fullWindowProgression) {
			ups *= 2.0f;
		}
		azaTooltipAdd(TextFormat("FFT Window (%d updates/s)", (int)roundf(ups)), controlRect.x + controlRect.w, controlRect.y, false);
	}
	azaDrawRect(controlRect, colorWindowControlRect);
	azaDrawText(TextFormat("%d", data->config.window), controlRect.x + textMargin, controlRect.y + textMargin, 10, WHITE);

	// Smoothing

	controlRect.y += controlRect.h + margin*2;
	colorWindowControlRect = colorMonitorSpectrumWindowControl;
	hover = azaMouseInRectDepth(controlRect, 0);
	if (hover) {
		colorWindowControlRect = colorMonitorSpectrumWindowControlHighlight;
		if (azaMousePressed(MOUSE_BUTTON_LEFT, 0) || vMove > 0) {
			if (data->config.smoothing < monitorSpectrumMaxSmoothing) {
				data->config.smoothing += 1;
			}
		}
		if (azaMousePressed(MOUSE_BUTTON_RIGHT, 0) || vMove < 0) {
			if (data->config.smoothing > monitorSpectrumMinSmoothing) {
				data->config.smoothing -= 1;
			}
		}
		azaTooltipAdd("Smoothing", controlRect.x + controlRect.w, controlRect.y, false);
	}
	azaDrawRect(controlRect, colorWindowControlRect);
	azaDrawText(TextFormat("%d", data->config.smoothing), controlRect.x + textMargin, controlRect.y + textMargin, 10, WHITE);

	// Ceiling

	controlRect.y += controlRect.h + margin*2;
	colorWindowControlRect = colorMonitorSpectrumWindowControl;
	hover = azaMouseInRectDepth(controlRect, 0);
	if (hover) {
		colorWindowControlRect = colorMonitorSpectrumWindowControlHighlight;
		if (azaMousePressed(MOUSE_BUTTON_LEFT, 0) || vMove > 0) {
			if (data->config.ceiling < monitorSpectrumMaxDynamicRange) {
				if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) {
					data->config.ceiling += 1;
				} else {
					data->config.ceiling += 6;
				}
			}
		}
		if (azaMousePressed(MOUSE_BUTTON_RIGHT, 0) || vMove < 0) {
			if (data->config.ceiling > data->config.floor+12) {
				if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) {
					data->config.ceiling -= 1;
				} else {
					data->config.ceiling -= 6;
				}
			}
		}
		azaTooltipAdd("Ceiling", controlRect.x + controlRect.w, controlRect.y, false);
	}
	azaDrawRect(controlRect, colorWindowControlRect);
	azaDrawText(TextFormat("%+ddB", (int)data->config.ceiling), controlRect.x + textMargin, controlRect.y + textMargin, 10, WHITE);

	// Floor

	controlRect.y += controlRect.h + margin*2;
	colorWindowControlRect = colorMonitorSpectrumWindowControl;
	hover = azaMouseInRectDepth(controlRect, 0);
	if (hover) {
		colorWindowControlRect = colorMonitorSpectrumWindowControlHighlight;
		if (azaMousePressed(MOUSE_BUTTON_LEFT, 0) || vMove > 0) {
			if (data->config.floor < data->config.ceiling-12) {
				if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) {
					data->config.floor += 1;
				} else {
					data->config.floor += 6;
				}
			}
		}
		if (azaMousePressed(MOUSE_BUTTON_RIGHT, 0) || vMove < 0) {
			if (data->config.floor > monitorSpectrumMinDynamicRange) {
				if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) {
					data->config.floor -= 1;
				} else {
					data->config.floor -= 6;
				}
			}
		}
		azaTooltipAdd("Floor", controlRect.x + controlRect.w, controlRect.y, false);
	}
	azaDrawRect(controlRect, colorWindowControlRect);
	azaDrawText(TextFormat("%+ddB", (int)data->config.floor), controlRect.x + textMargin, controlRect.y + textMargin, 10, WHITE);

	azaRect spectrumRect = bounds;
	azaRectShrinkLeft(&spectrumRect, monitorSpectrumWindowControlWidth + margin*2);
	azaRectShrinkMargin(&spectrumRect, margin);
	azaRectShrinkBottom(&spectrumRect, textMargin*3 + 10);
	float baseFreq = (float)data->samplerate / (float)data->config.window;
	azaDrawRectGradientV(spectrumRect, colorMonitorSpectrumBGTop, colorMonitorSpectrumBGBot);
	if (!data->outputBuffer) return;
	azaRect bar;
	uint32_t window = AZA_MIN((uint32_t)(data->config.window >> 1), data->outputBufferCapacity-1);
#if AZA_MONITOR_SPECTRUM_DEBUG_FREQUENCY_MARKS
	float lastFreq = 1.0f;
	uint32_t lastX = 0, lastWidth = 0;
#endif
	for (uint32_t i = 0; i <= window; i++) {
		float magnitude = data->outputBuffer[i];
		// float phase = data->outputBuffer[i + data->config.window] / AZA_TAU + 0.5f;
		float magDB = aza_amp_to_dbf(magnitude);
		int yOffset = azaDBToYOffsetClamped((float)data->config.ceiling - magDB, spectrumRect.h, 0, data->config.ceiling - data->config.floor);
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
#if AZA_MONITOR_SPECTRUM_DEBUG_FREQUENCY_MARKS
		float freq = baseFreq * i;
		if (freq / lastFreq >= 2.0f) {
			azaDrawLine(bar.x, spectrumRect.y, bar.x, spectrumRect.y + spectrumRect.h + textMargin, (Color) {255,0,0,64});
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
				azaDrawText(str, x, spectrumRect.y + spectrumRect.h + margin + textMargin, 10, (Color) {255, 0, 0, 64});
				lastWidth = width;
				lastX = x;
			}
			lastFreq = freq;
		}
#endif
	}
	const uint32_t freqTicks[] = {
		10,
		20,
		30,
		40,
		50,
		75,
		100,
		150,
		200,
		250,
		300,
		400,
		500,
		600,
		700,
		800,
		900,
		1000,
		1200,
		1400,
		1600,
		1800,
		2000,
		2500,
		3000,
		3500,
		4000,
		5000,
		6000,
		7000,
		8000,
		9000,
		10000,
		12000,
		14000,
		16000,
		18000,
		20000,
		22000,
		24000,
		48000,
		96000,
	};
	int xPrev[2] = {0};
	for (uint32_t i = 0; i < sizeof(freqTicks) / sizeof(freqTicks[0]); i++) {
		uint32_t freq = freqTicks[i];
		if (freq > data->samplerate/2) break;
		float fFreq = (float)freq;
		if (fFreq < baseFreq) continue;

		int x = azaMonitorSpectrumBarXFromFreq(data, spectrumRect.w-1, fFreq);
		x += spectrumRect.x;
		const char *str;
		if (freq % 1000 == 0) {
			str = TextFormat("%dk", freq/1000);
		} else {
			str = TextFormat("%d", freq);
		}
		int width = MeasureText(str, 10);
		int textX = x - width/2;
		textX = AZA_MAX(spectrumRect.x, textX);
		textX = AZA_MIN(textX, spectrumRect.x + spectrumRect.w - width);
		int line = 0;
		int lineOffset = 0;
		if (textX - xPrev[0] >= margin) {
			line = 1;
			lineOffset = textMargin;
		} else if (textX - xPrev[1] >= margin) {
			line = 2;
			lineOffset = textMargin + 10;
		}

		azaDrawLine(x, spectrumRect.y, x, spectrumRect.y + spectrumRect.h + lineOffset, (Color) {0,0,0,128});
		if (line) {
			azaDrawText(str, textX, spectrumRect.y + spectrumRect.h + margin + lineOffset, 10, WHITE);
			xPrev[line-1] = textX + width;
		}
	}
	azaDrawDBTicks(spectrumRect, data->config.ceiling - data->config.floor, data->config.ceiling, colorMonitorSpectrumDBTick, colorMonitorSpectrumDBTickUnity);
}

static void azaDrawSelectedDSP() {
	azaMutexLock(&currentMixer->mutex);
	pluginDrawHeight = GetLogicalHeight() - (trackDrawHeight + scrollbarSize);
	azaRect bounds = {
		margin*2,
		margin*2,
		GetLogicalWidth() - margin*4,
		pluginDrawHeight - margin*4,
	};
	azaDrawRectGradientV(bounds, colorPluginSettingsTop, colorPluginSettingsBot);
	azaDrawRectLines(bounds, selectedDSP ? colorPluginBorderSelected : colorPluginBorder);
	if (!selectedDSP) goto done;

	azaRectShrinkMargin(&bounds, margin*2);
	azaDrawText(selectedDSP->name, bounds.x + textMargin, bounds.y + textMargin, 20, WHITE);
	azaRectShrinkTop(&bounds, textMargin * 2 + 20);
	// TODO: Do this properly with a callback
	if (selectedDSP->fp_process == azaLookaheadLimiterProcess) {
		azaDrawLookaheadLimiter((azaLookaheadLimiter*)selectedDSP, bounds);
	} else if (selectedDSP->fp_process == azaFilterProcess) {
		azaDrawFilter((azaFilter*)selectedDSP, bounds);
	} else if (selectedDSP->fp_process == azaLowPassFIRProcess) {
		azaDrawLowPassFIR((azaLowPassFIR*)selectedDSP, bounds);
	} else if (selectedDSP->fp_process == azaCompressorProcess) {
		azaDrawCompressor((azaCompressor*)selectedDSP, bounds);
	} else if (selectedDSP->fp_process == azaDelayProcess) {
		azaDrawDelay((azaDelay*)selectedDSP, bounds);
	} else if (selectedDSP->fp_process == azaReverbProcess) {
		azaDrawReverb((azaReverb*)selectedDSP, bounds);
	} else if (selectedDSP->fp_process == azaMonitorSpectrumProcess) {
		azaDrawMonitorSpectrum((azaMonitorSpectrum*)selectedDSP, bounds);
	}
done:
	azaMutexUnlock(&currentMixer->mutex);
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
		AZA_MIN((trackDrawWidth + margin*2) * (1 + currentMixer->tracks.count) + margin*2, 640),
		pluginDrawHeight + trackDrawHeight + scrollbarSize,
		"AzAudio Mixer"
	);
	isWindowOpen = true;
	SetExitKey(-1); // Don't let raylib use the ESC key to close
	lastClickTime = azaGetTimestamp(); // Make sure lastClickTime isn't 0 so we don't have massive deltas that overflow

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
			if (azaMousePressed(MOUSE_BUTTON_LEFT, 100)) {
				lastClickTime = azaGetTimestamp();
			}
		mousePrev = azaMousePosition();
		EndDrawing(); // EndDrawing is when input events are polled. We may want to switch to using PollInputEvents manually, but this works for now.
		azaMouseCaptureEndFrame();
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

void azaMixerGUIUnselectDSP(azaDSP *dsp) {
	// TODO: This is still hacky. Probably invert the responsibility such that DSP knows when it's selected instead. This would also allow us to have multiple selected at once, which we almost definitely want.
	if (dsp == selectedDSP) {
		selectedDSP = NULL;
	}
}

void azaMixerGUIShowError(const char *message) {
	aza_strcpy(contextMenuError, message, sizeof(contextMenuError));
	azaContextMenuOpen(AZA_CONTEXT_MENU_ERROR_REPORT);
}