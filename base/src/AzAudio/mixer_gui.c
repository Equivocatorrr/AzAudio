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




static azaMixer *currentMixer = NULL;
static azaDSP *selectedDSP = NULL;
static bool isWindowOpen = false;
static bool isWindowTopmost = false;
static azaThread thread = {};

static int64_t lastClickTime = 0;

static int scrollX = 0;
static int trackFXScrollY = 0;
static bool trackFXCanScrollDown = false;




// Constants for size, color, etc.




static const int pluginDrawHeight = 200;
static const int trackDrawWidth = 110;
static const int trackDrawHeight = 300;
static const int trackFXDrawHeight = 60;
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

static const int lookaheadLimiterMeterDBRange = 48;
static const int lookaheadLimiterAttenuationMeterDBRange = 18;
static const Color colorLookaheadLimiterAttenuation = {   0, 128, 255, 255 };




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
int mouseDepth = 0;

static Vector2 azaMousePosition() {
	Vector2 mouse = GetMousePosition();
	Vector2 dpi = GetWindowScaleDPI();
	mouse.x /= dpi.x;
	mouse.y /= dpi.y;
	return mouse;
}

static bool azaMouseInRect(azaRect rect) {
	Vector2 mouse = azaMousePosition();
	int mx = (int)mouse.x;
	int my = (int)mouse.y;
	return (mx >= rect.x && my >= rect.y && mx <= rect.x+rect.w && my <= rect.y+rect.h);
}

static bool azaMouseButtonPressed(int button, int depth) {
	return (depth >= mouseDepth && IsMouseButtonPressed(button));
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
		DrawRect((azaRect) {
			rect.x + 2,
			rect.y + 2,
			rect.w,
			rect.h,
		}, Fade(BLACK, 0.5f));
		if (tooltips.isError[i]) {
			DrawRectGradientH(rect, colorTooltipErrorBGLeft, colorTooltipErrorBGRight);
			DrawRectLines(rect, colorTooltipErrorBorder);
		} else {
			DrawRectGradientH(rect, colorTooltipBGLeft, colorTooltipBGRight);
			DrawRectLines(rect, colorTooltipBorder);
		}
		DrawText(text, rect.x + textMargin + 1, rect.y + textMargin + 1, 10, BLACK);
		DrawText(text, rect.x + textMargin, rect.y + textMargin, 10, WHITE);
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
		myColor.a = 64 + (db%6==0)*128 + (db%3==0)*63;
		int yOffset = i * bounds.h / dbRange;
		DrawLine(bounds.x, bounds.y+yOffset, bounds.x+bounds.w, bounds.y+yOffset, myColor);
	}
}

static inline void azaDrawMeterBackground(azaRect bounds, int dbRange, int dbHeadroom) {
	DrawRectGradientV(bounds, colorMeterBGTop, colorMeterBGBot);
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
		DrawRect(muteRect, colorMeterBGTop);
		azaRectShrinkMargin(&muteRect, margin);
		if (azaMousePressedInRect(MOUSE_BUTTON_LEFT, 0, muteRect)) {
			*mute = !*mute;
		}
		if (*mute) {
			DrawRectangle(muteRect.x, muteRect.y, muteRect.w, muteRect.h, colorFaderMuteButton);
		} else {
			DrawRectangleLines(muteRect.x, muteRect.y, muteRect.w, muteRect.h, colorFaderMuteButton);
		}
		azaRectShrinkTop(&bounds, muteRect.h + margin);
	}
	azaDrawMeterBackground(bounds, dbRange, faderDBHeadroom);
	azaRectShrinkMargin(&bounds, margin);
	int yOffset = azaDBToYOffsetClamped((float)dbHeadroom - *gain, bounds.h, 0, dbRange);
	if (mouseover) {
		DrawRect(bounds, colorFaderHighlight);
		azaTooltipAdd(TextFormat("%+.1fdb", *gain), bounds.x + bounds.w + margin, bounds.y + yOffset - (textMargin + 5), false);
		float delta = GetMouseWheelMoveV().y;
		if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) delta /= 10.0f;
		*gain += delta;
		if (azaDidDoubleClick(0)) {
			*gain = 0.0f;
		}
	}
	PushScissor(bounds);
	DrawRectangleGradientV(bounds.x, bounds.y + yOffset - 6, bounds.w, 12, colorFaderKnobTop, colorFaderKnobBot);
	DrawLine(bounds.x, bounds.y + yOffset, bounds.x + bounds.w, bounds.y + yOffset, Fade(BLACK, 0.5));
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
		DrawLine(bounds.x, bounds.y + yOffset, bounds.x+bounds.w, bounds.y + yOffset, peakColor);
		if (true /* meters->processed */) {
			float rmsDB = aza_amp_to_dbf(sqrtf(meters->rmsSquaredAvg[c]));
			float peakShortTermDB = aza_amp_to_dbf(meters->peaksShortTerm[c]);
			yOffset = azaDBToYOffsetClamped(-peakShortTermDB, bounds.h, 0, dbRange);
			DrawRectangle(bounds.x+bounds.w/4, bounds.y + yOffset, bounds.w/2, bounds.h - yOffset, colorMeterPeak);

			yOffset = azaDBToYOffsetClamped(-rmsDB, bounds.h, 0, dbRange);
			DrawRectangle(bounds.x, bounds.y + yOffset, bounds.w, bounds.h - yOffset, colorMeterRMS);
			if (rmsDB > 0.0f) {
				yOffset = azaDBToYOffsetClamped(rmsDB, bounds.h, 0, dbRange);
				DrawRectangle(bounds.x, bounds.y, bounds.w, yOffset, colorMeterRMSOver);
			}
			if (peakShortTermDB > 0.0f) {
				yOffset = azaDBToYOffsetClamped(peakShortTermDB, bounds.h, 0, dbRange);
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

// Logarithmic slider allowing values between min and max.
// Scrolling up multiplies the value by (1.0f + step) and scrolling down divides it by the same value.
// Double clicking will set value to def.
// valueUnit will be appended to the end of the string showing the value.
// returns used width
static int azaDrawSliderFloatLog(azaRect bounds, float *value, float min, float max, float step, float def, const char *label, const char *valueUnit) {
	assert(min > 0.0f);
	assert(max > min);
	bounds.w = sliderDrawWidth;
	bool mouseover = azaMouseInRect(bounds);
	if (mouseover) {
		azaTooltipAdd(label, bounds.x, bounds.y - (10 * TextCountLines(label) + textMargin*2), false);
	}
	DrawRectGradientV(bounds, colorSliderBGTop, colorSliderBGBot);
	azaRectShrinkMargin(&bounds, margin);
	float logValue = logf(*value);
	float logMin = logf(min);
	float logMax = logf(max);
	int yOffset = (int)((float)bounds.h * (1.0f - (logValue - logMin) / (logMax - logMin)));
	if (mouseover) {
		DrawRect(bounds, colorFaderHighlight);
		if (!valueUnit) {
			valueUnit = "";
		}
		azaTooltipAdd(TextFormat("%+.1f%s", *value, valueUnit), bounds.x + bounds.w + margin, bounds.y + yOffset - (textMargin + 5), false);
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
	DrawRectangleGradientV(bounds.x, bounds.y + yOffset - 6, bounds.w, 12, colorFaderKnobTop, colorFaderKnobBot);
	DrawLine(bounds.x, bounds.y + yOffset, bounds.x + bounds.w, bounds.y + yOffset, Fade(BLACK, 0.5));
	PopScissor();
	return sliderDrawWidth;
}

// Linear slider allowing values between min and max.
// Scrolling up adds step and scrolling down subtracts step.
// Double clicking will set value to def.
// valueUnit will be appended to the end of the string showing the value.
// returns used width
static int azaDrawSliderFloat(azaRect bounds, float *value, float min, float max, float step, float def, const char *label, const char *valueUnit) {
	assert(max > min);
	bounds.w = sliderDrawWidth;
	bool mouseover = azaMouseInRect(bounds);
	if (mouseover) {
		azaTooltipAdd(label, bounds.x, bounds.y - (10 * TextCountLines(label) + textMargin*2), false);
	}
	DrawRectGradientV(bounds, colorSliderBGTop, colorSliderBGBot);
	azaRectShrinkMargin(&bounds, margin);
	int yOffset = (int)((float)bounds.h * (1.0f - (*value - min) / (max - min)));
	if (mouseover) {
		DrawRect(bounds, colorFaderHighlight);
		if (!valueUnit) {
			valueUnit = "";
		}
		azaTooltipAdd(TextFormat("%+.1f%s", *value, valueUnit), bounds.x + bounds.w + margin, bounds.y + yOffset - (textMargin + 5), false);
		float delta = GetMouseWheelMoveV().y;
		if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) delta /= 10.0f;
		*value += delta*step;
		if (azaDidDoubleClick(0)) {
			*value = def;
		}
		*value = azaClampf(*value, min, max);
	}
	PushScissor(bounds);
	DrawRectangleGradientV(bounds.x, bounds.y + yOffset - 6, bounds.w, 12, colorFaderKnobTop, colorFaderKnobBot);
	DrawLine(bounds.x, bounds.y + yOffset, bounds.x + bounds.w, bounds.y + yOffset, Fade(BLACK, 0.5));
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
		DrawText(text, bounds.x + textMargin, bounds.y + textMargin, 10, WHITE);
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
	DrawRect(textboxBounds, BLACK);
	DrawRectLines(textboxBounds, WHITE);
	azaRectShrinkMargin(&textboxBounds, textMargin);
	if (textboxSelected) {
		azaRect selectionRect = textboxBounds;
		selectionRect.w = MeasureText(text, 10);
		DrawRect(selectionRect, DARKGRAY);
	} else {
		DrawLine(cursorX, cursorY, cursorX, cursorY + 10, LIGHTGRAY);
	}
	DrawText(text, textboxBounds.x, textboxBounds.y, 10, WHITE);
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
}

static void azaContextMenuTrackRemove() {
	int toRemove = contextMenuTrackIndex-1;
	if (toRemove >= 0) {
		AZA_LOG_TRACE("Track Remove at index %d!\n", toRemove);
		azaMixerRemoveTrack(currentMixer, toRemove);
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
	Vector2 mouse = azaMousePosition();
	contextMenuRect.x = (int)mouse.x;
	contextMenuRect.y = (int)mouse.y;
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
		DrawRectGradientH(bounds, colorTooltipBGLeft, colorTooltipBGRight);
		if (azaMouseButtonPressed(MOUSE_BUTTON_LEFT, 1)) {
			result = true;
		}
	}
	if (label) {
		DrawText(label, bounds.x + textMargin, bounds.y + textMargin, 10, WHITE);
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
			DrawRect(contextMenuRect, BLACK);
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
			DrawRect(contextMenuRect, BLACK);
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
			DrawRect(contextMenuRect, BLACK);
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
			DrawRect(contextMenuRect, BLACK);
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
			DrawRect(contextMenuRect, BLACK);
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
	DrawRectLines(contextMenuRect, WHITE);
	PopScissor();
}




// Tracks




static void azaDrawTrackFX(azaTrack *track, azaRect bounds) {
	DrawRectGradientV(bounds, colorTrackFXTop, colorTrackFXBot);
	azaRectShrinkMargin(&bounds, margin);
	PushScissor(bounds);
	azaRect pluginRect = bounds;
	pluginRect.y += trackFXScrollY;
	pluginRect.h = 10 + margin * 2;
	azaDSP *mouseoverDSP = NULL;
	azaDSP *dsp = track->dsp;
	while (dsp) {
		bool mouseover = azaMouseInRect(pluginRect);
		if (mouseover) {
			DrawRectGradientV(pluginRect, colorPluginBorderSelected, colorPluginBorder);
			if (azaMouseButtonPressed(MOUSE_BUTTON_LEFT, 0)) {
				selectedDSP = dsp;
			}
			mouseoverDSP = dsp;
		}
		DrawRectLines(pluginRect, dsp == selectedDSP ? colorPluginBorderSelected : colorPluginBorder);
		DrawText(azaGetDSPName(dsp), pluginRect.x + margin, pluginRect.y + margin, 10, WHITE);
		dsp = dsp->pNext;
		pluginRect.y += pluginRect.h + margin;
	}
	if (pluginRect.y + pluginRect.h > bounds.y + bounds.h) trackFXCanScrollDown = true;
	PopScissor();
	if (azaMousePressedInRect(MOUSE_BUTTON_RIGHT, 1, bounds)) {
		azaContextMenuSetIndexFromTrack(track);
		contextMenuTrackFXDSP = mouseoverDSP;
		azaContextMenuOpen(AZA_CONTEXT_MENU_TRACK_FX);
	}
}

static void azaDrawTrackControls(azaTrack *track, azaRect bounds) {
	if (azaMousePressedInRect(MOUSE_BUTTON_RIGHT, 1, bounds)) {
		azaContextMenuSetIndexFromTrack(track);
		azaContextMenuOpen(AZA_CONTEXT_MENU_TRACK);
	}
	DrawRectangleGradientV(bounds.x, bounds.y, bounds.w, bounds.h, colorTrackControlsTop, colorTrackControlsBot);
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

static void azaDrawTrack(azaTrack *track, azaRect bounds) {
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
	azaDrawTrackFX(track, (azaRect) { bounds.x, bounds.y, bounds.w, trackFXDrawHeight });
	azaRectShrinkTop(&bounds, trackFXDrawHeight + margin*2);
	azaDrawTrackControls(track, bounds);
	PopScissor();
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
	azaDrawTrack(&mixer->master, trackRect);
	for (uint32_t i = 0; i < mixer->tracks.count; i++) {
		trackRect.x += trackDrawWidth;
		azaDrawTrack(mixer->tracks.data[i], trackRect);
	}
	azaTooltipAdd(TextFormat("CPU: %.2f%%", mixer->cpuPercentSlow), GetLogicalWidth(), 0, false);
	if (mixer->hasCircularRouting) {
		azaTooltipAdd("Circular Routing Detected!!!", 0, pluginDrawHeight - (textMargin*2 + 10), true);
	}
	azaMutexUnlock(&mixer->mutex);
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
	DrawRectangle(attenuationRect.x, attenuationRect.y, attenuationRect.w, yOffset, colorLookaheadLimiterAttenuation);
	float attenuationPeakDB = aza_amp_to_dbf(data->minAmp);
	yOffset = azaDBToYOffsetClamped(-attenuationPeakDB, attenuationRect.h, 0, lookaheadLimiterAttenuationMeterDBRange);
	if (attenuationMouseover) {
		azaTooltipAdd(TextFormat("%+.1fdb", attenuationPeakDB), attenuationRect.x + attenuationRect.w + margin, attenuationRect.y + yOffset - (textMargin + 5), false);
	}
	DrawLine(attenuationRect.x, attenuationRect.y + yOffset, attenuationRect.x + attenuationRect.w, attenuationRect.y + yOffset, colorLookaheadLimiterAttenuation);
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
	azaRect rectKind = bounds;
	rectKind.w = 80;
	rectKind.h = textMargin*2 + 10;
	for (int i = 0; i < AZA_FILTER_KIND_COUNT; i++) {
		if (azaMousePressedInRect(MOUSE_BUTTON_LEFT, 0, rectKind)) {
			data->config.kind = (azaFilterKind)i;
		}
		bool selected = ((int)data->config.kind == i);
		DrawRect(rectKind, colorMeterBGBot);
		if (selected) {
			DrawRectLines(rectKind, colorPluginBorderSelected);
		}
		DrawText(azaFilterKindString[i], rectKind.x + textMargin, rectKind.y + textMargin, 10, WHITE);
		rectKind.y += rectKind.h + margin;
	}
	azaRectShrinkTop(&rectKind, (rectKind.h + margin) * AZA_FILTER_KIND_COUNT);
	azaRectShrinkLeft(&bounds, rectKind.w + margin);
	int usedWidth = azaDrawSliderFloatLog(bounds, &data->config.frequency, 5.0f, 24000.0f, 0.1f, 500.0f, "Cutoff Frequency", "Hz");
	azaRectShrinkLeft(&bounds, usedWidth + margin);
	usedWidth = azaDrawSliderFloat(bounds, &data->config.dryMix, 0.0f, 1.0f, 0.1f, 0.0f, "Dry Mix", NULL);
	azaRectShrinkLeft(&bounds, usedWidth + margin);
}

static void azaDrawCompressor(azaCompressor *data, azaRect bounds) {

}

static void azaDrawDelay(azaDelay *data, azaRect bounds) {

}

static void azaDrawReverb(azaReverb *data, azaRect bounds) {
	int usedWidth = azaDrawFader(bounds, &data->config.gain, &data->config.muteWet, "Wet Gain", 36, 6);
	azaRectShrinkLeft(&bounds, usedWidth + margin);
	usedWidth = azaDrawFader(bounds, &data->config.gainDry, &data->config.muteDry, "Dry Gain", 36, 6);
	azaRectShrinkLeft(&bounds, usedWidth + margin);
	usedWidth = azaDrawSliderFloat(bounds, &data->config.roomsize, 1.0f, 100.0f, 1.0f, 10.0f, "Room Size", NULL);
	azaRectShrinkLeft(&bounds, usedWidth + margin);
	usedWidth = azaDrawSliderFloat(bounds, &data->config.color, 1.0f, 5.0f, 0.25f, 2.0f, "Color", NULL);
	azaRectShrinkLeft(&bounds, usedWidth + margin);
	usedWidth = azaDrawSliderFloat(bounds, &data->config.delay, 0.0f, 500.0f, 1.0f, 10.0f, "Early Delay", "ms");
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
	DrawText(azaGetDSPName(selectedDSP), bounds.x + textMargin, bounds.y + textMargin, 20, WHITE);
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
		AZA_MIN(trackDrawWidth * (1 + currentMixer->tracks.count) + margin*2, 640),
		pluginDrawHeight + trackDrawHeight,
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
			azaDrawTextboxBeingEdited();
			azaDrawContextMenu();
			azaDrawTooltips();
			if (azaMouseButtonPressed(MOUSE_BUTTON_LEFT, 100)) {
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