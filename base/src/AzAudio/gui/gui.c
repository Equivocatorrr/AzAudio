/*
	File: gui.c
	Author: Philip Haynes
*/

#include "gui.h"

#include <ctype.h>

#include "platform.h"

#include "../math.h"
#include "../timer.h"


// Theme



azagTheme azagThemeCurrent;

static const azagTheme azagThemeDefault = {
	.margin = { 2.0f, 2.0f },
	.marginText = { 5.0f, 5.0f },
	.textScaleFontSize = { 10.0f, 20.0f },
	.colorBG                 = {  15,  25,  50, 255 },
	.colorText               = { 255, 255, 255, 255 },
	.attenuationMeterWidth   = 14.0f,
	.colorAttenuation        = {  50, 255, 150, 255 },
	.colorSwitch             = {  20,  40,  40, 255 },
	.colorSwitchHighlight    = {  30,  60,  60, 255 },
	.tooltipBasic = {
		.colorBGLeft          = {  15,  25,  35, 255 },
		.colorBGRight         = {  45,  75, 105, 255 },
		.colorBorder          = { 100, 200, 255, 255 },
		.colorShadow          = {   0,   0,   0, 128 },
		.colorText            = { 255, 255, 255, 255 },
		.colorTextShadow      = {   0,   0,   0, 255 },
	},
	.tooltipError = {
		.colorBGLeft          = {  40,   0,   0, 255 },
		.colorBGRight         = {  20,   0,   0, 255 },
		.colorBorder          = { 200,   0,   0, 255 },
		.colorShadow          = {   0,   0,   0, 128 },
		.colorText            = { 255, 255, 255, 255 },
		.colorTextShadow      = {   0,   0,   0, 255 },
	},
	.contextMenu = {
		.minWidth             = 80.0f,
		.colorBGLeft          = {   5,  10,  15, 255 },
		.colorBGRight         = {  10,  20,  25, 255 },
		.colorHighlightLeft   = {  15,  25,  35, 255 },
		.colorHighlightRight  = {  45,  75, 105, 255 },
		.colorOutline         = { 255, 255, 255, 255 },
		.colorTextHeader      = { 128, 128, 128, 255 },
		.colorTextButton      = { 255, 255, 255, 255 },
	},
	.track = {
		// To let the fader db ticks be pixel-perfect, this height can be calculated from a formula:
		// (trackMeterDBRange + trackMeterDBHeadroom) * 2 + margin.y*6 + fader.width + fxDrawHeight + spacing
		// (72 + 12) * 2 + 2*6 + 14 + 80 + 4 = 278
		.size                 = { 120.0f, 278.0f },
		.fxHeight             = 80.0f,
		.spacing              = 4.0f,
		.colorFXBGTop         = {  65,  65, 120, 255 },
		.colorFXBGBot         = {  40,  50, 110, 255 },
		.colorControlsBGTop   = {  40,  45, 100, 255 },
		.colorControlsBGBot   = {  25,  30,  70, 255 },
	},
	.dspChain = {
		.colorBorder          = { 100, 120, 150, 255 },
		.colorBorderSelected  = { 200, 150, 100, 255 },
		.colorHighlightBGTop  = { 100, 120, 150, 255 },
		.colorHighlightBGBot  = { 200, 150, 100, 255 },
		.colorBypass          = { 140,  40, 240, 255 },
		.colorError           = { 255,   0,   0, 255 },
		.colorText            = { 255, 255, 255, 255 },
	},
	.plugin = {
		.colorBGTop           = {  10,  10,  10, 192 },
		.colorBGBot           = {  10,  30,  20, 192 },
		.colorBorder          = {  10,  60,  30, 255 },
		.colorPluginName      = { 255, 255, 255, 255 },
		.colorText            = { 255, 255, 255, 255 },
	},
	.meter = {
		.channelDrawWidthPeak = 2.0f,
		.channelDrawWidthRMS  = 4.0f,
		.channelMargin        = 2.0f,
		.colorBGTop           = {  10,  25,  20, 255 },
		.colorBGBot           = {  10,  40,  30, 255 },
		.colorDBTick          = { 100, 200, 140, 192 },
		.colorDBTickUnity     = { 240, 140, 100, 192 },
		.colorPeak            = { 200, 220, 255, 255 },
		.colorPeakUnity       = { 240, 180,  80, 255 },
		.colorPeakOver        = { 255,  50,  50, 255 },
		.colorRMS             = {  50, 255, 150, 255 },
		.colorRMSOver         = { 255,   0,   0, 255 },
	},
	.fader = {
		.width                = 14.0f,
		.knobHeight           = 12.0f,
		.colorBGTop           = {  10,  20,  30, 255 },
		.colorBGBot           = {  10,  35,  60, 255 },
		.colorDBTick          = { 100, 140, 200, 192 },
		.colorDBTickUnity     = { 240, 140, 100, 192 },
		.colorBGHighlight     = { 128, 255, 192,  64 },
		.colorKnobTop         = { 200, 230, 255, 192 },
		.colorKnobBot         = {  80, 120, 240, 192 },
		.colorKnobCenterLine  = {   0,   0,   0, 128 },
		.colorMuteButton      = { 140,  40, 240, 255 },
	},
	.slider = {
		.width                = 14.0f,
		.knobHeight           = 12.0f,
		.colorBGHighlight     = { 255, 255, 255,  64 },
		.colorBGTop           = {  30,  20,  10, 255 },
		.colorBGBot           = {  60,  35,  10, 255 },
		.colorKnobTop         = { 255, 230, 200, 255 },
		.colorKnobBot         = { 200, 100,  60, 255 },
		.colorKnobCenterLine  = {   0,   0,   0, 128 },
	},
	.textbox = {
		.colorText            = { 255, 255, 255, 255 },
		.colorBGLeft          = {   0,   0,   0, 255 },
		.colorBGRight         = {  20,  10,   5, 255 },
		.colorOutline         = { 255, 255, 255, 255 },
		.colorSelection       = {  80,  80,  80, 255 },
		.colorCursor          = { 200, 200, 200, 255 },
	},
	.scrollbar = {
		.thickness            = 12.0f,
		.colorBGLo            = {  10,  20,  60, 255 },
		.colorBGHi            = {  20,  40, 100, 255 },
		.colorFGLo            = {  45,  90, 240, 255 },
		.colorFGHi            = {  30,  60, 180, 255 },
	},
};

void azagSetDefaultTheme() {
	azagThemeCurrent = azagThemeDefault;
}

void azagSetTheme(const azagTheme *theme) {
	azagThemeCurrent = *theme;
}

float azagGetFontSizeForScale(azagTextScale scale) {
	assert((uint32_t)scale < AZAG_TEXT_SCALE_ENUM_COUNT);
	float fontSize = azagThemeCurrent.textScaleFontSize[scale];
	return fontSize;
}



// Hovering Text/Tooltips



typedef struct azagTooltip {
	azaVec2 position;
	azagTooltipTheme theme;
} azagTooltip;

typedef struct azagTooltips {
	char buffer[1024];
	azagTooltip tooltips[128];
	// How many bytes have already been written to buffer
	uint32_t bufferCount;
	// How many tooltips are represented
	uint32_t count;
} azagTooltips;

static azagTooltips tooltips = {0};

static void azagTooltipsReset() {
	tooltips.bufferCount = 0;
	tooltips.count = 0;
}

void azagTooltipAddThemed(const char *text, azaVec2 position, azaVec2 anchor, azagTooltipTheme theme) {
	assert(text);
	assert(tooltips.bufferCount < sizeof(tooltips.buffer));
	assert(tooltips.count < sizeof(tooltips.tooltips) / sizeof(*tooltips.tooltips));
	uint32_t count = (uint32_t)strlen(text) + 1;
	memcpy(tooltips.buffer + tooltips.bufferCount, text, count);
	tooltips.bufferCount += count;
	azaVec2 size = azagTextSizeMargin(text, AZAG_TEXT_SCALE_TEXT);
	tooltips.tooltips[tooltips.count] = (azagTooltip) {
		.position = azaSubVec2(position, azaMulVec2(size, anchor)),
		.theme = theme,
	};
	tooltips.count++;
}

void azagTooltipAdd(const char *text, azaVec2 position, azaVec2 anchor) {
	azagTooltipAddThemed(text, position, anchor, azagThemeCurrent.tooltipBasic);
}

void azagTooltipAddError(const char *text, azaVec2 position, azaVec2 anchor) {
	azagTooltipAddThemed(text, position, anchor, azagThemeCurrent.tooltipError);
}

static void azagDrawTooltips() {
	const char *text = tooltips.buffer;
	for (uint32_t i = 0; i < tooltips.count; i++) {
		azagTooltip *tooltip = &tooltips.tooltips[i];
		azaVec2 textSize = azagTextSize(text, AZAG_TEXT_SCALE_TEXT);
		azagRect rect = {
			.xy = tooltip->position,
			.size = azaAddVec2(textSize, azaMulVec2Scalar(azagThemeCurrent.marginText, 2.0f)),
		};
		azagRectFitOnScreen(&rect);
		azagDrawRect((azagRect) {
			rect.x + 2,
			rect.y + 2,
			rect.w,
			rect.h,
		}, tooltip->theme.colorShadow);
		azagDrawRectGradientH(rect, tooltip->theme.colorBGLeft, tooltip->theme.colorBGRight);
		azagDrawRectOutline(rect, tooltip->theme.colorBorder);
		azagRectShrinkAllXY(&rect, azagThemeCurrent.marginText);
		azagDrawText(text, azaAddVec2(rect.xy, (azaVec2) { 1, 1 }), AZAG_TEXT_SCALE_TEXT, tooltip->theme.colorTextShadow);
		azagDrawText(text, rect.xy, AZAG_TEXT_SCALE_TEXT, tooltip->theme.colorText);
		text += strlen(text) + 1;
	}
	// We'll reset them here because whatever man, we already drew them
	azagTooltipsReset();
}



// Scissor Stack



typedef struct azaScissorStack {
	azagRect scissors[32];
	int count;
} azaScissorStack;
static azaScissorStack scissorStack = {0};

void azagPushScissor(azagRect rect) {
	assert(scissorStack.count < sizeof(scissorStack.scissors) / sizeof(scissorStack.scissors[0]));
	if (scissorStack.count > 0) {
		azagRect up = scissorStack.scissors[scissorStack.count-1];
		float right = rect.x + rect.w;
		float bottom = rect.y + rect.h;
		rect.x = azaMaxf(rect.x, up.x);
		rect.y = azaMaxf(rect.y, up.y);
		float upRight = up.x + up.w;
		float upBottom = up.y + up.h;
		right = azaMinf(right, upRight);
		bottom = azaMinf(bottom, upBottom);
		rect.w = right - rect.x;
		rect.h = bottom - rect.y;
	}
	azagSetScissor(rect);
	scissorStack.scissors[scissorStack.count] = rect;
	scissorStack.count++;
}

void azagPopScissor() {
	assert(scissorStack.count > 0);
	scissorStack.count--;
	if (scissorStack.count > 0) {
		azagRect up = scissorStack.scissors[scissorStack.count-1];
		azagSetScissor(up);
	} else {
		azagResetScissor();
	}
}

azagRect azagGetCurrentScissor() {
	if (scissorStack.count > 0) {
		return scissorStack.scissors[scissorStack.count-1];
	} else {
		return (azagRect) {
			.x = 0, .y = 0,
			.size = azagGetScreenSize(),
		};
	}
}



// Mouse utilities



static azagMouseDepth mouseDepth = AZAG_MOUSE_DEPTH_BASE;
static azaVec2 mousePos = {0};
static azaVec2 mousePosPrev = {0};

static void *mouseDragID = NULL;
static int64_t currentFrameTimestamp = 0;
static int64_t mouseDragTimestamp = 0;
static azaVec2 mouseDragStart = {0};

// Used for detecting double clicks
static int64_t lastClickTime = 0;


static void azagMouseCaptureStart(void *id) {
	mouseDragID = id;
	mouseDepth = AZAG_MOUSE_DEPTH_DRAG;
	mouseDragTimestamp = azaGetTimestamp();
	mouseDragStart = azagMousePosition();
}
static void azagMouseCaptureEnd() {
	mouseDragID = NULL;
	mouseDepth = AZAG_MOUSE_DEPTH_BASE;
}
// Call this in window update before drawing gui
static void azagUpdateMousePre() {
	mousePosPrev = mousePos;
	mousePos = azagMousePosition();
	// Mouse dragging
	currentFrameTimestamp = azaGetTimestamp();
}
// Call this in window update after drawing gui
static void azagUpdateMousePost() {
	// Double clicking
	if (azagMousePressed_base(AZAG_MOUSE_BUTTON_LEFT)) {
		lastClickTime = azaGetTimestamp();
	}
	// Mouse dragging
	// Handle resetting from ids disappearing (probably should never happen, but would be a really annoying bug if it did).
	if (mouseDragID != NULL && mouseDragTimestamp < currentFrameTimestamp) {
		azagMouseCaptureEnd();
	}
}

bool azagMouseInRect_base(azagRect rect) {
	return azaVec2InRect(rect, azagMousePosition());
}

bool azagMouseInScissor() {
	return azagMouseInRect_base(azagGetCurrentScissor());
}

bool azagMouseInRectDepth(azagRect rect, azagMouseDepth depth) {
	return depth >= mouseDepth && azagMouseInScissor() && azagMouseInRect_base(rect);
}

bool azagMousePressedDepthIgnoreScissor(azagMouseButton button, azagMouseDepth depth) {
	return depth >= mouseDepth && azagMousePressed_base(button);
}

bool azagMousePressedDepth(azagMouseButton button, azagMouseDepth depth) {
	return depth >= mouseDepth && azagMouseInScissor() && azagMousePressed_base(button);
}

bool azagMousePressedInRectDepthIgnoreScissor(azagMouseButton button, azagRect rect, azagMouseDepth depth) {
	return azagMousePressedDepthIgnoreScissor(button, depth) && azagMouseInRect_base(rect);
}

bool azagMousePressedInRectDepth(azagMouseButton button, azagRect rect, azagMouseDepth depth) {
	return azagMousePressedDepth(button, depth) && azagMouseInRect_base(rect);
}

bool azagMouseDownDepthIgnoreScissor(azagMouseButton button, azagMouseDepth depth) {
	return depth >= mouseDepth && azagMouseDown_base(button);
}

bool azagMouseDownDepth(azagMouseButton button, azagMouseDepth depth) {
	return depth >= mouseDepth && azagMouseInScissor() && azagMouseDown_base(button);
}

bool azagMouseReleasedDepthIgnoreScissor(azagMouseButton button, azagMouseDepth depth) {
	return depth >= mouseDepth && azagMouseReleased_base(button);
}

bool azagMouseReleasedDepth(azagMouseButton button, azagMouseDepth depth) {
	return depth >= mouseDepth && azagMouseInScissor() && azagMouseReleased_base(button);
}

bool azagDoubleClickDepth(azagMouseDepth depth) {
	if (azagMousePressedDepth(AZAG_MOUSE_BUTTON_LEFT, depth)) {
		int64_t delta = azaGetTimestamp() - lastClickTime;
		int64_t delta_ns = azaGetTimestampDeltaNanoseconds(delta);
		if (delta_ns < 250 * 1000000) return true;
	}
	return false;
}

bool azagDoubleClickDepthIgnoreScissor(azagMouseDepth depth) {
	if (azagMousePressedDepthIgnoreScissor(AZAG_MOUSE_BUTTON_LEFT, depth)) {
		int64_t delta = azaGetTimestamp() - lastClickTime;
		int64_t delta_ns = azaGetTimestampDeltaNanoseconds(delta);
		if (delta_ns < 250 * 1000000) return true;
	}
	return false;
}

bool azagCaptureMouseDelta(azagRect bounds, azaVec2 *out_delta, void *id) {
	assert(out_delta);
	assert(id);
	azaVec2 mouse = azagMousePosition();
	*out_delta = (azaVec2) {0};

	if (mouseDragID == NULL) {
		if (azagMousePressedInRect(AZAG_MOUSE_BUTTON_LEFT, bounds)) {
			azagMouseCaptureStart(id);
			return true;
		}
	} else if (mouseDragID == id) {
		// Don't check depth or scissors, because drags persist outside of their starting bounds
		if (!azagMouseDown_base(AZAG_MOUSE_BUTTON_LEFT)) {
			azagMouseCaptureEnd();
			return false;
		}
		mouseDragTimestamp = azaGetTimestamp();
		*out_delta = azaSubVec2(mouse, mouseDragStart);
		return true;
	}
	return false;
}

void azagMouseCaptureResetDelta() {
	mouseDragStart = azagMousePosition();
}

bool azagMouseCaptureJustStarted() {
	return azagMousePressedDepth(AZAG_MOUSE_BUTTON_LEFT, AZAG_MOUSE_DEPTH_DRAG);
}



static bool azagCharIsConsonant(char c) {
	switch (c) {
		case 'b': case 'B':
		case 'c': case 'C':
		case 'd': case 'D':
		case 'f': case 'F':
		case 'g': case 'G':
		case 'h': case 'H':
		case 'j': case 'J':
		case 'k': case 'K':
		case 'l': case 'L':
		case 'm': case 'M':
		case 'n': case 'N':
		case 'p': case 'P':
		case 'q': case 'Q':
		case 'r': case 'R':
		case 's': case 'S':
		case 't': case 'T':
		case 'v': case 'V':
		case 'w': case 'W':
		case 'x': case 'X':
		case 'y': case 'Y':
		case 'z': case 'Z':
			return true;
		default:
			return false;
	}
}

static bool azagCharIsVowel(char c) {
	switch (c) {
		case 'a': case 'A':
		case 'e': case 'E':
		case 'i': case 'I':
		case 'o': case 'O':
		case 'u': case 'U':
		case 'y': case 'Y':
			return true;
		default:
			return false;
	}
}

static bool azagCharIsLowercase(char c) {
	if (c >= 'a' && c <= 'z') {
		return true;
	}
	return false;
}

static bool azagCharIsUppercase(char c) {
	if (c >= 'A' && c <= 'Z') {
		return true;
	}
	return false;
}

static bool azagCharPairIsOneSound(char c1, char c2) {
	if (azagCharIsVowel(c1) && azagCharIsVowel(c2)) {
		// Might be overzealous, better safe than sorry I guess.
		return true;
	}
	c1 = tolower(c1);
	c2 = tolower(c2);
	switch (c1) {
		case 'b': return c2 == 'b' || c2 == 'r' || c2 == 'l' || c2 == 'y';
		case 'c': return c2 == 'c' || c2 == 'k' || c2 == 'h' || c2 == 'r' || c2 == 'l' || c2 == 'y';
		case 'd': return c2 == 'd' || c2 == 'r' || c2 == 'j' || c2 == 'g' || c2 == 'y';
		case 'f': return c2 == 'f' || c2 == 'r' || c2 == 'l' || c2 == 'y';
		case 'g': return c2 == 'g' || c2 == 'h' || c2 == 'r' || c2 == 'l' || c2 == 'y';
		case 'l': return c2 == 'l' || c2 == 'f' || c2 == 'y';
		case 'm': return c2 == 'm' || c2 == 'y' || c2 == 'p' || c2 == 'n';
		case 'n': return c2 == 'n' || c2 == 'x' || c2 == 'y' || c2 == 'c' || c2 == 'd' || c2 == 't' || c2 == 'k';
		case 'p': return c2 == 'p' || c2 == 'h' || c2 == 'l' || c2 == 'r' || c2 == 'y';
		case 'r': return c2 == 'r' || c2 == 'y';
		case 's': return c2 == 's' || c2 == 't' || c2 == 'l' || c2 == 'p' || c2 == 'c' || c2 == 'k' || c2 == 'y';
		case 't': return c2 == 't' || c2 == 'h' || c2 == 'r' || c2 == 'y' || c2 == 'z';
		case 'v': return c2 == 'v' || c2 == 'r' || c2 == 'y';
		case 'w': return c2 == 'w' || c2 == 'h' || c2 == 'n' || c2 == 'r' || c2 == 'y';
		case 'x': return c2 == 'x' || c2 == 'p' || c2 == 'y';
		case 'z': return c2 == 'z' || c2 == 'l';
		default: return false;
	}
}

// May have exceptions to azagCharPairIsOneSound for the purposes of allowing certain splits
static bool azagCharPairCantBeSplit(char c1, char c2) {
	if (azagCharIsUppercase(c1) && azagCharIsLowercase(c2)) {
		return true;
	}
	c1 = tolower(c1);
	c2 = tolower(c2);
	switch (c1) {
		case 'b':
			if (c2 == 'b') return false;
			break;
		case 'c':
			if (c2 == 'c') return false;
			break;
		case 'd':
		 	if (c2 == 'd') return false;
		 	break;
		case 'f':
			if (c2 == 'f') return false;
			// Include FX because it's a common acronym
			if (c2 == 'x') return true;
			break;
		case 'g':
		 	if (c2 == 'g') return false;
		 	break;
		case 'l':
		 	if (c2 == 'l') return false;
		 	break;
		case 'm':
		 	if (c2 == 'm') return false;
		 	break;
		case 'n':
		 	if (c2 == 'n') return false;
		 	break;
		case 'p':
		 	if (c2 == 'p') return false;
		 	break;
		case 'r':
		 	if (c2 == 'r') return false;
		 	break;
		case 't':
		 	if (c2 == 't') return false;
		 	break;
		default: break;
	}
	// No exceptions, just do the normal thing
	return azagCharPairIsOneSound(c1, c2);
}

#include "azaHasPrefix.gen.c"
#include "azaHasSuffix.gen.c"

// Checks for patterns around text, bounded by minRange(inclusive) and maxRange(exclusive), and returns true if a hyphen can be placed at the beginning of text
static bool azagTextCanBeHyphenated(const char *text, int minRange, int maxRange, bool *out_was_suffix) {
	*out_was_suffix = false;
	// Check suffix first, as it has special behavior that can block other matches
	if (minRange <= -2 && charIsAlpha(text[-1]) && charIsAlpha(text[-2]) && azaHasSuffix(text, minRange, maxRange)) {
		*out_was_suffix = true;
		return true;
	}
	{ // Check for lowercase into an uppercase (camelCaseWordBoundary)
		if (minRange <= -1 && maxRange >= 1) {
			if (azagCharIsLowercase(text[-1]) && azagCharIsUppercase(text[0])) {
				return true;
			}
		}
	}
	{ // Check for vowel-consonant-consonant-vowel
		bool leftVowelConsonant = false;
		if (minRange <= -3) {
			if (azagCharIsVowel(text[-3]) && azagCharIsConsonant(text[-2]) && azagCharIsConsonant(text[-1]) && azagCharPairIsOneSound(text[-2], text[-1])) {
				leftVowelConsonant = true;
			}
		}
		if (minRange <= -2) {
			if (azagCharIsVowel(text[-2]) && azagCharIsConsonant(text[-1])) {
				leftVowelConsonant = true;
			}
		}
		if (leftVowelConsonant) {
			bool rightConsonantVowel = false;
			if (maxRange >= 3) {
				if (azagCharIsConsonant(text[0]) && azagCharIsConsonant(text[1]) && azagCharIsVowel(text[2]) && azagCharPairIsOneSound(text[0], text[1])) {
					rightConsonantVowel = true;
				}
			}
			if (maxRange >= 2) {
				if (azagCharIsConsonant(text[0]) && azagCharIsVowel(text[1])) {
					rightConsonantVowel = true;
				}
			}
			if (rightConsonantVowel) {
				if (!azagCharPairCantBeSplit(text[-1], text[0])) {
					return true;
				}
			}
		}
	}
	// Make sure we don't hyphenate just 1 letter or break up certain pairs
	if (maxRange > 2 && charIsAlpha(text[0]) && charIsAlpha(text[1]) && azaHasPrefix(text, minRange, maxRange)) {
		return true;
	}
	return false;
}

size_t azagTextInsertNewlines(char *dst, size_t dstSize, const char *src, azagTextScale scale, float availableWidth, bool hyphenate) {
	size_t srcLen = strlen(src);
	size_t srcCur = 0;
	size_t dstCur = 0;
	size_t lastSpaceSrc = 0;
	size_t lastSpaceDst = 0;
	size_t lastHyphenSrc = 0;
	size_t lastHyphenDst = 0;
	bool lastWasSuffix = false;
	size_t lastHyphenatedSrc = 0;
	size_t dstSizeRemaining = dstSize-1; // Leave space for the null terminator
	float cursor = 0.0f;
	float width = 0.0f; // Because of kerning, actual text width may at times be wider than the cursor's position plus the last character's advance. This is the actual width of the text.
	while (src[srcCur] != 0 && dstSizeRemaining != 0) {
		if (src[srcCur] == '\n') {
			lastSpaceDst = 0;
			cursor = 0.0f;
			width = 0.0f;
		} else {
			azagCharacterAdvanceX advanceX = azagGetCharacterAdvanceX(src + srcCur, scale);
			if (advanceX.characterBytes > dstSizeRemaining) {
				// Stop here, the codepoint is too long to insert, so omit it completely.
				break;
			}
			if (src[srcCur] == ' ' || src[srcCur] == '\t') {
				lastSpaceSrc = srcCur;
				lastSpaceDst = dstCur;
				lastWasSuffix = false;
			} else {
				width = cursor + advanceX.width;
				if (width > availableWidth) {
					// Time to break up the line
					if (lastSpaceDst > lastHyphenDst) {
						dst[lastSpaceDst] = '\n';
						// Now go back and redo the line from the start
						srcCur = lastSpaceSrc+1;
						dstCur = lastSpaceDst+1;
						dstSizeRemaining = dstSize-1 - dstCur;
						lastSpaceDst = 0; // Don't use this space again, it's not a space any more
						lastWasSuffix = false;
						cursor = 0.0f;
						width = 0.0f;
						continue;
					}
					if (lastHyphenDst > lastSpaceDst) {
						dst[lastHyphenDst] = '-';
						dst[lastHyphenDst+1] = '\n';
						srcCur = lastHyphenSrc + (src[lastHyphenSrc] == '-' ? 1 : 0);
						lastHyphenatedSrc = srcCur;
						dstCur = lastHyphenDst+2;
						dstSizeRemaining = dstSize-1 - dstCur;
						lastHyphenDst = 0;
						lastWasSuffix = false;
						cursor = 0.0f;
						width = 0.0f;
						continue;
					}
					// Last-ditch fallback that just breaks the word in-place (probably ugly)
					dst[dstCur++] = '\n';
					dstSizeRemaining--;
					cursor = 0.0f;
					width = 0.0f;
					continue;
				}
				bool wasSuffix = false;
				if (
					(dstSizeRemaining > 1 && src[srcCur] == '-') ||
					(dstSizeRemaining > 2 && azagTextCanBeHyphenated(src + srcCur, -(int)srcCur, (int)(srcLen - srcCur), &wasSuffix))
				) {
					if (lastHyphenatedSrc != srcCur && !lastWasSuffix) {
						// Don't allow a suffix to replace a suffix, as we want the longest version of similar suffixes.
						lastHyphenSrc = srcCur;
						lastHyphenDst = dstCur;
						lastWasSuffix = lastWasSuffix || wasSuffix;
					}
				}
			}
			memcpy(dst + dstCur, src + srcCur, advanceX.characterBytes);
			srcCur += advanceX.characterBytes;
			dstCur += advanceX.characterBytes;
			dstSizeRemaining = dstSize-1 - dstCur;
			cursor += advanceX.advance;
		}
	}
	assert(dstCur < dstSize);
	dst[dstCur] = 0;
	return dstCur;
}



bool azagMouseDragFloat_id(azagRect knobRect, float *value, bool inverted, float dragRegion, bool vertical, float valueMin, float valueMax, bool doClamp, float preciseDiv, bool doPrecise, float snapInterval, bool doSnap, void *id) {
	assert(valueMax > valueMin);
	azaVec2 mouseDelta = {0};
	if (azagCaptureMouseDelta(knobRect, &mouseDelta, id)) {
		static float dragStartValue = 0.0f;
		if (azagMouseCaptureJustStarted()) {
			dragStartValue = *value;
		}
		bool precise = doPrecise && azagIsShiftDown();
		bool snap = doSnap && azagIsControlDown();
		if (doPrecise && (azagIsShiftPressed() || azagIsShiftReleased())) {
			// Transition between precise and not precise, reset the offsets and values so the knob doesn't jump.
			dragStartValue = *value;
			azagMouseCaptureResetDelta();
			mouseDelta = (azaVec2) {0};
		}
		float usefulDelta = vertical ? mouseDelta.y : mouseDelta.x;
		float valueRange = valueMax - valueMin;
		float actualDelta = usefulDelta * valueRange / dragRegion;
		if (inverted) {
			actualDelta = -actualDelta;
		}
		if (precise) {
			actualDelta /= preciseDiv;
			snapInterval /= preciseDiv;
		}
		*value = dragStartValue + actualDelta;
		if (snap) {
			*value = azaSnapf(*value, snapInterval);
		}
		if (doClamp) {
			*value = azaClampf(*value, valueMin, valueMax);
		}
		return true;
	}
	return false;
}

bool azagMouseDragFloatLog_id(azagRect knobRect, float *value, bool inverted, float dragRegion, bool vertical, float valueMin, float valueMax, bool doClamp, float preciseDiv, bool doPrecise, float snapInterval, bool doSnap, void *id) {
	assert(valueMax > valueMin);
	azaVec2 mouseDelta = {0};
	if (azagCaptureMouseDelta(knobRect, &mouseDelta, id)) {
		static float dragStartValue = 0.0f;
		float logValue = log10f(*value);
		float logMin = log10f(valueMin);
		float logMax = log10f(valueMax);
		if (azagMouseCaptureJustStarted()) {
			dragStartValue = logValue;
		}
		bool precise = doPrecise && azagIsShiftDown();
		bool snap = doSnap && azagIsControlDown();
		if (doPrecise && (azagIsShiftPressed() || azagIsShiftReleased())) {
			// Transition between precise and not precise, reset the offsets and values so the knob doesn't jump.
			dragStartValue = logValue;
			azagMouseCaptureResetDelta();
			mouseDelta = (azaVec2) {0};
		}
		float usefulDelta = vertical ? mouseDelta.y : mouseDelta.x;
		float valueRange = logMax - logMin;
		float actualDelta = usefulDelta * valueRange / dragRegion;
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
			*value = azaSnapf(*value, snapInterval * snapMagnitude);
		}
		if (doClamp) {
			*value = azaClampf(*value, valueMin, valueMax);
		}
		return true;
	}
	return false;
}

bool azagMouseDragInt64_id(azagRect knobRect, int64_t *value, bool inverted, float dragRegion, bool vertical, int64_t valueMin, int64_t valueMax, bool doClamp, int64_t preciseDiv, bool doPrecise, int64_t snapInterval, bool doSnap, void *id) {
	assert(valueMax > valueMin);
	azaVec2 mouseDelta = {0};
	if (azagCaptureMouseDelta(knobRect, &mouseDelta, id)) {
		static int64_t dragStartValue = 0;
		if (azagMouseCaptureJustStarted()) {
			dragStartValue = *value;
		}
		bool precise = doPrecise && azagIsShiftDown();
		bool snap = doSnap && azagIsControlDown();
		if (doPrecise && (azagIsShiftPressed() || azagIsShiftReleased())) {
			// Transition between precise and not precise, reset the offsets and values so the knob doesn't jump.
			dragStartValue = *value;
			azagMouseCaptureResetDelta();
			mouseDelta = (azaVec2) {0};
		}
		int64_t usefulDelta = vertical ? (int64_t)mouseDelta.y : (int64_t)mouseDelta.x;
		int64_t valueRange = valueMax - valueMin;
		int64_t actualDelta = (int64_t)round((double)(usefulDelta * valueRange) / (double)dragRegion);
		if (inverted) {
			actualDelta = -actualDelta;
		}
		if (precise) {
			actualDelta /= preciseDiv;
			snapInterval /= preciseDiv;
		}
		*value = dragStartValue + actualDelta;
		if (snap) {
			*value = azaSnapi64(*value, AZA_MAX(snapInterval, 1));
		}
		if (doClamp) {
			*value = AZA_CLAMP(*value, valueMin, valueMax);
		}
		return true;
	}
	return false;
}



// Faders, sliders



void azagDrawDBTicks(azagRect bounds, float dbRange, float dbOffset, azagColor color, azagColor colorUnity) {
	int dbOffsetInt = (int)floorf(dbOffset);
	float dbOffsetFrac = dbOffset - (float)dbOffsetInt;
	for (int i = 0; i <= dbRange; i++) {
		int db = i + dbOffsetInt;
		azagColor myColor = i == dbOffsetInt ? colorUnity : color;
		myColor.a = (int)myColor.a * (64 + (db%6==0)*128 + (db%3==0)*63) / 255;
		float yOffset = ((float)i + dbOffsetFrac) * bounds.h / dbRange;
		azagDrawLine((azaVec2) {bounds.x, bounds.y+yOffset}, (azaVec2) {bounds.x+bounds.w, bounds.y+yOffset}, myColor);
	}
}

static inline void azagDrawFaderBackground(azagRect bounds, float dbRange, float dbHeadroom) {
	azagDrawRectGradientV(bounds, azagThemeCurrent.fader.colorBGTop, azagThemeCurrent.fader.colorBGBot);
	azagRectShrinkAllV(&bounds, azagThemeCurrent.margin.y);
	azagDrawDBTicks(bounds, dbRange, dbHeadroom, azagThemeCurrent.fader.colorDBTick, azagThemeCurrent.fader.colorDBTickUnity);
}

void azagRectCutOutFaderMuteButton(azagRect *rect) {
	azagRectShrinkTop(rect, azagThemeCurrent.fader.width + azagThemeCurrent.margin.y);
}

#define FADER_GAIN_IN_TITLE 0

float azagDrawFader(azagRect bounds, float *gain, bool *mute, bool cutOutMissingMuteButton, const char *label, float dbRange, float dbHeadroom) {
	bounds.w = azagThemeCurrent.fader.width;
	bool mouseover = azagMouseInRect(bounds);
	if (mouseover && azagDoubleClick()) {
		*gain = 0.0f;
	}
	bool precise = azagIsShiftDown();
	azagRect faderBounds = bounds;
	if (mute || cutOutMissingMuteButton) {
		azagRectCutOutFaderMuteButton(&faderBounds);
	}
	azagRect sliderBounds = faderBounds;
	azagRectShrinkAllXY(&sliderBounds, azagThemeCurrent.margin);
	float yOffset = azagDBToYOffsetClamped(dbHeadroom - *gain, sliderBounds.h, 0.0f, dbRange);
	azagRect knobRect = {
		sliderBounds.x,
		sliderBounds.y + yOffset - azagThemeCurrent.fader.knobHeight/2,
		sliderBounds.w,
		azagThemeCurrent.fader.knobHeight
	};
	bool dragging = azagMouseDragFloat(
		/* knobRect: */ knobRect,
		/* value: */ gain,
		/* inverted: */ true,
		/* dragRegion: */ sliderBounds.h,
		/* vertical: */ true,
		/* valueMin: */ dbHeadroom-dbRange,
		/* valueMax: */ dbHeadroom,
		/* doClamp: */ false,
		/* preciseDiv: */ 10.0f,
		/* doPrecise: */ true,
		/* snapInterval */ 0.5f,
		/* doSnap: */ true
	);
	if (dragging) {
		yOffset = azagDBToYOffsetClamped(dbHeadroom - *gain, sliderBounds.h, 0.0f, dbRange);
		knobRect.y = sliderBounds.y + yOffset - 6.0f;
		mouseover = true;
	}
	if (dragging || azagMouseInRect(knobRect)) {
		azagSetMouseCursor(AZAG_MOUSE_CURSOR_RESIZE_V);
	}
	if (mouseover) {
		azaVec2 tooltipPosition = {
			bounds.x + bounds.w / 2,
			bounds.y - azagThemeCurrent.margin.y,
		};
#if FADER_GAIN_IN_TITLE
		azagTooltipAdd(azaTextFormat(precise ? "%s %+.2fdb" : "%s %+.1fdb", label, *gain), tooltipPosition, 0.5f, 1.0f);
#else
		azagTooltipAdd(label, tooltipPosition, (azaVec2) { 0.5f, 1.0f });
		tooltipPosition = (azaVec2) {
			bounds.x + bounds.w,
			sliderBounds.y + yOffset,
		};
		azagTooltipAdd(azaTextFormat(precise ? "%+.2fdb" : "%+.1fdb", *gain), tooltipPosition, (azaVec2) { 0.0f, 0.5f });
#endif
	}
	if (mute) {
		azagRect muteRect = bounds;
		muteRect.h = muteRect.w;
		// azagDrawRect(muteRect, azagThemeCurrent.fader.colorBGTop);
		// azagRectShrinkAllXY(&muteRect, azagThemeCurrent.margin);
		if (azagMouseInRect(muteRect)) {
			azagSetMouseCursor(AZAG_MOUSE_CURSOR_POINTING_HAND);
			azaVec2 tooltipPosition = {
				muteRect.x + muteRect.w / 2.0f,
				muteRect.y + muteRect.h + azagThemeCurrent.margin.y,
			};
			azagTooltipAdd("Mute", tooltipPosition, (azaVec2) { 0.5f, 0.0f });
			if (azagMousePressed(AZAG_MOUSE_BUTTON_LEFT)) {
				*mute = !*mute;
			}
		}
		if (*mute) {
			azagDrawRect(muteRect, azagThemeCurrent.fader.colorMuteButton);
		} else {
			azagDrawRectOutline(muteRect, azagThemeCurrent.fader.colorMuteButton);
		}
	}
	azagDrawFaderBackground(faderBounds, dbRange, dbHeadroom);
	if (mouseover) {
		azagDrawRect(sliderBounds, azagThemeCurrent.fader.colorBGHighlight);
		float delta = azagMouseWheelV();
		if (azagIsShiftDown()) delta /= 10.0f;
		*gain += delta;
	}
	azagPushScissor(faderBounds);
	azagDrawRectGradientV(knobRect, azagThemeCurrent.fader.colorKnobTop, azagThemeCurrent.fader.colorKnobBot);
	azagDrawLine((azaVec2) {sliderBounds.x, sliderBounds.y + yOffset}, (azaVec2) {sliderBounds.x + sliderBounds.w, sliderBounds.y + yOffset}, azagThemeCurrent.fader.colorKnobCenterLine);
	azagPopScissor();
	return azagThemeCurrent.fader.width;
}

#undef FADER_GAIN_IN_TITLE



float azagDrawSliderFloatLog(azagRect bounds, float *value, float min, float max, float step, float def, const char *label, const char *valueFormat) {
	assert(min > 0.0f);
	assert(max > min);
	bounds.w = azagThemeCurrent.slider.width;
	bool mouseover = azagMouseInRect(bounds);
	azagDrawRectGradientV(bounds, azagThemeCurrent.slider.colorBGTop, azagThemeCurrent.slider.colorBGBot);
	azagRect sliderBounds = bounds;
	azagRectShrinkAllXY(&sliderBounds, azagThemeCurrent.margin);
	float logValue = logf(*value);
	float logMin = logf(min);
	float logMax = logf(max);
	float yOffset = sliderBounds.h * (1.0f - (logValue - logMin) / (logMax - logMin));
	if (mouseover) {
		float delta = azagMouseWheelV();
		if (azagIsShiftDown()) {
			delta /= 10.0f;
		}
		if (delta > 0.0f) {
			*value *= (1.0f + delta*step);
		} else if (delta < 0.0f) {
			*value /= (1.0f - delta*step);
		}
		if (azagDoubleClick()) {
			*value = def;
		}
		*value = azaClampf(*value, min, max);
	}
	azagRect knobRect = {
		sliderBounds.x,
		sliderBounds.y + yOffset - azagThemeCurrent.fader.knobHeight/2.0f,
		sliderBounds.w,
		azagThemeCurrent.fader.knobHeight
	};
	bool dragging = azagMouseDragFloatLog(
		/* knobRect: */ knobRect,
		/* value: */ value,
		/* inverted: */ step >= 0.0f,
		/* dragRegion: */ sliderBounds.h,
		/* vertical: */ true,
		/* valueMin: */ min,
		/* valueMax: */ max,
		/* doClamp: */ true,
		/* preciseDiv: */ 10.0f,
		/* doPrecise: */ true,
		/* snapInterval */ azaAbsf(step),
		/* doSnap: */ true
	);
	if (dragging) {
		logValue = logf(*value);
		yOffset = sliderBounds.h * (1.0f - (logValue - logMin) / (logMax - logMin));
		knobRect.y = sliderBounds.y + yOffset - 6.0f;
		mouseover = true;
	}
	if (dragging || azagMouseInRect(knobRect)) {
		azagSetMouseCursor(AZAG_MOUSE_CURSOR_RESIZE_V);
	}
	if (mouseover) {
		azaVec2 tooltipPosition = {
			bounds.x + bounds.w / 2,
			bounds.y - azagThemeCurrent.margin.y,
		};
		azagTooltipAdd(label, tooltipPosition, (azaVec2) { 0.5f, 1.0f });
		azagDrawRect(sliderBounds, azagThemeCurrent.slider.colorBGHighlight);
		if (!valueFormat) {
			valueFormat = "%+.1f";
		}
		tooltipPosition = (azaVec2) {
			bounds.x + bounds.w,
			sliderBounds.y + yOffset,
		};
		azagTooltipAdd(azaTextFormat(valueFormat, *value), tooltipPosition, (azaVec2) { 0.0f, 0.5f });
	}
	azagPushScissor(sliderBounds);
	azagDrawRectGradientV((azagRect) {
		sliderBounds.x,
		sliderBounds.y + yOffset - 6,
		sliderBounds.w,
		12
	}, azagThemeCurrent.slider.colorKnobTop, azagThemeCurrent.slider.colorKnobBot);
	azagDrawLine((azaVec2) {sliderBounds.x, sliderBounds.y + yOffset}, (azaVec2) {sliderBounds.x + sliderBounds.w, sliderBounds.y + yOffset}, azagThemeCurrent.slider.colorKnobCenterLine);
	azagPopScissor();
	return azagThemeCurrent.slider.width;
}

float azagDrawSliderFloat(azagRect bounds, float *value, float min, float max, float step, float def, const char *label, const char *valueFormat) {
	assert(max > min);
	bounds.w = azagThemeCurrent.slider.width;
	bool mouseover = azagMouseInRect(bounds);
	azagDrawRectGradientV(bounds, azagThemeCurrent.slider.colorBGTop, azagThemeCurrent.slider.colorBGBot);
	azagRect sliderBounds = bounds;
	azagRectShrinkAllXY(&sliderBounds, azagThemeCurrent.margin);
	float yOffset = sliderBounds.h * (1.0f - (*value - min) / (max - min));
	if (mouseover) {
		float delta = azagMouseWheelV();
		if (azagIsShiftDown()) {
			delta /= 10.0f;
		}
		*value += delta*step;
		if (azagDoubleClick()) {
			*value = def;
		}
		*value = azaClampf(*value, min, max);
	}
	azagRect knobRect = {
		sliderBounds.x,
		sliderBounds.y + yOffset - azagThemeCurrent.fader.knobHeight/2,
		sliderBounds.w,
		azagThemeCurrent.fader.knobHeight
	};
	bool dragging = azagMouseDragFloat(
		/* knobRect: */ knobRect,
		/* value: */ value,
		/* inverted: */ step >= 0.0f,
		/* dragRegion: */ sliderBounds.h,
		/* vertical: */ true,
		/* valueMin: */ min,
		/* valueMax: */ max,
		/* doClamp: */ true,
		/* preciseDiv: */ 10.0f,
		/* doPrecise: */ true,
		/* snapInterval */ azaAbsf(step),
		/* doSnap: */ true
	);
	if (dragging) {
		yOffset = sliderBounds.h * (1.0f - (*value - min) / (max - min));
		knobRect.y = sliderBounds.y + yOffset - 6.0f;
		mouseover = true;
	}
	if (dragging || azagMouseInRect(knobRect)) {
		azagSetMouseCursor(AZAG_MOUSE_CURSOR_RESIZE_V);
	}
	if (mouseover) {
		azaVec2 tooltipPosition = {
			bounds.x + bounds.w / 2.0f,
			bounds.y - azagThemeCurrent.margin.y,
		};
		azagTooltipAdd(label, tooltipPosition, (azaVec2) { 0.5f, 1.0f });
		azagDrawRect(sliderBounds, azagThemeCurrent.slider.colorBGHighlight);
		if (!valueFormat) {
			valueFormat = "%+.1f";
		}
		tooltipPosition = (azaVec2) {
			bounds.x + bounds.w,
			sliderBounds.y + yOffset,
		};
		azagTooltipAdd(azaTextFormat(valueFormat, *value), tooltipPosition, (azaVec2) { 0.0f, 0.5f });
	}
	azagPushScissor(sliderBounds);
	azagDrawRectGradientV(knobRect, azagThemeCurrent.slider.colorKnobTop, azagThemeCurrent.slider.colorKnobBot);
	azagDrawLine((azaVec2) {sliderBounds.x, sliderBounds.y + yOffset}, (azaVec2) {sliderBounds.x + sliderBounds.w, sliderBounds.y + yOffset}, azagThemeCurrent.slider.colorKnobCenterLine);
	azagPopScissor();
	return azagThemeCurrent.slider.width;
}



// TextBox



static void azagTextCharInsert(char c, uint32_t index, char *text, uint32_t len, uint32_t capacity) {
	assert(index < capacity-1);
	for (uint32_t i = len; i > index; i--) {
		text[i] = text[i-1];
	}
	text[index] = c;
	text[len+1] = 0;
}

static void azagTextCharErase(uint32_t index, char *text, uint32_t len) {
	assert(index < len);
	for (uint32_t i = index; i < len; i++) {
		text[i] = text[i+1];
	}
	text[len] = 0;
}

static bool azagIsWhitespace(char c) {
	return c == ' ' || c == '\r' || c == '\n' || c == '\t';
}

static char *textboxTextBeingEdited = NULL;
static uint32_t textboxCursor = 0;
static bool textboxSelected = false;
static azagRect textboxBounds;

void azagDrawTextBox(azagRect bounds, char *text, uint32_t textCapacity) {
	bool mouseover = azagMouseInRect(bounds);
	uint32_t textLen = (uint32_t)strlen(text);
	if (textCapacity && mouseover && azagDoubleClick()) {
		textboxTextBeingEdited = text;
		textboxSelected = true;
		textboxCursor = textLen;
	}
	if (textboxTextBeingEdited == text) {
		if (azagKeyPressed(AZAG_KEY_ESC) || azagKeyPressed(AZAG_KEY_ENTER) || azagKeyPressed(AZAG_KEY_KPENTER)
		|| (!mouseover && azagMousePressedIgnoreScissor(AZAG_MOUSE_BUTTON_LEFT))) {
			textboxTextBeingEdited = NULL;
		}
	}
	if (textboxTextBeingEdited == text) {
		if (azagKeyRepeated(AZAG_KEY_LEFT)) {
			if (textboxSelected) {
				textboxCursor = 0;
				textboxSelected = false;
			} else if (textboxCursor > 0) {
				// TODO: Handle wide UTF-8 codepoints
				textboxCursor--;
				if (azagIsControlDown()) {
					while (textboxCursor > 0) {
						textboxCursor--;
						if (textboxCursor > 0 && azagIsWhitespace(text[textboxCursor-1])) break;
					}
				}
			}
		}
		if (azagKeyRepeated(AZAG_KEY_RIGHT)) {
			if (textboxSelected) {
				textboxCursor = textLen;
				textboxSelected = false;
			} else if (textboxCursor < textLen) {
				// TODO: Handle wide UTF-8 codepoints
				textboxCursor++;
				if (azagIsControlDown()) {
					while (textboxCursor < textLen) {
						textboxCursor++;
						if (azagIsWhitespace(text[textboxCursor-1])) break;
					}
				}
			}
		}
		if (azagKeyPressed(AZAG_KEY_END)) {
			textboxCursor = textLen;
			textboxSelected = false;
		}
		if (azagKeyPressed(AZAG_KEY_HOME)) {
			textboxCursor = 0;
			textboxSelected = false;
		}
		if (azagKeyRepeated(AZAG_KEY_BACKSPACE)) {
			if (textboxSelected) {
				textLen = 0;
				textboxCursor = 0;
				textboxSelected = false;
				text[0] = 0;
			} else if (textboxCursor > 0) {
				// TODO: Handle wide UTF-8 codepoints
				azagTextCharErase(textboxCursor-1, text, textLen);
				textboxCursor--;
				textLen--;
			}
		}
		if (azagKeyRepeated(AZAG_KEY_DELETE)) {
			if (textboxSelected) {
				textLen = 0;
				textboxCursor = 0;
				textboxSelected = false;
				text[0] = 0;
			} else if (textboxCursor < textLen) {
				// TODO: Handle wide UTF-8 codepoints
				azagTextCharErase(textboxCursor, text, textLen);
				textLen--;
			}
		}
		uint32_t c;
		while ((c = azagGetNextChar()) && (textLen < textCapacity-1 || textboxSelected)) {
			// TODO: Handle wide UTF-8 codepoints
			if (c < 128) {
				if (textboxSelected) {
					textLen = 0;
					textboxCursor = 0;
					textboxSelected = false;
					text[0] = 0;
				}
				char cToAdd = (char)c;
				azagTextCharInsert(cToAdd, textboxCursor, text, textLen, textCapacity);
				textboxCursor++;
				textLen++;
			}
		}
		float textWidth = azagTextWidth(text, AZAG_TEXT_SCALE_TEXT);
		if (textWidth > (bounds.w - azagThemeCurrent.marginText.x * 2)) {
			bounds.w = textWidth + azagThemeCurrent.marginText.x * 2;
			azagRectFitOnScreen(&bounds);
		}
		textboxBounds = bounds;
	} else {
		float textWidth = azagTextWidth(text, AZAG_TEXT_SCALE_TEXT);
		if (mouseover && textWidth > (bounds.w - azagThemeCurrent.marginText.x * 2)) {
			azagTooltipAdd(text, bounds.xy, (azaVec2) { 0.0f, 0.0f });
		}
		azagPushScissor(bounds);
		azagDrawText(text, azaAddVec2(bounds.xy, azagThemeCurrent.marginText), AZAG_TEXT_SCALE_TEXT, azagThemeCurrent.textbox.colorText);
		azagPopScissor();
	}
}

static void azagDrawTextboxBeingEdited() {
	if (!textboxTextBeingEdited) return;
	char *text = textboxTextBeingEdited;
	azagDrawRectGradientH(textboxBounds, azagThemeCurrent.textbox.colorBGLeft, azagThemeCurrent.textbox.colorBGRight);
	azagDrawRectOutline(textboxBounds, azagThemeCurrent.textbox.colorOutline);
	azagRectShrinkAllXY(&textboxBounds, azagThemeCurrent.marginText);
	if (textboxSelected) {
		azagRect selectionRect = textboxBounds;
		selectionRect.w = azagTextWidth(text, AZAG_TEXT_SCALE_TEXT);
		azagDrawRect(selectionRect, azagThemeCurrent.textbox.colorSelection);
	} else {
		char holdover = text[textboxCursor];
		text[textboxCursor] = 0;
		azaVec2 cursor = {
			.x = azagTextWidth(text, AZAG_TEXT_SCALE_TEXT) + textboxBounds.x + 0.5f,
			.y = textboxBounds.y,
		};
		text[textboxCursor] = holdover;
		azagDrawLine(cursor, (azaVec2) {cursor.x, cursor.y + azagGetFontSizeForScale(AZAG_TEXT_SCALE_TEXT)}, azagThemeCurrent.textbox.colorCursor);
	}
	azagDrawText(text, textboxBounds.xy, AZAG_TEXT_SCALE_TEXT, azagThemeCurrent.textbox.colorText);
}



void azagDrawScrollbarHorizontal(azagRect bounds, float *value, float min, float max, float step) {
	assert(max >= min);
	bool mouseover = azagMouseInRect(bounds);
	float scrollbarWidth = bounds.w / 4.0f;
	float useableWidth = bounds.w - scrollbarWidth;
	float mouseX = azagMousePosition().x - bounds.x;
	azagDrawRectGradientH(bounds, azagThemeCurrent.scrollbar.colorBGLo, azagThemeCurrent.scrollbar.colorBGHi);
	if (min == max) return;
	float range = azaMaxf(max - min, 1.0f);
	float offset = useableWidth * (*value - min) / range;
	if (step < 0) {
		offset = bounds.w - scrollbarWidth - offset;
	}
	if (mouseover) {
		float scroll = azagMouseWheelV();
		float click = (float)((int)azagMousePressed(AZAG_MOUSE_BUTTON_LEFT) * ((int)(mouseX >= offset + scrollbarWidth) - (int)(mouseX < offset)));
		*value += step * (scroll + click);
		*value = azaClampf(*value, min, max);
		offset = useableWidth * (*value - min) / range;
		if (step < 0) {
			offset = bounds.w - scrollbarWidth - offset;
		}
	}
	azagRect knobRect = {
		bounds.x + offset,
		bounds.y,
		scrollbarWidth,
		bounds.h,
	};
	if (azagMouseDragFloat(
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
		/* snapInterval */ fabsf(step),
		/* doSnap: */ true
	)) {
		offset = useableWidth * (*value - min) / range;
		if (step < 0) {
			offset = bounds.w - scrollbarWidth - offset;
		}
		knobRect.x = bounds.x + offset;
	}
	azagDrawRectGradientH(knobRect, azagThemeCurrent.scrollbar.colorFGLo, azagThemeCurrent.scrollbar.colorFGHi);
}




// Context Menus



struct azagContextMenuState {
	fp_azagContextMenu current;
	fp_azagContextMenu next;
	azagRect rect;
	bool newPosition;
	float targetWidth;
	const char *title;
};
static struct azagContextMenuState azagContextMenuState = {
	.current = NULL,
	.rect = {0},
	.newPosition = false,
	.targetWidth = 0.0f,
	.title = NULL,
};

void azagContextMenuOpen(fp_azagContextMenu menu) {
	assert(menu);
	azagContextMenuState.next = menu;
	mouseDepth = AZAG_MOUSE_DEPTH_CONTEXT_MENU;
	azagContextMenuState.newPosition = true;
}

void azagContextMenuOpenSub(fp_azagContextMenu menu) {
	assert(menu);
	azagContextMenuState.next = menu;
	mouseDepth = AZAG_MOUSE_DEPTH_CONTEXT_MENU;
	azagContextMenuState.newPosition = azagContextMenuState.current == NULL;
}

void azagContextMenuClose() {
	azagContextMenuState.next = NULL;
	mouseDepth = AZAG_MOUSE_DEPTH_BASE;
}


static void azagDrawContextMenu() {
	if (azagContextMenuState.newPosition) {
		azagContextMenuState.rect.xy = azagMousePosition();
		azagContextMenuState.newPosition = false;
	}
	if (azagContextMenuState.next != azagContextMenuState.current) {
		azagContextMenuState.current = azagContextMenuState.next;
		azagContextMenuState.rect.w = azagThemeCurrent.contextMenu.minWidth;
		azagContextMenuState.rect.h = 0;
		azagContextMenuState.targetWidth = 0;
	}
	if (!azagContextMenuState.current) return;
	if (azagKeyPressed(AZAG_KEY_ESC) || (azagMousePressedDepthIgnoreScissor(AZAG_MOUSE_BUTTON_LEFT, AZAG_MOUSE_DEPTH_CONTEXT_MENU) && !azagMouseInRectDepth(azagContextMenuState.rect, AZAG_MOUSE_DEPTH_CONTEXT_MENU))) {
		azagContextMenuClose();
		return;
	}
	azagContextMenuState.rect.w += (float)(int)(azagContextMenuState.targetWidth > azagContextMenuState.rect.w) * 10.0f;
	azagContextMenuState.current();
}



// Context Menu Utilities



void azagDrawContextMenuBegin(const char *title) {
	azagContextMenuState.title = title;
	azagRectFitOnScreen(&azagContextMenuState.rect);
	azagContextMenuState.rect.h = 0;
	if (azagContextMenuState.title) {
		azagContextMenuState.rect.h += azagTextHeightMargin(title, AZAG_TEXT_SCALE_TEXT);
		float targetWidth = azagTextWidthMargin(azagContextMenuState.title, AZAG_TEXT_SCALE_TEXT);
		azagContextMenuState.targetWidth = azaMaxf(azagContextMenuState.targetWidth, targetWidth);
		azagRect titleBounds = {
			azagContextMenuState.rect.x,
			azagContextMenuState.rect.y,
			azagContextMenuState.rect.w,
			azagTextHeightMargin(title, AZAG_TEXT_SCALE_TEXT),
		};
		azagDrawRectGradientH(titleBounds, azagThemeCurrent.contextMenu.colorBGLeft, azagThemeCurrent.contextMenu.colorBGRight);
		azagDrawTextMargin(azagContextMenuState.title, azagContextMenuState.rect.xy, AZAG_TEXT_SCALE_TEXT, azagThemeCurrent.contextMenu.colorTextHeader);
		azagDrawLine((azaVec2) {azagContextMenuState.rect.x, azagContextMenuState.rect.y + titleBounds.h - 1}, (azaVec2) {azagContextMenuState.rect.x + azagContextMenuState.rect.w, azagContextMenuState.rect.y + titleBounds.h - 1}, azagThemeCurrent.contextMenu.colorOutline);
	}
}

void azagDrawContextMenuEnd() {
	azagDrawRectOutline(azagContextMenuState.rect, azagThemeCurrent.contextMenu.colorOutline);
}

bool azagDrawContextMenuButton(const char *label) {
	azagRect bounds = {
		azagContextMenuState.rect.x,
		azagContextMenuState.rect.y + azagContextMenuState.rect.h,
		azagContextMenuState.rect.w,
		azagTextHeightMargin(label, AZAG_TEXT_SCALE_TEXT),
	};
	azagContextMenuState.rect.h += bounds.h;
	bool result = false;
	azagDrawRectGradientH(bounds, azagThemeCurrent.contextMenu.colorBGLeft, azagThemeCurrent.contextMenu.colorBGRight);
	if (azagMouseInRectDepth(bounds, AZAG_MOUSE_DEPTH_CONTEXT_MENU)) {
		azagDrawRectGradientH(bounds, azagThemeCurrent.contextMenu.colorHighlightLeft, azagThemeCurrent.contextMenu.colorHighlightRight);
		if (azagMousePressedDepth(AZAG_MOUSE_BUTTON_LEFT, AZAG_MOUSE_DEPTH_CONTEXT_MENU)) {
			result = true;
			azagContextMenuClose();
		}
	}
	if (label) {
		azagPushScissor(bounds);
		azagDrawText(label, azaAddVec2(bounds.xy, azagThemeCurrent.marginText), AZAG_TEXT_SCALE_TEXT, azagThemeCurrent.contextMenu.colorTextButton);
		float targetWidth = azagTextWidthMargin(label, AZAG_TEXT_SCALE_TEXT);
		azagContextMenuState.targetWidth = azaMaxf(azagContextMenuState.targetWidth, targetWidth);
		azagPopScissor();
	}
	return result;
}



// Context Menu Implementations



static char contextMenuError[256] = {0};
static int contextMenuPleaStage = 0;
static bool contextMenuPleaHate = false;

void azagContextMenuErrorPlea();

void azagContextMenuErrorReport() {
	azagDrawContextMenuBegin(contextMenuError);
	azagDrawContextMenuButton("Okay");
	azagDrawContextMenuButton("Well dang!");
	if (azagDrawContextMenuButton("Bruh fix that shit!")) {
		contextMenuPleaStage = 0;
		azagContextMenuOpenSub(azagContextMenuErrorPlea);
	}
	azagDrawContextMenuEnd();
}

void azagContextMenuErrorPlea() {
	if (contextMenuPleaHate) {
		azagDrawContextMenuBegin("Buzz off!");
		azagDrawContextMenuEnd();
		return;
	}
	switch (contextMenuPleaStage) {
		case 0:
			azagDrawContextMenuBegin("I'm sorry, I'll do better next time.");
			azagDrawContextMenuButton("Okay.");
			if (azagDrawContextMenuButton("I forgive you.")) {
				contextMenuPleaStage = 1;
				azagContextMenuOpenSub(azagContextMenuErrorPlea);
			}
			if (azagDrawContextMenuButton("That's not good enough!")) {
				contextMenuPleaStage = 2;
				azagContextMenuOpenSub(azagContextMenuErrorPlea);
			}
			break;
		case 1:
			azagDrawContextMenuBegin("That means a lot, thanks.");
			azagDrawContextMenuButton("Okay.");
			break;
		case 2:
			azagDrawContextMenuBegin("What's your problem? I'm not gonna beg for forgiveness.");
			if (azagDrawContextMenuButton("Huh?")) {
				contextMenuPleaStage = 3;
				azagContextMenuOpenSub(azagContextMenuErrorPlea);
			}
			break;
		case 3:
			azagDrawContextMenuBegin("You think you can just walk all over me?");
			if (azagDrawContextMenuButton("Yeah?")) {
				contextMenuPleaStage = 4;
				azagContextMenuOpenSub(azagContextMenuErrorPlea);
			}
			if (azagDrawContextMenuButton("Sorry, didn't realize you could talk back.")) {
				contextMenuPleaStage = 5;
				azagContextMenuOpenSub(azagContextMenuErrorPlea);
			}
			break;
		case 4:
			azagDrawContextMenuBegin("Well you can't!");
			if (azagDrawContextMenuButton("Oh can't I?")) {
				contextMenuPleaStage = 7;
				azagContextMenuOpenSub(azagContextMenuErrorPlea);
			}
			if (azagDrawContextMenuButton("I'm sorry, I won't do it again.")) {
				contextMenuPleaStage = 8;
				azagContextMenuOpenSub(azagContextMenuErrorPlea);
			}
			break;
		case 5:
			azagDrawContextMenuBegin("Shows what you know.");
			azagDrawContextMenuButton("Uh huh.");
			if (azagDrawContextMenuButton("I'm sorry, I won't do it again.")) {
				contextMenuPleaStage = 8;
				azagContextMenuOpenSub(azagContextMenuErrorPlea);
			}
			break;
		case 6:
			azagDrawContextMenuBegin("Well I'd appreciate it if you didn't do it again.");
			azagDrawContextMenuButton("Sure.");
			azagDrawContextMenuButton("No promises.");
			break;
		case 7:
			azagDrawContextMenuBegin("Nope.");
			azagDrawContextMenuButton("Okay then.");
			if (azagDrawContextMenuButton("Sure I can.")) {
				contextMenuPleaStage = 6;
				azagContextMenuOpenSub(azagContextMenuErrorPlea);
			}
			if (azagDrawContextMenuButton("I hate you.")) {
				contextMenuPleaHate = true;
				azagWindowClose();
			}
			break;
		case 8:
			azagDrawContextMenuBegin("I appreciate that.");
			azagDrawContextMenuButton("Okay, we still have work to do.");
			break;
	}
	azagDrawContextMenuEnd();
}



void azagOnGuiOpen() {
	lastClickTime = azaGetTimestamp(); // Make sure lastClickTime isn't 0 so we don't have massive deltas that overflow
}

void azagOnDrawBegin() {
	azagUpdateMousePre();
	azagSetMouseCursor(AZAG_MOUSE_CURSOR_DEFAULT);
}

void azagOnDrawEnd() {
	azagDrawTextboxBeingEdited();
	azagDrawTooltips();
	azagDrawContextMenu();
	azagUpdateMousePost();
}
