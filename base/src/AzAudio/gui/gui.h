/*
	File: gui.h
	Author: Philip Haynes
	Exported GUI rendering and input functions for making plugin GUIs.
	Gui stuff will be prepended with "azag" instead of the usual "aza"
*/

#ifndef AZAUDIO_GUI_H
#define AZAUDIO_GUI_H

#include "types.h"
#include "platform.h"
#include "../math.h"

#ifdef __cplusplus
extern "C" {
#endif



// Hovering Text/Tooltips



typedef struct azagTooltipTheme {
	azagColor colorBGLeft;
	azagColor colorBGRight;
	azagColor colorBorder;
	azagColor colorShadow;
	azagColor colorText;
	azagColor colorTextShadow;
} azagTooltipTheme;
/*
	Adds a basic tooltip which will be drawn on top of all other UI elements.
	- Uses the tooltipBasic theme from the current UI theme.
	position is the screen position to draw the tooltip
	anchorX and anchorY describe the position on the tooltip (in the range 0 to 1) that corresponds to the given screen position.
	- an anchorX of 0.0f means the left side
	- an anchorX of 0.5f means the middle
	- an anchorX of 1.0f means the right side
	- an anchorY of 0.0f means the top
	- an anchorY of 0.5f means the middle
	- an anchorY of 1.0f means the bottom
	The text will be copied out, so it's OK to use temporary buffers.
*/
void azagTooltipAdd(const char *text, azagPoint position, float anchorX, float anchorY);
/*
	Adds an error tooltip.
	- Uses the tooltipError theme from the current UI theme.
*/
void azagTooltipAddError(const char *text, azagPoint position, float anchorX, float anchorY);
/*
	Adds a generic tooltip.
	- Uses the provided tooltip theme.
*/
void azagTooltipAddThemed(const char *text, azagPoint position, float anchorX, float anchorY, azagTooltipTheme theme);



// Scissor Stack



void azagPushScissor(azagRect rect);
void azagPopScissor();
azagRect azagGetCurrentScissor();



// Mouse utilities



bool azagMouseInRect_base(azagRect rect);

bool azagMouseInScissor();

bool azagMouseInRectDepth(azagRect rect, azagMouseDepth depth);
static inline bool azagMouseInRect(azagRect rect) {
	return azagMouseInRectDepth(rect, AZAG_MOUSE_DEPTH_BASE);
}

bool azagMousePressedDepth(azagMouseButton button, azagMouseDepth depth);
static inline bool azagMousePressed(azagMouseButton button) {
	return azagMousePressedDepth(button, AZAG_MOUSE_DEPTH_BASE);
}
bool azagMousePressedInRectDepth(azagMouseButton button, azagRect rect, azagMouseDepth depth);
static inline bool azagMousePressedInRect(azagMouseButton button, azagRect rect) {
	return azagMousePressedInRectDepth(button, rect, AZAG_MOUSE_DEPTH_BASE);
}

bool azagMouseDownDepth(azagMouseButton button, azagMouseDepth depth);
static inline bool azagMouseDown(azagMouseButton button) {
	return azagMouseDownDepth(button, AZAG_MOUSE_DEPTH_BASE);
}

bool azagMouseReleasedDepth(azagMouseButton button, azagMouseDepth depth);
static inline bool azagMouseReleased(azagMouseButton button) {
	return azagMouseReleasedDepth(button, AZAG_MOUSE_DEPTH_BASE);
}

bool azagDoubleClickDepth(azagMouseDepth depth);
static inline bool azagDoubleClick() {
	return azagDoubleClickDepth(AZAG_MOUSE_DEPTH_BASE);
}

/*
	id is a unique pointer identifier for continuity.
	- id only needs to be unique among calls to this function.
	- A const char* can work just fine, or if you're using this to change a value, a pointer to that value can work too.
	out_delta will be set relative to the drag start position unless you call azaMouseCaptureResetDelta().
	if we're capturing the mouse, returns true, else returns false and zeroes out_delta.
*/
bool azagCaptureMouseDelta(azagRect bounds, azagPoint *out_delta, void *id);

// Resets the mouse drag origin to the current mouse position. Useful for handling the switch to and from precise dragging modes.
void azagMouseCaptureResetDelta();

// Meant for use when azaCaptureMouseDelta returns true to know when a drag was just initiated.
bool azagMouseCaptureJustStarted();



// Keyboard utilities



static inline bool azagIsControlPressed() {
	return azagKeyPressed(AZAG_KEY_LEFTCTRL) || azagKeyPressed(AZAG_KEY_RIGHTCTRL);
}
static inline bool azagIsShiftPressed() {
	return azagKeyPressed(AZAG_KEY_LEFTSHIFT) || azagKeyPressed(AZAG_KEY_RIGHTSHIFT);
}
static inline bool azagIsAltPressed() {
	return azagKeyPressed(AZAG_KEY_LEFTALT) || azagKeyPressed(AZAG_KEY_RIGHTALT);
}
static inline bool azagIsControlDown() {
	return azagKeyDown(AZAG_KEY_LEFTCTRL) || azagKeyDown(AZAG_KEY_RIGHTCTRL);
}
static inline bool azagIsShiftDown() {
	return azagKeyDown(AZAG_KEY_LEFTSHIFT) || azagKeyDown(AZAG_KEY_RIGHTSHIFT);
}
static inline bool azagIsAltDown() {
	return azagKeyDown(AZAG_KEY_LEFTALT) || azagKeyDown(AZAG_KEY_RIGHTALT);
}
static inline bool azagIsControlReleased() {
	return azagKeyReleased(AZAG_KEY_LEFTCTRL) || azagKeyReleased(AZAG_KEY_RIGHTCTRL);
}
static inline bool azagIsShiftReleased() {
	return azagKeyReleased(AZAG_KEY_LEFTSHIFT) || azagKeyReleased(AZAG_KEY_RIGHTSHIFT);
}
static inline bool azagIsAltReleased() {
	return azagKeyReleased(AZAG_KEY_LEFTALT) || azagKeyReleased(AZAG_KEY_RIGHTALT);
}



// Context Menus



typedef void (*fp_azagContextMenu)();

void azagContextMenuOpen(fp_azagContextMenu menu);
void azagContextMenuClose();
// Helper functions for implementing context menus
void azagDrawContextMenuBegin(const char *title);
void azagDrawContextMenuEnd();
// Returns whether the button was pressed
bool azagDrawContextMenuButton(const char *label);

// Displays an error message on screen
void azagContextMenuErrorReport();



// Theme



typedef struct azagTheme {
	// General margins for most UI element spacing
	azagPoint margin;
	// Margin around text
	azagPoint marginText;
	int textScaleFontSize[AZAG_TEXT_SCALE_ENUM_COUNT];
	// Some generic values for plugin implementations to use
	azagColor colorBG;
	azagColor colorText;
	int attenuationMeterWidth;
	azagColor colorAttenuation;
	azagColor colorSwitch;
	azagColor colorSwitchHighlight;

	azagTooltipTheme tooltipBasic;
	azagTooltipTheme tooltipError;
	struct {
		int minWidth;
		azagColor colorBGLeft;
		azagColor colorBGRight;
		azagColor colorHighlightLeft;
		azagColor colorHighlightRight;
		azagColor colorOutline;
		azagColor colorTextHeader;
		azagColor colorTextButton;
	} contextMenu;
	struct {
		azagPoint size;
		int fxHeight;
		int spacing; // Spacing between tracks
		azagColor colorFXBGTop;
		azagColor colorFXBGBot;
		azagColor colorControlsBGTop;
		azagColor colorControlsBGBot;
	} track;
	struct {
		azagColor colorBorder;
		azagColor colorBorderSelected;
		azagColor colorHighlightBGTop;
		azagColor colorHighlightBGBot;
		azagColor colorBypass;
		azagColor colorError;
		azagColor colorText;
	} dspChain;
	struct {
		azagColor colorBGTop;
		azagColor colorBGBot;
		azagColor colorBorder;
		azagColor colorPluginName;
		azagColor colorText;
	} plugin;
	struct {
		int channelDrawWidthPeak;
		int channelDrawWidthRMS;
		int channelMargin;
		azagColor colorBGTop;
		azagColor colorBGBot;
		azagColor colorDBTick;
		azagColor colorDBTickUnity;
		azagColor colorPeak;
		azagColor colorPeakUnity;
		azagColor colorPeakOver;
		azagColor colorRMS;
		azagColor colorRMSOver;
	} meter;
	struct {
		int width;
		int knobHeight;
		azagColor colorBGTop;
		azagColor colorBGBot;
		azagColor colorDBTick;
		azagColor colorDBTickUnity;
		azagColor colorBGHighlight;
		azagColor colorKnobTop;
		azagColor colorKnobBot;
		azagColor colorKnobCenterLine;
		azagColor colorMuteButton;
	} fader;
	struct {
		int width;
		int knobHeight;
		azagColor colorBGHighlight;
		azagColor colorBGTop;
		azagColor colorBGBot;
		azagColor colorKnobTop;
		azagColor colorKnobBot;
		azagColor colorKnobCenterLine;
	} slider;
	struct {
		azagColor colorText;
		azagColor colorBGLeft;
		azagColor colorBGRight;
		azagColor colorOutline;
		azagColor colorSelection;
		azagColor colorCursor;
	} textbox;
	struct {
		int thickness;
		azagColor colorBGLo; // For horizontal, this is left, for vertical this is top
		azagColor colorBGHi; // For horizontal, this is right, for vertical this is bottom
		azagColor colorFGLo;
		azagColor colorFGHi;
	} scrollbar;
} azagTheme;

extern azagTheme azagThemeCurrent;

void azagSetDefaultTheme();
void azagSetTheme(const azagTheme *theme);
int azagGetFontSizeForScale(azagTextScale scale);



// General utilities



// Gets the width of the text including text margin
static inline int azagTextWidthMargin(const char *text, azagTextScale scale) {
	return azagTextWidth(text, scale) + azagThemeCurrent.marginText.x * 2;
}
// Gets the height of the text including text margin
static inline int azagTextHeightMargin(const char *text, azagTextScale scale) {
	return azagTextHeight(text, scale) + azagThemeCurrent.marginText.y * 2;
}
// Gets the size of the text including text margin
static inline azagPoint azagTextSizeMargin(const char *text, azagTextScale scale) {
	azagPoint totalMargin = azagPointAdd(azagThemeCurrent.marginText, azagThemeCurrent.marginText);
	return azagPointAdd(azagTextSize(text, scale), totalMargin);
}

// Draw the text like normal, just offset by text margin
static inline void azagDrawTextMargin(const char *text, azagPoint position, azagTextScale textScale, azagColor color) {
	azagDrawText(text, azagPointAdd(position, azagThemeCurrent.marginText), textScale, color);
}
// anchor of {0,0} means top left
static inline void azagDrawTextAligned(const char *text, azagPoint position, azagTextScale textScale, azagColor color, float anchorX, float anchorY) {
	azagPoint textSize = azagTextSize(text, textScale);
	azagPoint offset = {
		(int)((float)textSize.x * anchorX),
		(int)((float)textSize.y * anchorY),
	};
	azagDrawText(text, azagPointAdd(position, offset), textScale, color);
}
// anchor of {0,0} means top left, including text margin
static inline void azagDrawTextAlignedMargin(const char *text, azagPoint position, azagTextScale textScale, azagColor color, float anchorX, float anchorY) {
	azagPoint textSize = azagTextSizeMargin(text, textScale);
	azagPoint offset = {
		(int)((float)textSize.x * anchorX),
		(int)((float)textSize.y * anchorY),
	};
	azagDrawTextMargin(text, azagPointAdd(position, offset), textScale, color);
}

// Rect shrinking functions that add the theme's margin internally so you don't have to write it out so much.

static inline void azagRectShrinkMargin(azagRect *rect, int additional) {
	azagRectShrinkAllXY(rect, azagPointAdd(azagThemeCurrent.margin, (azagPoint) {additional, additional}));
}

static inline void azagRectShrinkMarginXY(azagRect *rect, azagPoint additional) {
	azagRectShrinkAllXY(rect, azagPointAdd(azagThemeCurrent.margin, additional));
}

static inline void azagRectShrinkMarginH(azagRect *rect, int additional) {
	azagRectShrinkAllH(rect, azagThemeCurrent.margin.x + additional);
}

static inline void azagRectShrinkMarginV(azagRect *rect, int additional) {
	azagRectShrinkAllV(rect, azagThemeCurrent.margin.y + additional);
}

static inline void azagRectShrinkTopMargin(azagRect *rect, int additional) {
	azagRectShrinkTop(rect, azagThemeCurrent.margin.y + additional);
}

static inline void azagRectShrinkBottomMargin(azagRect *rect, int additional) {
	azagRectShrinkBottom(rect, azagThemeCurrent.margin.y + additional);
}

static inline void azagRectShrinkLeftMargin(azagRect *rect, int additional) {
	azagRectShrinkLeft(rect, azagThemeCurrent.margin.x + additional);
}

static inline void azagRectShrinkRightMargin(azagRect *rect, int additional) {
	azagRectShrinkRight(rect, azagThemeCurrent.margin.x + additional);
}



/*
	knobRect is the region on the screen that can be grabbed
	value is the target value to be changed by dragging
	inverted changes how the drag coordinates change value. When true, positive mouse movements (down, or right) result in negative changes in value.
	dragRegion is how many logical pixels we have to drag (used to scale pixels to the range determined by valueMin and valueMax)
	when vertical is true, we use the y component of the mouse drag
	valueMin determines the lowest value representable in our drag region
	valueMax determines the highest value representable in our drag region
	when doClamp is true, the output value is clamped between valueMin and valueMax
	preciseDiv is a scaling factor used during precise dragging (actual delta = delta / preciseDiv)
	when doPrecise is true, holding either shift key enables precise dragging
	snapInterval is the exact interval we snap to when snapping (modified by precise dragging)
	when doSnap is true, holding either control key enables snapping
	returns true if we're dragging, meaning the value may have updated
*/
bool azagMouseDragFloat(azagRect knobRect, float *value, bool inverted, int dragRegion, bool vertical, float valueMin, float valueMax, bool doClamp, float preciseDiv, bool doPrecise, float snapInterval, bool doSnap);
/*
	knobRect is the region on the screen that can be grabbed
	value is the target value to be changed by dragging
	inverted changes how the drag coordinates change value. When true, positive mouse movements (down, or right) result in negative changes in value.
	dragRegion is how many logical pixels we have to drag (used to scale pixels to the range determined by valueMin and valueMax)
	when vertical is true, we use the y component of the mouse drag
	valueMin determines the lowest value representable in our drag region
	valueMax determines the highest value representable in our drag region
	when doClamp is true, the output value is clamped between valueMin and valueMax
	preciseDiv is a scaling factor used during precise dragging (actual delta = delta / preciseDiv)
	when doPrecise is true, holding either shift key enables precise dragging
	snapInterval is the exact interval we snap to when snapping (modified by precise dragging)
	when doSnap is true, holding either control key enables snapping
	NOTE: snapping is done in linear space, since basically the only reason snapping exists at all is to make the number pretty
	returns true if we're dragging, meaning the value may have updated
*/
bool azagMouseDragFloatLog(azagRect knobRect, float *value, bool inverted, int dragRegion, bool vertical, float valueMin, float valueMax, bool doClamp, float preciseDiv, bool doPrecise, float snapInterval, bool doSnap);
/*
	knobRect is the region on the screen that can be grabbed
	value is the target value to be changed by dragging
	inverted changes how the drag coordinates change value. When true, positive mouse movements (down, or right) result in negative changes in value.
	dragRegion is how many logical pixels we have to drag (used to scale pixels to the range determined by valueMin and valueMax)
	when vertical is true, we use the y component of the mouse drag
	valueMin determines the lowest value representable in our drag region
	valueMax determines the highest value representable in our drag region
	when doClamp is true, the output value is clamped between valueMin and valueMax
	preciseDiv is a scaling factor used during precise dragging (actual delta = delta / preciseDiv)
	when doPrecise is true, holding either shift key enables precise dragging
	snapInterval is the exact interval we snap to when snapping (modified by precise dragging)
	when doSnap is true, holding either control key enables snapping
	returns true if we're dragging, meaning the value may have updated
*/
bool azagMouseDragInt(azagRect knobRect, int *value, bool inverted, int dragRegion, bool vertical, int valueMin, int valueMax, bool doClamp, int preciseDiv, bool doPrecise, int snapInterval, bool doSnap);

/*
	get a y offset for the given db reading, height, and dbRange spread across the height
	cannot go beyond height
*/
static inline int azagDBToYOffset(float db, int height, float dbRange) {
	return (int)AZA_MIN(db * (float)height / dbRange, height);
}
/*
	get a y offset for the given db reading, height, and dbRange spread across the height
	cannot go beyond height, or below minY
*/
static inline int azagDBToYOffsetClamped(float db, int height, int minY, float dbRange) {
	int result = azagDBToYOffset(db, height, dbRange);
	return AZA_MAX(result, minY);
}



// Widgets



/*
	Draws the ticks for faders and meters, from top to bottom ascending.
	dbRange is the total number of ticks
	dbOffset determines which tick is considered unity
*/
void azagDrawDBTicks(azagRect bounds, int dbRange, int dbOffset, azagColor color, azagColor colorUnity);

void azagRectCutOutFaderMuteButton(azagRect *rect);

/*
	Draws a standard volume fader, with a width determined by theme, and a height determined by bounds.
	gain is the target value
	mute is the target mute toggle value
	if mute is NULL, we don't draw the mute button
	if cutOutMissingMuteButton is true, then even when a mute button is missing, we'll cut out the top of the fader where the mute button would be.
	- This allows faders with and without mute buttons, with the same dbRange, to line up all of the dB ticks horizontally.
	label is text that will be shown on a tooltip when the mouse hovers over the fader
	dbRange is how many dB of gain are physically represented across the whole fader
	dbHeadroom is how many dB of gain are physically represented above unity
	The actual range represented goes from dbHeadroom-dbRange to dbHeadroom from bottom to top.
	returns used width
*/
int azagDrawFader(azagRect bounds, float *gain, bool *mute, bool cutOutMissingMuteButton, const char *label, int dbRange, int dbHeadroom);

/*
	Logarithmic slider allowing values between min and max.
	Scrolling up multiplies the value by (1.0f + step) and scrolling down divides it by the same value.
	Double clicking will set value to def.
	valueFormat is a printf-style format string, used for formatting the tooltip string, where the argument is *value. If NULL, defaults to "%+.1f"
	returns used width
*/
int azagDrawSliderFloatLog(azagRect bounds, float *value, float min, float max, float step, float def, const char *label, const char *valueFormat);
/*
	Linear slider allowing values between min and max.
	Scrolling up adds step and scrolling down subtracts step.
	Double clicking will set value to def.
	valueFormat is a printf-style format string, used for formatting the tooltip string, where the argument is *value. If NULL, defaults to "%+.1f"
	returns used width
*/
int azagDrawSliderFloat(azagRect bounds, float *value, float min, float max, float step, float def, const char *label, const char *valueFormat);



// If textCapacity is specified, this textbox will be editable.
void azagDrawTextBox(azagRect bounds, char *text, uint32_t textCapacity);



void azagDrawScrollbarHorizontal(azagRect bounds, int *value, int min, int max, int step);



#ifdef __cplusplus
}
#endif

#endif // AZAUDIO_GUI_H
