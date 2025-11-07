/*
	File: gui.c
	Author: Philip Haynes
*/

#include "gui.h"

#include "platform.h"

#include "../math.h"
#include "../timer.h"


// Theme



azagTheme azagThemeCurrent;

static const azagTheme azagThemeDefault = {
	.margin = { 2, 2 },
	.marginText = { 5, 5 },
	.textScaleFontSize = {10, 20},
	.colorBG                 = {  15,  25,  50, 255 },
	.colorText               = { 255, 255, 255, 255 },
	.attenuationMeterWidth   = 14,
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
		.minWidth             = 80,
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
		.size                 = { 120, 278 },
		.fxHeight             = 80,
		.spacing              = 4,
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
		.channelDrawWidthPeak = 2,
		.channelDrawWidthRMS  = 4,
		.channelMargin        = 2,
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
		.width                = 14,
		.knobHeight           = 12,
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
		.width                = 14,
		.knobHeight           = 12,
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
		.thickness            = 12,
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

int azagGetFontSizeForScale(azagTextScale scale) {
	assert((int)scale < AZAG_TEXT_SCALE_ENUM_COUNT);
	int fontSize = azagThemeCurrent.textScaleFontSize[scale];
	return fontSize;
}



// Hovering Text/Tooltips



typedef struct azagTooltip {
	azagPoint position;
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

void azagTooltipAddThemed(const char *text, azagPoint position, float anchorX, float anchorY, azagTooltipTheme theme) {
	assert(text);
	assert(tooltips.bufferCount < sizeof(tooltips.buffer));
	assert(tooltips.count < sizeof(tooltips.tooltips) / sizeof(*tooltips.tooltips));
	uint32_t count = (uint32_t)strlen(text) + 1;
	memcpy(tooltips.buffer + tooltips.bufferCount, text, count);
	tooltips.bufferCount += count;
	azagPoint size = azagTextSizeMargin(text, AZAG_TEXT_SCALE_TEXT);
	tooltips.tooltips[tooltips.count] = (azagTooltip) {
		.position = azagPointSub(position, (azagPoint) {
			(int)((float)size.x * anchorX),
			(int)((float)size.y * anchorY),
		}),
		.theme = theme,
	};
	tooltips.count++;
}

void azagTooltipAdd(const char *text, azagPoint position, float anchorX, float anchorY) {
	azagTooltipAddThemed(text, position, anchorX, anchorY, azagThemeCurrent.tooltipBasic);
}

void azagTooltipAddError(const char *text, azagPoint position, float anchorX, float anchorY) {
	azagTooltipAddThemed(text, position, anchorX, anchorY, azagThemeCurrent.tooltipError);
}

static void azagDrawTooltips() {
	const char *text = tooltips.buffer;
	for (uint32_t i = 0; i < tooltips.count; i++) {
		azagTooltip *tooltip = &tooltips.tooltips[i];
		azagPoint textSize = azagTextSize(text, AZAG_TEXT_SCALE_TEXT);
		azagRect rect = {
			.xy = tooltip->position,
			.size = azagPointAdd(textSize, azagPointMulScalar(azagThemeCurrent.marginText, 2)),
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
		azagDrawText(text, azagPointAdd(rect.xy, (azagPoint) { 1, 1 }), AZAG_TEXT_SCALE_TEXT, tooltip->theme.colorTextShadow);
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
static azagPoint mousePos = {0};
static azagPoint mousePosPrev = {0};

static void *mouseDragID = NULL;
static int64_t currentFrameTimestamp = 0;
static int64_t mouseDragTimestamp = 0;
static azagPoint mouseDragStart = {0};

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
	return azagPointInRect(rect, azagMousePosition());
}

bool azagMouseInScissor() {
	return azagMouseInRect_base(azagGetCurrentScissor());
}

bool azagMouseInRectDepth(azagRect rect, azagMouseDepth depth) {
	return depth >= mouseDepth && azagMouseInScissor() && azagMouseInRect_base(rect);
}

bool azagMousePressedDepth(azagMouseButton button, azagMouseDepth depth) {
	return depth >= mouseDepth && azagMouseInScissor() && azagMousePressed_base(button);
}

bool azagMousePressedInRectDepth(azagMouseButton button, azagRect rect, azagMouseDepth depth) {
	return azagMousePressedDepth(button, depth) && azagMouseInRect_base(rect);
}

bool azagMouseDownDepth(azagMouseButton button, azagMouseDepth depth) {
	return depth >= mouseDepth && azagMouseInScissor() && azagMouseDown_base(button);
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

bool azagCaptureMouseDelta(azagRect bounds, azagPoint *out_delta, void *id) {
	assert(out_delta);
	assert(id);
	azagPoint mouse = azagMousePosition();
	*out_delta = (azagPoint) {0};

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
		*out_delta = azagPointSub(mouse, mouseDragStart);
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



bool azagMouseDragFloat(azagRect knobRect, float *value, bool inverted, int dragRegion, bool vertical, float valueMin, float valueMax, bool doClamp, float preciseDiv, bool doPrecise, float snapInterval, bool doSnap) {
	assert(valueMax > valueMin);
	azagPoint mouseDelta = {0};
	// We assume our value pointer is unique, so it works as an implicit id. Even if we had 2 knobs for the same value, this would probably still be well-behaved.
	if (azagCaptureMouseDelta(knobRect, &mouseDelta, value)) {
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
			mouseDelta = (azagPoint) {0};
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
			*value = azaSnapf(*value, snapInterval);
		}
		if (doClamp) {
			*value = azaClampf(*value, valueMin, valueMax);
		}
		return true;
	}
	return false;
}

bool azagMouseDragFloatLog(azagRect knobRect, float *value, bool inverted, int dragRegion, bool vertical, float valueMin, float valueMax, bool doClamp, float preciseDiv, bool doPrecise, float snapInterval, bool doSnap) {
	assert(valueMax > valueMin);
	azagPoint mouseDelta = {0};
	// We assume our value pointer is unique, so it works as an implicit id. Even if we had 2 knobs for the same value, this would probably still be well-behaved.
	if (azagCaptureMouseDelta(knobRect, &mouseDelta, value)) {
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
			mouseDelta = (azagPoint) {0};
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
			*value = azaSnapf(*value, snapInterval * snapMagnitude);
		}
		if (doClamp) {
			*value = azaClampf(*value, valueMin, valueMax);
		}
		return true;
	}
	return false;
}

bool azagMouseDragInt(azagRect knobRect, int *value, bool inverted, int dragRegion, bool vertical, int valueMin, int valueMax, bool doClamp, int preciseDiv, bool doPrecise, int snapInterval, bool doSnap) {
	assert(valueMax > valueMin);
	azagPoint mouseDelta = {0};
	// We assume our value pointer is unique, so it works as an implicit id. Even if we had 2 knobs for the same value, this would probably still be well-behaved.
	if (azagCaptureMouseDelta(knobRect, &mouseDelta, value)) {
		static int dragStartValue = 0;
		if (azagMouseCaptureJustStarted()) {
			dragStartValue = *value;
		}
		bool precise = doPrecise && azagIsShiftDown();
		bool snap = doSnap && azagIsControlDown();
		if (doPrecise && (azagIsShiftPressed() || azagIsShiftReleased())) {
			// Transition between precise and not precise, reset the offsets and values so the knob doesn't jump.
			dragStartValue = *value;
			azagMouseCaptureResetDelta();
			mouseDelta = (azagPoint) {0};
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
			*value = azaSnapi(*value, AZA_MAX(snapInterval, 1));
		}
		if (doClamp) {
			*value = AZA_CLAMP(*value, valueMin, valueMax);
		}
		return true;
	}
	return false;
}



// Faders, sliders



void azagDrawDBTicks(azagRect bounds, int dbRange, int dbOffset, azagColor color, azagColor colorUnity) {
	for (int i = 0; i <= dbRange; i++) {
		int db = i + dbOffset;
		azagColor myColor = i == dbOffset ? colorUnity : color;
		myColor.a = (int)myColor.a * (64 + (db%6==0)*128 + (db%3==0)*63) / 255;
		int yOffset = i * bounds.h / dbRange;
		azagDrawLine((azagPoint) {bounds.x, bounds.y+yOffset}, (azagPoint) {bounds.x+bounds.w, bounds.y+yOffset}, myColor);
	}
}

static inline void azagDrawFaderBackground(azagRect bounds, int dbRange, int dbHeadroom) {
	azagDrawRectGradientV(bounds, azagThemeCurrent.fader.colorBGTop, azagThemeCurrent.fader.colorBGBot);
	azagRectShrinkAllV(&bounds, azagThemeCurrent.margin.y);
	azagDrawDBTicks(bounds, dbRange, dbHeadroom, azagThemeCurrent.fader.colorDBTick, azagThemeCurrent.fader.colorDBTickUnity);
}

void azagRectCutOutFaderMuteButton(azagRect *bounds) {
	azagRectShrinkTop(bounds, azagThemeCurrent.fader.width + azagThemeCurrent.margin.y);
}

#define FADER_GAIN_IN_TITLE 0

int azagDrawFader(azagRect bounds, float *gain, bool *mute, bool cutOutMissingMuteButton, const char *label, int dbRange, int dbHeadroom) {
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
	int yOffset = azagDBToYOffsetClamped((float)dbHeadroom - *gain, sliderBounds.h, 0, (float)dbRange);
	azagRect knobRect = {
		sliderBounds.x,
		sliderBounds.y + yOffset - azagThemeCurrent.fader.knobHeight/2,
		sliderBounds.w,
		azagThemeCurrent.fader.knobHeight
	};
	if (azagMouseDragFloat(
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
		yOffset = azagDBToYOffsetClamped((float)dbHeadroom - *gain, sliderBounds.h, 0, (float)dbRange);
		knobRect.y = sliderBounds.y + yOffset - 6;
		mouseover = true;
	}
	if (mouseover) {
		azagPoint tooltipPosition = {
			bounds.x + bounds.w / 2,
			bounds.y - azagThemeCurrent.margin.y,
		};
#if FADER_GAIN_IN_TITLE
		azagTooltipAdd(azaTextFormat(precise ? "%s %+.2fdb" : "%s %+.1fdb", label, *gain), tooltipPosition, 0.5f, 1.0f);
#else
		azagTooltipAdd(label, tooltipPosition, 0.5f, 1.0f);
		tooltipPosition = (azagPoint) {
			bounds.x + bounds.w,
			sliderBounds.y + yOffset,
		};
		azagTooltipAdd(azaTextFormat(precise ? "%+.2fdb" : "%+.1fdb", *gain), tooltipPosition, 0.0f, 0.5f);
#endif
	}
	if (mute) {
		azagRect muteRect = bounds;
		muteRect.h = muteRect.w;
		// azagDrawRect(muteRect, azagThemeCurrent.fader.colorBGTop);
		// azagRectShrinkAllXY(&muteRect, azagThemeCurrent.margin);
		if (azagMouseInRect(muteRect)) {
			azagPoint tooltipPosition = {
				muteRect.x + muteRect.w / 2,
				muteRect.y + muteRect.h + azagThemeCurrent.margin.y,
			};
			azagTooltipAdd("Mute", tooltipPosition, 0.5f, 0.0f);
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
	azagDrawLine((azagPoint) {sliderBounds.x, sliderBounds.y + yOffset}, (azagPoint) {sliderBounds.x + sliderBounds.w, sliderBounds.y + yOffset}, azagThemeCurrent.fader.colorKnobCenterLine);
	azagPopScissor();
	return azagThemeCurrent.fader.width;
}

#undef FADER_GAIN_IN_TITLE



int azagDrawSliderFloatLog(azagRect bounds, float *value, float min, float max, float step, float def, const char *label, const char *valueFormat) {
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
	int yOffset = (int)((float)sliderBounds.h * (1.0f - (logValue - logMin) / (logMax - logMin)));
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
		sliderBounds.y + yOffset - azagThemeCurrent.fader.knobHeight/2,
		sliderBounds.w,
		azagThemeCurrent.fader.knobHeight
	};
	if (azagMouseDragFloatLog(
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
		azagPoint tooltipPosition = {
			bounds.x + bounds.w / 2,
			bounds.y - azagThemeCurrent.margin.y,
		};
		azagTooltipAdd(label, tooltipPosition, 0.5f, 1.0f);
		azagDrawRect(sliderBounds, azagThemeCurrent.slider.colorBGHighlight);
		if (!valueFormat) {
			valueFormat = "%+.1f";
		}
		tooltipPosition = (azagPoint) {
			bounds.x + bounds.w,
			sliderBounds.y + yOffset,
		};
		azagTooltipAdd(azaTextFormat(valueFormat, *value), tooltipPosition, 0.0f, 0.5f);
	}
	azagPushScissor(sliderBounds);
	azagDrawRectGradientV((azagRect) {
		sliderBounds.x,
		sliderBounds.y + yOffset - 6,
		sliderBounds.w,
		12
	}, azagThemeCurrent.slider.colorKnobTop, azagThemeCurrent.slider.colorKnobBot);
	azagDrawLine((azagPoint) {sliderBounds.x, sliderBounds.y + yOffset}, (azagPoint) {sliderBounds.x + sliderBounds.w, sliderBounds.y + yOffset}, azagThemeCurrent.slider.colorKnobCenterLine);
	azagPopScissor();
	return azagThemeCurrent.slider.width;
}

int azagDrawSliderFloat(azagRect bounds, float *value, float min, float max, float step, float def, const char *label, const char *valueFormat) {
	assert(max > min);
	bounds.w = azagThemeCurrent.slider.width;
	bool mouseover = azagMouseInRect(bounds);
	azagDrawRectGradientV(bounds, azagThemeCurrent.slider.colorBGTop, azagThemeCurrent.slider.colorBGBot);
	azagRect sliderBounds = bounds;
	azagRectShrinkAllXY(&sliderBounds, azagThemeCurrent.margin);
	int yOffset = (int)((float)sliderBounds.h * (1.0f - (*value - min) / (max - min)));
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
	if (azagMouseDragFloat(
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
		azagPoint tooltipPosition = {
			bounds.x + bounds.w / 2,
			bounds.y - azagThemeCurrent.margin.y,
		};
		azagTooltipAdd(label, tooltipPosition, 0.5f, 1.0f);
		azagDrawRect(sliderBounds, azagThemeCurrent.slider.colorBGHighlight);
		if (!valueFormat) {
			valueFormat = "%+.1f";
		}
		tooltipPosition = (azagPoint) {
			bounds.x + bounds.w,
			sliderBounds.y + yOffset,
		};
		azagTooltipAdd(azaTextFormat(valueFormat, *value), tooltipPosition, 0.0f, 0.5f);
	}
	azagPushScissor(sliderBounds);
	azagDrawRectGradientV(knobRect, azagThemeCurrent.slider.colorKnobTop, azagThemeCurrent.slider.colorKnobBot);
	azagDrawLine((azagPoint) {sliderBounds.x, sliderBounds.y + yOffset}, (azagPoint) {sliderBounds.x + sliderBounds.w, sliderBounds.y + yOffset}, azagThemeCurrent.slider.colorKnobCenterLine);
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
		|| (!mouseover && azagMousePressed(AZAG_MOUSE_BUTTON_LEFT))) {
			textboxTextBeingEdited = NULL;
		}
	}
	if (textboxTextBeingEdited == text) {
		if (azagKeyRepeated(AZAG_KEY_LEFT)) {
			if (textboxSelected) {
				textboxCursor = 0;
				textboxSelected = false;
			} else if (textboxCursor > 0) {
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
				azagTextCharErase(textboxCursor, text, textLen);
				textLen--;
			}
		}
		uint32_t c;
		while ((c = azagGetNextChar()) && (textLen < textCapacity-1 || textboxSelected)) {
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
		int textWidth = azagTextWidth(text, AZAG_TEXT_SCALE_TEXT);
		if (textWidth > (bounds.w - azagThemeCurrent.marginText.x * 2)) {
			bounds.w = textWidth + azagThemeCurrent.marginText.x * 2;
			azagRectFitOnScreen(&bounds);
		}
		textboxBounds = bounds;
	} else {
		int textWidth = azagTextWidth(text, AZAG_TEXT_SCALE_TEXT);
		if (mouseover && textWidth > (bounds.w - azagThemeCurrent.marginText.x * 2)) {
			azagTooltipAdd(text, bounds.xy, 0.0f, 0.0f);
		}
		azagPushScissor(bounds);
		azagDrawText(text, azagPointAdd(bounds.xy, azagThemeCurrent.marginText), AZAG_TEXT_SCALE_TEXT, azagThemeCurrent.textbox.colorText);
		azagPopScissor();
	}
}

static void azagDrawTextboxBeingEdited() {
	if (!textboxTextBeingEdited) return;
	char *text = textboxTextBeingEdited;
	char holdover = text[textboxCursor];
	text[textboxCursor] = 0;
	int cursorX = azagTextWidth(text, AZAG_TEXT_SCALE_TEXT) + textboxBounds.x + azagThemeCurrent.marginText.x;
	text[textboxCursor] = holdover;
	int cursorY = textboxBounds.y + azagThemeCurrent.marginText.y;
	azagDrawRectGradientH(textboxBounds, azagThemeCurrent.textbox.colorBGLeft, azagThemeCurrent.textbox.colorBGRight);
	azagDrawRectOutline(textboxBounds, azagThemeCurrent.textbox.colorOutline);
	azagRectShrinkAllXY(&textboxBounds, azagThemeCurrent.marginText);
	if (textboxSelected) {
		azagRect selectionRect = textboxBounds;
		selectionRect.w = azagTextWidth(text, AZAG_TEXT_SCALE_TEXT);
		azagDrawRect(selectionRect, azagThemeCurrent.textbox.colorSelection);
	} else {
		azagDrawLine((azagPoint) {cursorX, cursorY}, (azagPoint) {cursorX, cursorY + azagGetFontSizeForScale(AZAG_TEXT_SCALE_TEXT)}, azagThemeCurrent.textbox.colorCursor);
	}
	azagDrawText(text, textboxBounds.xy, AZAG_TEXT_SCALE_TEXT, azagThemeCurrent.textbox.colorText);
}



void azagDrawScrollbarHorizontal(azagRect bounds, int *value, int min, int max, int step) {
	assert(max >= min);
	bool mouseover = azagMouseInRect(bounds);
	int scrollbarWidth = bounds.w / 4;
	int useableWidth = bounds.w - scrollbarWidth;
	int mouseX = (int)azagMousePosition().x - bounds.x;
	azagDrawRectGradientH(bounds, azagThemeCurrent.scrollbar.colorBGLo, azagThemeCurrent.scrollbar.colorBGHi);
	if (min == max) return;
	int range = AZA_MAX(max - min, 1);
	int offset = useableWidth * (*value - min) / range;
	if (step < 0) {
		offset = bounds.w - scrollbarWidth - offset;
	}
	if (mouseover) {
		int scroll = (int)azagMouseWheelV();
		int click = (int)azagMousePressed(AZAG_MOUSE_BUTTON_LEFT) * ((int)(mouseX >= offset + scrollbarWidth) - (int)(mouseX < offset));
		*value += step * (scroll + click);
		*value = AZA_CLAMP(*value, min, max);
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
	if (azagMouseDragInt(
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
	azagDrawRectGradientH(knobRect, azagThemeCurrent.scrollbar.colorFGLo, azagThemeCurrent.scrollbar.colorFGHi);
}




// Context Menus



struct azagContextMenuState {
	fp_azagContextMenu current;
	fp_azagContextMenu next;
	azagRect rect;
	int targetWidth;
	const char *title;
};
static struct azagContextMenuState azagContextMenuState = {
	.current = NULL,
	.rect = {0},
	.targetWidth = 0,
	.title = NULL,
};

void azagContextMenuOpen(fp_azagContextMenu menu) {
	assert(menu);
	azagContextMenuState.next = menu;
	mouseDepth = AZAG_MOUSE_DEPTH_CONTEXT_MENU;
}

void azagContextMenuClose() {
	azagContextMenuState.next = NULL;
	mouseDepth = AZAG_MOUSE_DEPTH_BASE;
}


static void azagDrawContextMenu() {
	if (azagContextMenuState.next != azagContextMenuState.current) {
		if (!azagContextMenuState.current) {
			azagContextMenuState.rect.xy = azagMousePosition();
		}
		azagContextMenuState.current = azagContextMenuState.next;
		azagContextMenuState.rect.w = azagThemeCurrent.contextMenu.minWidth;
		azagContextMenuState.rect.h = 0;
		azagContextMenuState.targetWidth = 0;
	}
	if (!azagContextMenuState.current) return;
	if (azagKeyPressed(AZAG_KEY_ESC) || (azagMousePressedDepth(AZAG_MOUSE_BUTTON_LEFT, AZAG_MOUSE_DEPTH_CONTEXT_MENU) && !azagMouseInRectDepth(azagContextMenuState.rect, AZAG_MOUSE_DEPTH_CONTEXT_MENU))) {
		azagContextMenuClose();
		return;
	}
	azagContextMenuState.rect.w += (int)(azagContextMenuState.targetWidth > azagContextMenuState.rect.w) * 10;
	azagContextMenuState.current();
}



// Context Menu Utilities



void azagDrawContextMenuBegin(const char *title) {
	azagContextMenuState.title = title;
	azagRectFitOnScreen(&azagContextMenuState.rect);
	azagContextMenuState.rect.h = 0;
	if (azagContextMenuState.title) {
		int targetWidth = azagTextWidthMargin(azagContextMenuState.title, AZAG_TEXT_SCALE_TEXT);
		azagContextMenuState.targetWidth = AZA_MAX(azagContextMenuState.targetWidth, targetWidth);
		azagRect titleBounds = {
			azagContextMenuState.rect.x,
			azagContextMenuState.rect.y,
			azagContextMenuState.rect.w,
			azagTextHeightMargin(title, AZAG_TEXT_SCALE_TEXT),
		};
		azagDrawRectGradientH(titleBounds, azagThemeCurrent.contextMenu.colorBGLeft, azagThemeCurrent.contextMenu.colorBGRight);
		azagDrawTextMargin(azagContextMenuState.title, azagContextMenuState.rect.xy, AZAG_TEXT_SCALE_TEXT, azagThemeCurrent.contextMenu.colorTextHeader);
		azagDrawLine((azagPoint) {azagContextMenuState.rect.x, azagContextMenuState.rect.y + titleBounds.h - 1}, (azagPoint) {azagContextMenuState.rect.x + azagContextMenuState.rect.w, azagContextMenuState.rect.y + titleBounds.h - 1}, azagThemeCurrent.contextMenu.colorOutline);
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
		azagDrawText(label, azagPointAdd(bounds.xy, azagThemeCurrent.marginText), AZAG_TEXT_SCALE_TEXT, azagThemeCurrent.contextMenu.colorTextButton);
		int targetWidth = azagTextWidthMargin(label, AZAG_TEXT_SCALE_TEXT);
		azagContextMenuState.targetWidth = AZA_MAX(azagContextMenuState.targetWidth, targetWidth);
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
		azagContextMenuOpen(azagContextMenuErrorPlea);
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
				azagContextMenuOpen(azagContextMenuErrorPlea);
			}
			if (azagDrawContextMenuButton("That's not good enough!")) {
				contextMenuPleaStage = 2;
				azagContextMenuOpen(azagContextMenuErrorPlea);
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
				azagContextMenuOpen(azagContextMenuErrorPlea);
			}
			break;
		case 3:
			azagDrawContextMenuBegin("You think you can just walk all over me?");
			if (azagDrawContextMenuButton("Yeah?")) {
				contextMenuPleaStage = 4;
				azagContextMenuOpen(azagContextMenuErrorPlea);
			}
			if (azagDrawContextMenuButton("Sorry, didn't realize you could talk back.")) {
				contextMenuPleaStage = 5;
				azagContextMenuOpen(azagContextMenuErrorPlea);
			}
			break;
		case 4:
			azagDrawContextMenuBegin("Well you can't!");
			if (azagDrawContextMenuButton("Oh can't I?")) {
				contextMenuPleaStage = 7;
				azagContextMenuOpen(azagContextMenuErrorPlea);
			}
			if (azagDrawContextMenuButton("I'm sorry, I won't do it again.")) {
				contextMenuPleaStage = 8;
				azagContextMenuOpen(azagContextMenuErrorPlea);
			}
			break;
		case 5:
			azagDrawContextMenuBegin("Shows what you know.");
			azagDrawContextMenuButton("Uh huh.");
			if (azagDrawContextMenuButton("I'm sorry, I won't do it again.")) {
				contextMenuPleaStage = 8;
				azagContextMenuOpen(azagContextMenuErrorPlea);
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
				azagContextMenuOpen(azagContextMenuErrorPlea);
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
}

void azagOnDrawEnd() {
	azagDrawTextboxBeingEdited();
	azagDrawTooltips();
	azagDrawContextMenu();
	azagUpdateMousePost();
}
