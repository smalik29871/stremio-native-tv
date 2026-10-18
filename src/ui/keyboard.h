/* On-screen keyboard driven by the d-pad; also accepts SDL text input on desktop. */
#ifndef SNTV_KEYBOARD_H
#define SNTV_KEYBOARD_H
#include "input.h"

typedef struct Keyboard {
    char text[256];
    int row, col;
    int shift, symbols;
    int password;
} Keyboard;

typedef enum { KB_IGNORED = 0, KB_CHANGED, KB_DONE, KB_LEAVE_UP } KeyboardResult;

void keyboard_init(Keyboard *kb, int password, const char *initial);
KeyboardResult keyboard_key(Keyboard *kb, KeyAction a);
void keyboard_text_input(Keyboard *kb, const char *utf8);
void keyboard_draw(const Keyboard *kb, float x, float y, int focused);
float keyboard_height(void);
float keyboard_width(void);

#endif
