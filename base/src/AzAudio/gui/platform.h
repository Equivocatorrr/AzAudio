/*
	File: platform.h
	Author: Philip Haynes
	Platform backend-specific interface.
*/

#ifndef AZAUDIO_GUI_PLATFORM_H
#define AZAUDIO_GUI_PLATFORM_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif



typedef uint32_t azagWindow;
enum { AZAG_WINDOW_INVALID = 0 };

azagWindow azagWindowCreate(int width, int height, const char *title);
void azagWindowDestroy(azagWindow window);

// Set the window that's used for drawing and input functions
// This is thread_local, so each thread can work with different windows at the same time
// NOTE: As long as Raylib is the only backend, this is just a stub for the future, as Raylib doesn't support opening multiple windows.
void azagWindowSetCurrent(azagWindow window);

void azagWindowSetAlwaysOnTop(bool alwaysOnTop);

// Implementer's note: backend needs to call `void azagOnGuiOpen()` once drawing is set up
// This function is not in this header. You must declare it in the implementation file.
int azagWindowOpen();
void azagWindowClose();

void azagWindowPollEvents();

// Read close button, returning true if the window should be closed
// Calls azagWindowPollEvents internally.
bool azagWindowShouldClose();

// returns the logical screen width, which may not be integer because we factor in DPI scaling
float azagGetScreenWidth();
// returns the logical screen height, which may not be integer because we factor in DPI scaling
float azagGetScreenHeight();
// returns the logical screen size, which may not be integer because we factor in DPI scaling
azaVec2 azagGetScreenSize();



// Drawing



// Implementer's note: backend needs to call `void azagOnDrawBegin()` once drawing is set up
// This function is not in this header. You must declare it in the implementation file.
void azagBeginDrawing();
// Implementer's note: backend needs to call `void azagOnDrawEnd()` before actually finishing up drawing
// This function is not in this header. You must declare it in the implementation file.
void azagEndDrawing();

void azagClearBackground(azagColor color);

void azagSetScissor(azagRect rect);
void azagResetScissor();


void azagDrawLineThickness(azaVec2 posStart, azaVec2 posEnd, float lineThickness, azagColor color);
static inline void azagDrawLine(azaVec2 posStart, azaVec2 posEnd, azagColor color) {
	azagDrawLineThickness(posStart, posEnd, 1.0f, color);
}

void azagDrawRect(azagRect rect, azagColor color);
void azagDrawRect(azagRect rect, azagColor color);

void azagDrawRectOutlineThickness(azagRect rect, float lineThickness, azagColor color);
static inline void azagDrawRectOutline(azagRect rect, azagColor color) {
	azagDrawRectOutlineThickness(rect, 1.0f, color);
}

void azagDrawRectGradient(azagRect rect, azagColor topLeft, azagColor bottomLeft, azagColor topRight, azagColor bottomRight);
void azagDrawRectGradientV(azagRect rect, azagColor top, azagColor bottom);
void azagDrawRectGradientH(azagRect rect, azagColor left, azagColor right);



// Text



float azagTextWidth(const char *text, azagTextScale scale);
float azagTextHeight(const char *text, azagTextScale scale);
azaVec2 azagTextSize(const char *text, azagTextScale scale);

typedef struct azagCharacterAdvanceX {
	uint32_t characterBytes; // How many bytes long is the given UTF-8 codepoint
	float width; // Total width of the character, only considering the right of the cursor
	float advance; // This can be different from width if special kerning rules are used
} azagCharacterAdvanceX;
// returns some data about the first character in text, possibly with respect to the second character in text for the purposes of explicit kerning.
// may read up to 8 bytes of text, depending on the width of the utf-8 codepoints, but will not read past a null byte
azagCharacterAdvanceX azagGetCharacterAdvanceX(const char *text, azagTextScale scale);

// Text is aligned on the top left
void azagDrawText(const char *text, azaVec2 position, azagTextScale textScale, azagColor color);
// Text is aligned and rotated around origin, where x and y are between 0 and 1 inclusive
// origin.x of 0 is the left of the text, and 1 is the right
// origin.y of 0 is the top of the text, and 1 is the bottom
// rotationDegrees increases as the angle goes clockwise
void azagDrawTextRotated(const char *text, azaVec2 position, azagTextScale textScale, azagColor color, float rotationDegrees, azaVec2 origin);



// Mouse input



azaVec2 azagMousePosition();
float azagMouseWheelV();
float azagMouseWheelH();

// Base form that does no depth culling, just the actual input from the platform
bool azagMousePressed_base(azagMouseButton button);
bool azagMouseDown_base(azagMouseButton button);
bool azagMouseReleased_base(azagMouseButton button);

void azagSetMouseCursor(azagMouseCursor cursor);



// Keyboard input



bool azagKeyRepeated(azagKeyCode key);
bool azagKeyPressed(azagKeyCode key);
bool azagKeyDown(azagKeyCode key);
bool azagKeyReleased(azagKeyCode key);

// returns the next queued unicode character, or 0 if the queue is empty.
// TODO: Generally speaking this is oversimplified for text input. We can keep this interface and add a better one later for IMEs and such.
uint32_t azagGetNextChar();




#ifdef __cplusplus
}
#endif

#endif // AZAUDIO_GUI_PLATFORM_H
