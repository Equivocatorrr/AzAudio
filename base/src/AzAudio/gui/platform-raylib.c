/*
	File: platform-raylib.c
	Author: Philip Haynes
*/

#include "platform.h"
#include "gui.h"
#include <raylib.h>

#include "../AzAudio.h"
#include "../error.h"
#include "../math.h"
#include <stdio.h>


// Internal state



static float currentDPIScale = 1.0f;
static int mouseCursorPrevious = MOUSE_CURSOR_DEFAULT;
static int mouseCursorCurrent = MOUSE_CURSOR_DEFAULT;



// Platform internals



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

// TODO: For raylib on glfw, wayland support appears incredibly broken, so I guess we can't have nice things for now. Check back later for updates to raylib that might address this (even if that just means they updated the version of glfw they ship.) In the meantime, just using X11 (current default for raylib) seems to work fine.

static float azagGetDPIScale() {
	float rounded = roundf(GetWindowScaleDPI().x);
	return azaMaxf(rounded, 1.0f);
}

static void azagHandleDPIChanges() {
	float newDPI = azagGetDPIScale();
	if (newDPI != currentDPIScale) {
		// TODO: Check and make sure we don't re-resize if we're dragged to maximize on a different monitor.
		SetWindowSize((int)((float)GetScreenWidth() * newDPI / currentDPIScale), (int)((float)GetScreenHeight() * newDPI / currentDPIScale));
		currentDPIScale = newDPI;
	}
}

static inline Color GetRaylibColor(azagColor color) {
	return (Color) {
		color.r,
		color.g,
		color.b,
		color.a,
	};
}

static inline Vector2 GetRaylibVector2(azaVec2 myVector) {
	return (Vector2) {
		.x = myVector.x,
		.y = myVector.y,
	};
}

static inline azaVec2 azaVec2FromRaylib(Vector2 rlVector) {
	return (azaVec2) {
		.x = rlVector.x,
		.y = rlVector.y,
	};
}




// Platform exports



static bool oneWindowCreated = false;
static bool windowIsOpen = false;
static int windowWidth = 0;
static int windowHeight = 0;
static char windowTitle[128] = {0};
static uint32_t windowConfigFlags = 0;

azagWindow azagWindowCreate(int width, int height, const char *title) {
	assert(!oneWindowCreated && "we already have one window, and Raylib doesn't support more than 1!");
	oneWindowCreated = true;
	windowWidth = width;
	windowHeight = height;
	aza_strcpy(windowTitle, title, sizeof(windowTitle));
	windowConfigFlags = FLAG_VSYNC_HINT | FLAG_WINDOW_RESIZABLE;

	SetTraceLogLevel(LOG_ERROR);
	SetTraceLogCallback(azaRaylibTraceLogCallback);
	return (azagWindow) {1};
}

void azagWindowDestroy(azagWindow window) {
	assert(oneWindowCreated);
	oneWindowCreated = false;
}

void azagWindowSetAlwaysOnTop(bool alwaysOnTop) {
	if (alwaysOnTop) {
		windowConfigFlags |= FLAG_WINDOW_TOPMOST;
		if (windowIsOpen) {
			SetWindowState(FLAG_WINDOW_TOPMOST);
		}
	} else {
		windowConfigFlags &= ~FLAG_WINDOW_TOPMOST;
		if (windowIsOpen) {
			ClearWindowState(FLAG_WINDOW_TOPMOST);
		}
	}
}

int azagWindowOpen() {
	void azagOnGuiOpen();
	currentDPIScale = 1.0f;
	SetConfigFlags(windowConfigFlags);
	InitWindow(windowWidth, windowHeight, windowTitle);
	SetExitKey(-1);
	windowIsOpen = true;
	azagOnGuiOpen();
	return AZA_SUCCESS;
}

void azagWindowClose() {
	CloseWindow();
	windowIsOpen = false;
}

bool azagWindowShouldClose() {
	azagWindowPollEvents();
	return WindowShouldClose();
}

void azagWindowPollEvents() {
	// PollInputEvents();
	azagHandleDPIChanges();
}

void azagWindowSetCurrent(azagWindow window) {
	(void)window;
}



float azagGetScreenWidth() {
	return (float)GetScreenWidth() / currentDPIScale;
}

float azagGetScreenHeight() {
	return (float)GetScreenHeight() / currentDPIScale;
}

azaVec2 azagGetScreenSize() {
	return (azaVec2) {
		.x = azagGetScreenWidth(),
		.y = azagGetScreenHeight(),
	};
}

void azagBeginDrawing() {
	void azagOnDrawBegin(); // Defined in gui.c, not declared in gui.h
	BeginDrawing();
	azagOnDrawBegin(); // Automagically handle internal GUI stuff
}

void azagEndDrawing() {
	void azagOnDrawEnd(); // Defined in gui.c, not declared in gui.h
	azagOnDrawEnd(); // Automagically handle internal GUI stuff
	if (mouseCursorCurrent != mouseCursorPrevious) {
		// We do this here instead of in the function call because rapidly setting it back and forth confuses Windows. It may do so on other platforms as well, so best just to chill it out.
		SetMouseCursor(mouseCursorCurrent);
		mouseCursorPrevious = mouseCursorCurrent;
	}
	EndDrawing();
}

void azagClearBackground(azagColor color) {
	ClearBackground(GetRaylibColor(color));
}

void azagSetScissor(azagRect rect) {
	rect.xy = azaMulVec2Scalar(rect.xy, currentDPIScale);
	rect.size = azaMulVec2Scalar(rect.size, currentDPIScale);
	BeginScissorMode((int)rect.x, (int)rect.y, (int)rect.w, (int)rect.h);
}

void azagResetScissor() {
	EndScissorMode();
}



// This is how Raylib gets spacing internally, which we need to do in some cases
// ^ This used to be true until we replaced all the basic calls to ones where we provide spacing, so I guess we can do whatever we want now.
static float GetRaylibTextSpacing(float fontSize) {
	float defaultFontSize = 10.0f;
	float spacing = azaMaxf(fontSize, defaultFontSize) / defaultFontSize;
	return spacing;
}

float azagTextWidth(const char *text, azagTextScale scale) {
	float fontSize = azagGetFontSizeForScale(scale);
	float spacing = GetRaylibTextSpacing(fontSize);
	return MeasureTextEx(GetFontDefault(), text, fontSize, spacing).x;
}

float azagTextHeight(const char *text, azagTextScale scale) {
	float fontSize = azagGetFontSizeForScale(scale);
	return fontSize * (float)azaTextCountLines(text);
}

azaVec2 azagTextSize(const char *text, azagTextScale scale) {
	float fontSize = azagGetFontSizeForScale(scale);
	float spacing = GetRaylibTextSpacing(fontSize);
	azaVec2 result = azaVec2FromRaylib(MeasureTextEx(GetFontDefault(), text, fontSize, spacing));
	return result;
}



// Drawing



azagCharacterAdvanceX azagGetCharacterAdvanceX(const char *text, azagTextScale scale) {
	// We use MeasureTextEx as a reference for how Raylib advances per-character and extract that same behavior
	Font font = GetFontDefault();
	int codepointSize = 0;
	int codepoint = GetCodepointNext(text, &codepointSize);
	int index = GetGlyphIndex(font, codepoint);
	float fontSize = (float)azagGetFontSizeForScale(scale);
	float scaleFactor = fontSize/(float)font.baseSize;
	float spacing = GetRaylibTextSpacing(fontSize);
	azagCharacterAdvanceX result = {
		.characterBytes = codepointSize,
		.advance = spacing,
		.width = 0.0f,
	};
	if (codepoint != '\n') {
		result.width = (font.recs[index].width + (float)font.glyphs[index].offsetX) * scaleFactor;
		if (font.glyphs[index].advanceX > 0) {
			result.advance = ((float)font.glyphs[index].advanceX) * scaleFactor + spacing;
		} else {
			result.advance = result.width + spacing;
		}
	}
	return result;
}

void azagDrawText(const char *text, azaVec2 position, azagTextScale textScale, azagColor color) {
	Font font = GetFontDefault();
	float fontSize = azagGetFontSizeForScale(textScale) * currentDPIScale;
	float spacing = GetRaylibTextSpacing(fontSize);
	// round position because Raylib's default font looks horrible if you don't
	position = (azaVec2) {
		.x = roundf(position.x * currentDPIScale),
		.y = roundf(position.y * currentDPIScale),
	};
	Vector2 rlPos = GetRaylibVector2(position);
	DrawTextEx(font, text, rlPos, fontSize, spacing, GetRaylibColor(color));
}

void azagDrawTextRotated(const char *text, azaVec2 position, azagTextScale textScale, azagColor color, float rotationDegrees, azaVec2 origin) {
	Font font = GetFontDefault();
	float fontSize = azagGetFontSizeForScale(textScale) * currentDPIScale;
	float spacing = GetRaylibTextSpacing(fontSize);
	// round position because Raylib's default font looks horrible if you don't
	position = (azaVec2) {
		.x = roundf(position.x * currentDPIScale),
		.y = roundf(position.y * currentDPIScale),
	};
	Vector2 rlPos = GetRaylibVector2(position);
	azaVec2 textSize = azaVec2FromRaylib(MeasureTextEx(GetFontDefault(), text, fontSize, spacing));
	// textSize = azaMulVec2Scalar(textSize, currentDPIScale);
	Vector2 rlOrigin = GetRaylibVector2(azaMulVec2(origin, textSize));
	DrawTextPro(font, text, rlPos, rlOrigin, rotationDegrees, fontSize, spacing, GetRaylibColor(color));
}

void azagDrawLineThickness(azaVec2 posStart, azaVec2 posEnd, float lineThickness, azagColor color) {
	posStart = azaMulVec2Scalar(posStart, currentDPIScale);
	posEnd = azaMulVec2Scalar(posEnd, currentDPIScale);
	lineThickness = azaMaxf(1.0f, lineThickness * currentDPIScale);
	DrawLineEx(GetRaylibVector2(posStart), GetRaylibVector2(posEnd), lineThickness, GetRaylibColor(color));
}

void azagDrawRect(azagRect rect, azagColor color) {
	rect.xy = azaMulVec2Scalar(rect.xy, currentDPIScale);
	rect.size = azaMulVec2Scalar(rect.size, currentDPIScale);
	DrawRectangleV(GetRaylibVector2(rect.xy), GetRaylibVector2(rect.size), GetRaylibColor(color));
}

void azagDrawRectOutlineThickness(azagRect rect, float lineThickness, azagColor color) {
	Rectangle rlRect = {
		.x = rect.x * currentDPIScale,
		.y = rect.y * currentDPIScale,
		.width = rect.w * currentDPIScale,
		.height = rect.h * currentDPIScale,
	};
	lineThickness = azaMaxf(1.0f, lineThickness * currentDPIScale);
	DrawRectangleLinesEx(rlRect, lineThickness, GetRaylibColor(color));
}

void azagDrawRectGradient(azagRect rect, azagColor topLeft, azagColor bottomLeft, azagColor topRight, azagColor bottomRight) {
	Rectangle rec = {
		.x = rect.x * currentDPIScale,
		.y = rect.y * currentDPIScale,
		.width = rect.w * currentDPIScale,
		.height = rect.h * currentDPIScale,
	};
	// DrawRectangleGradientEx has incorrect parameter names, so we pass them in according to function, not name.
	DrawRectangleGradientEx(rec, GetRaylibColor(topLeft), GetRaylibColor(bottomLeft), GetRaylibColor(bottomRight), GetRaylibColor(topRight));
}

void azagDrawRectGradientV(azagRect rect, azagColor top, azagColor bottom) {
	azagDrawRectGradient(rect, top, bottom, top, bottom);
}

void azagDrawRectGradientH(azagRect rect, azagColor left, azagColor right) {
	azagDrawRectGradient(rect, left, left, right, right);
}



// Mouse Input



static inline int RaylibMouseButton(azagMouseButton button) {
	return (int)button;
}

azaVec2 azagMousePosition() {
	Vector2 mouse = GetMousePosition();
	return (azaVec2) { mouse.x / currentDPIScale, mouse.y / currentDPIScale };
}

float azagMouseWheelV() {
	return GetMouseWheelMoveV().y;
}

float azagMouseWheelH() {
	return GetMouseWheelMoveV().x;
}

bool azagMousePressed_base(azagMouseButton button) {
	return IsMouseButtonPressed(RaylibMouseButton(button));
}

bool azagMouseDown_base(azagMouseButton button) {
	return IsMouseButtonDown(RaylibMouseButton(button));
}

bool azagMouseReleased_base(azagMouseButton button) {
	return IsMouseButtonReleased(RaylibMouseButton(button));
}

static int raylibCursorMap[] = {
	[AZAG_MOUSE_CURSOR_DEFAULT]           = MOUSE_CURSOR_DEFAULT,
	[AZAG_MOUSE_CURSOR_ARROW]             = MOUSE_CURSOR_ARROW,
	[AZAG_MOUSE_CURSOR_IBEAM]             = MOUSE_CURSOR_IBEAM,
	[AZAG_MOUSE_CURSOR_CROSSHAIR]         = MOUSE_CURSOR_CROSSHAIR,
	[AZAG_MOUSE_CURSOR_POINTING_HAND]     = MOUSE_CURSOR_POINTING_HAND,
	[AZAG_MOUSE_CURSOR_RESIZE_H]          = MOUSE_CURSOR_RESIZE_EW,
	[AZAG_MOUSE_CURSOR_RESIZE_V]          = MOUSE_CURSOR_RESIZE_NS,
	[AZAG_MOUSE_CURSOR_RESIZE_DIAG_TL2BR] = MOUSE_CURSOR_RESIZE_NWSE,
	[AZAG_MOUSE_CURSOR_RESIZE_DIAG_TR2BL] = MOUSE_CURSOR_RESIZE_NESW,
	[AZAG_MOUSE_CURSOR_RESIZE_OMNI]       = MOUSE_CURSOR_RESIZE_ALL,
	[AZAG_MOUSE_CURSOR_NOT_ALLOWED]       = MOUSE_CURSOR_NOT_ALLOWED,
};

void azagSetMouseCursor(azagMouseCursor cursor) {
	mouseCursorCurrent = raylibCursorMap[(int)cursor];
}



// Keyboard input



static int16_t raylibKeyCodeMap[] = {
	[AZAG_KEY_NONE]                = KEY_NULL,
	[AZAG_KEY_ERR]                 = KEY_NULL,
	[AZAG_KEY_A]                   = KEY_A,
	[AZAG_KEY_B]                   = KEY_B,
	[AZAG_KEY_C]                   = KEY_C,
	[AZAG_KEY_D]                   = KEY_D,
	[AZAG_KEY_E]                   = KEY_E,
	[AZAG_KEY_F]                   = KEY_F,
	[AZAG_KEY_G]                   = KEY_G,
	[AZAG_KEY_H]                   = KEY_H,
	[AZAG_KEY_I]                   = KEY_I,
	[AZAG_KEY_J]                   = KEY_J,
	[AZAG_KEY_K]                   = KEY_K,
	[AZAG_KEY_L]                   = KEY_L,
	[AZAG_KEY_M]                   = KEY_M,
	[AZAG_KEY_N]                   = KEY_N,
	[AZAG_KEY_O]                   = KEY_O,
	[AZAG_KEY_P]                   = KEY_P,
	[AZAG_KEY_Q]                   = KEY_Q,
	[AZAG_KEY_R]                   = KEY_R,
	[AZAG_KEY_S]                   = KEY_S,
	[AZAG_KEY_T]                   = KEY_T,
	[AZAG_KEY_U]                   = KEY_U,
	[AZAG_KEY_V]                   = KEY_V,
	[AZAG_KEY_W]                   = KEY_W,
	[AZAG_KEY_X]                   = KEY_X,
	[AZAG_KEY_Y]                   = KEY_Y,
	[AZAG_KEY_Z]                   = KEY_Z,
	[AZAG_KEY_1]                   = KEY_ONE,
	[AZAG_KEY_2]                   = KEY_TWO,
	[AZAG_KEY_3]                   = KEY_THREE,
	[AZAG_KEY_4]                   = KEY_FOUR,
	[AZAG_KEY_5]                   = KEY_FIVE,
	[AZAG_KEY_6]                   = KEY_SIX,
	[AZAG_KEY_7]                   = KEY_SEVEN,
	[AZAG_KEY_8]                   = KEY_EIGHT,
	[AZAG_KEY_9]                   = KEY_NINE,
	[AZAG_KEY_0]                   = KEY_ZERO,
	[AZAG_KEY_ENTER]               = KEY_ENTER,
	[AZAG_KEY_ESC]                 = KEY_ESCAPE,
	[AZAG_KEY_BACKSPACE]           = KEY_BACKSPACE,
	[AZAG_KEY_TAB]                 = KEY_TAB,
	[AZAG_KEY_SPACE]               = KEY_SPACE,
	[AZAG_KEY_MINUS]               = KEY_MINUS,
	[AZAG_KEY_EQUAL]               = KEY_EQUAL,
	[AZAG_KEY_LEFTBRACE]           = KEY_LEFT_BRACKET,
	[AZAG_KEY_RIGHTBRACE]          = KEY_RIGHT_BRACKET,
	[AZAG_KEY_BACKSLASH]           = KEY_BACKSLASH,
	[AZAG_KEY_HASHTILDE]           = KEY_NULL,
	[AZAG_KEY_SEMICOLON]           = KEY_SEMICOLON,
	[AZAG_KEY_APOSTROPHE]          = KEY_APOSTROPHE,
	[AZAG_KEY_GRAVE]               = KEY_GRAVE,
	[AZAG_KEY_COMMA]               = KEY_COMMA,
	[AZAG_KEY_DOT]                 = KEY_PERIOD,
	[AZAG_KEY_SLASH]               = KEY_SLASH,
	[AZAG_KEY_CAPSLOCK]            = KEY_CAPS_LOCK,
	[AZAG_KEY_F1]                  = KEY_F1,
	[AZAG_KEY_F2]                  = KEY_F2,
	[AZAG_KEY_F3]                  = KEY_F3,
	[AZAG_KEY_F4]                  = KEY_F4,
	[AZAG_KEY_F5]                  = KEY_F5,
	[AZAG_KEY_F6]                  = KEY_F6,
	[AZAG_KEY_F7]                  = KEY_F7,
	[AZAG_KEY_F8]                  = KEY_F8,
	[AZAG_KEY_F9]                  = KEY_F9,
	[AZAG_KEY_F10]                 = KEY_F10,
	[AZAG_KEY_F11]                 = KEY_F11,
	[AZAG_KEY_F12]                 = KEY_F12,
	[AZAG_KEY_SYSRQ]               = KEY_PRINT_SCREEN,
	[AZAG_KEY_SCROLLLOCK]          = KEY_SCROLL_LOCK,
	[AZAG_KEY_PAUSE]               = KEY_PAUSE,
	[AZAG_KEY_INSERT]              = KEY_INSERT,
	[AZAG_KEY_HOME]                = KEY_HOME,
	[AZAG_KEY_PAGEUP]              = KEY_PAGE_UP,
	[AZAG_KEY_DELETE]              = KEY_DELETE,
	[AZAG_KEY_END]                 = KEY_END,
	[AZAG_KEY_PAGEDOWN]            = KEY_PAGE_DOWN,
	[AZAG_KEY_RIGHT]               = KEY_RIGHT,
	[AZAG_KEY_LEFT]                = KEY_LEFT,
	[AZAG_KEY_DOWN]                = KEY_DOWN,
	[AZAG_KEY_UP]                  = KEY_UP,
	[AZAG_KEY_NUMLOCK]             = KEY_NUM_LOCK,
	[AZAG_KEY_KPSLASH]             = KEY_KP_DIVIDE,
	[AZAG_KEY_KPASTERISK]          = KEY_KP_MULTIPLY,
	[AZAG_KEY_KPMINUS]             = KEY_KP_SUBTRACT,
	[AZAG_KEY_KPPLUS]              = KEY_KP_ADD,
	[AZAG_KEY_KPENTER]             = KEY_KP_ENTER,
	[AZAG_KEY_KP1]                 = KEY_KP_1,
	[AZAG_KEY_KP2]                 = KEY_KP_2,
	[AZAG_KEY_KP3]                 = KEY_KP_3,
	[AZAG_KEY_KP4]                 = KEY_KP_4,
	[AZAG_KEY_KP5]                 = KEY_KP_5,
	[AZAG_KEY_KP6]                 = KEY_KP_6,
	[AZAG_KEY_KP7]                 = KEY_KP_7,
	[AZAG_KEY_KP8]                 = KEY_KP_8,
	[AZAG_KEY_KP9]                 = KEY_KP_9,
	[AZAG_KEY_KP0]                 = KEY_KP_0,
	[AZAG_KEY_KPDOT]               = KEY_KP_DECIMAL,
	[AZAG_KEY_102ND]               = KEY_NULL,
	[AZAG_KEY_COMPOSE]             = KEY_NULL,
	[AZAG_KEY_POWER]               = KEY_NULL,
	[AZAG_KEY_KPEQUAL]             = KEY_KP_EQUAL,
	[AZAG_KEY_F13]                 = KEY_NULL,
	[AZAG_KEY_F14]                 = KEY_NULL,
	[AZAG_KEY_F15]                 = KEY_NULL,
	[AZAG_KEY_F16]                 = KEY_NULL,
	[AZAG_KEY_F17]                 = KEY_NULL,
	[AZAG_KEY_F18]                 = KEY_NULL,
	[AZAG_KEY_F19]                 = KEY_NULL,
	[AZAG_KEY_F20]                 = KEY_NULL,
	[AZAG_KEY_F21]                 = KEY_NULL,
	[AZAG_KEY_F22]                 = KEY_NULL,
	[AZAG_KEY_F23]                 = KEY_NULL,
	[AZAG_KEY_F24]                 = KEY_NULL,
	[AZAG_KEY_OPEN]                = KEY_NULL,
	[AZAG_KEY_HELP]                = KEY_NULL,
	[AZAG_KEY_PROPS]               = KEY_NULL,
	[AZAG_KEY_FRONT]               = KEY_NULL,
	[AZAG_KEY_STOP]                = KEY_NULL,
	[AZAG_KEY_AGAIN]               = KEY_NULL,
	[AZAG_KEY_UNDO]                = KEY_NULL,
	[AZAG_KEY_CUT]                 = KEY_NULL,
	[AZAG_KEY_COPY]                = KEY_NULL,
	[AZAG_KEY_PASTE]               = KEY_NULL,
	[AZAG_KEY_FIND]                = KEY_NULL,
	[AZAG_KEY_MUTE]                = KEY_NULL,
	[AZAG_KEY_VOLUMEUP]            = KEY_VOLUME_UP,
	[AZAG_KEY_VOLUMEDOWN]          = KEY_VOLUME_DOWN,
	[AZAG_KEY_KPCOMMA]             = KEY_NULL,
	[AZAG_KEY_RO]                  = KEY_NULL,
	[AZAG_KEY_KATAKANAHIRAGANA]    = KEY_NULL,
	[AZAG_KEY_YEN]                 = KEY_NULL,
	[AZAG_KEY_HENKAN]              = KEY_NULL,
	[AZAG_KEY_MUHENKAN]            = KEY_NULL,
	[AZAG_KEY_KPJPCOMMA]           = KEY_NULL,
	[AZAG_KEY_HANGEUL]             = KEY_NULL,
	[AZAG_KEY_HANJA]               = KEY_NULL,
	[AZAG_KEY_KATAKANA]            = KEY_NULL,
	[AZAG_KEY_HIRAGANA]            = KEY_NULL,
	[AZAG_KEY_ZENKAKUHANKAKU]      = KEY_NULL,
	[AZAG_KEY_KPLEFTPAREN]         = KEY_NULL,
	[AZAG_KEY_KPRIGHTPAREN]        = KEY_NULL,
	[AZAG_KEY_LEFTCTRL]            = KEY_LEFT_CONTROL, // Keyboard Left Control
	[AZAG_KEY_LEFTSHIFT]           = KEY_LEFT_SHIFT, // Keyboard Left Shift
	[AZAG_KEY_LEFTALT]             = KEY_LEFT_ALT, // Keyboard Left Alt
	[AZAG_KEY_LEFTMETA]            = KEY_LEFT_SUPER, // Keyboard Left GUI
	[AZAG_KEY_RIGHTCTRL]           = KEY_RIGHT_CONTROL, // Keyboard Right Control
	[AZAG_KEY_RIGHTSHIFT]          = KEY_RIGHT_SHIFT, // Keyboard Right Shift
	[AZAG_KEY_RIGHTALT]            = KEY_RIGHT_ALT, // Keyboard Right Alt
	[AZAG_KEY_RIGHTMETA]           = KEY_RIGHT_SUPER, // Keyboard Right GUI
	[AZAG_KEY_MEDIA_PLAYPAUSE]     = KEY_NULL,
	[AZAG_KEY_MEDIA_STOPCD]        = KEY_NULL,
	[AZAG_KEY_MEDIA_PREVIOUSSONG]  = KEY_NULL,
	[AZAG_KEY_MEDIA_NEXTSONG]      = KEY_NULL,
	[AZAG_KEY_MEDIA_EJECTCD]       = KEY_NULL,
	[AZAG_KEY_MEDIA_VOLUMEUP]      = KEY_NULL,
	[AZAG_KEY_MEDIA_VOLUMEDOWN]    = KEY_NULL,
	[AZAG_KEY_MEDIA_MUTE]          = KEY_NULL,
	[AZAG_KEY_MEDIA_WWW]           = KEY_NULL,
	[AZAG_KEY_MEDIA_BACK]          = KEY_NULL,
	[AZAG_KEY_MEDIA_FORWARD]       = KEY_NULL,
	[AZAG_KEY_MEDIA_STOP]          = KEY_NULL,
	[AZAG_KEY_MEDIA_FIND]          = KEY_NULL,
	[AZAG_KEY_MEDIA_SCROLLUP]      = KEY_NULL,
	[AZAG_KEY_MEDIA_SCROLLDOWN]    = KEY_NULL,
	[AZAG_KEY_MEDIA_EDIT]          = KEY_NULL,
	[AZAG_KEY_MEDIA_SLEEP]         = KEY_NULL,
	[AZAG_KEY_MEDIA_COFFEE]        = KEY_NULL,
	[AZAG_KEY_MEDIA_REFRESH]       = KEY_NULL,
	[AZAG_KEY_MEDIA_CALC]          = KEY_NULL,
	[AZAG_KEY_MEDIA_MAIL]          = KEY_NULL,
	[AZAG_KEY_MEDIA_FILE]          = KEY_NULL,
};

static int RaylibKeyCode(azagKeyCode key) {
	return (int)raylibKeyCodeMap[key];
}



bool azagKeyRepeated(azagKeyCode key) {
	int rlkey = RaylibKeyCode(key);
	return IsKeyPressed(rlkey) || IsKeyPressedRepeat(rlkey);
}

bool azagKeyPressed(azagKeyCode key) {
	int rlkey = RaylibKeyCode(key);
	return IsKeyPressed(rlkey);
}

bool azagKeyDown(azagKeyCode key) {
	int rlkey = RaylibKeyCode(key);
	return IsKeyDown(rlkey);
}

bool azagKeyReleased(azagKeyCode key) {
	int rlkey = RaylibKeyCode(key);
	return IsKeyReleased(rlkey);
}

uint32_t azagGetNextChar() {
	return (uint32_t)GetCharPressed();
}

