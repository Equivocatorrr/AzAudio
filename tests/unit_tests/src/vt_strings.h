/*
	File: vt_strings.h
	Author: Philip Haynes
	Get some pretty colors in the terminal!
*/

#ifndef VT_STRINGS_H
#define VT_STRINGS_H

#define VT_RESET          "\033[0m"
#define VT_FG_BLACK       "\033[30m"
#define VT_FG_DK_RED      "\033[31m"
#define VT_FG_DK_GREEN    "\033[32m"
#define VT_FG_DK_YELLOW   "\033[33m"
#define VT_FG_DK_BLUE     "\033[34m"
#define VT_FG_DK_MAGENTA  "\033[35m"
#define VT_FG_DK_CYAN     "\033[36m"
#define VT_FG_LT_GRAY     "\033[37m"
#define VT_FG_DK_GRAY     "\033[90m"
#define VT_FG_RED         "\033[91m"
#define VT_FG_GREEN       "\033[92m"
#define VT_FG_YELLOW      "\033[93m"
#define VT_FG_BLUE        "\033[94m"
#define VT_FG_MAGENTA     "\033[95m"
#define VT_FG_CYAN        "\033[96m"
#define VT_FG_WHITE       "\033[97m"
#define VT_BG_BLACK       "\033[40m"
#define VT_BG_DK_RED      "\033[41m"
#define VT_BG_DK_GREEN    "\033[42m"
#define VT_BG_DK_YELLOW   "\033[43m"
#define VT_BG_DK_BLUE     "\033[44m"
#define VT_BG_DK_MAGENTA  "\033[45m"
#define VT_BG_DK_CYAN     "\033[46m"
#define VT_BG_LT_GRAY     "\033[47m"
#define VT_BG_DK_GRAY     "\033[100m"
#define VT_BG_RED         "\033[101m"
#define VT_BG_GREEN       "\033[102m"
#define VT_BG_YELLOW      "\033[103m"
#define VT_BG_BLUE        "\033[104m"
#define VT_BG_MAGENTA     "\033[105m"
#define VT_BG_CYAN        "\033[106m"
#define VT_BG_WHITE       "\033[107m"

// String literal span
#define VT_SPAN(vt, body) vt body VT_RESET

#endif // VT_STRINGS_H