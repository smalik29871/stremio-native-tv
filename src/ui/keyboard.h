/* On-screen keyboard driven by the d-pad; also accepts SDL text input on desktop. */
#ifndef SNTV_KEYBOARD_H
#define SNTV_KEYBOARD_H
#include "input.h"

typedef struct Keyboard {
    char text[256];
    int row, col;
    int shift, symbols;
    int password;
    int compact;   /* Search's keyboard: QWERTY, 10 keys wide, fitted to fit_w */
    float fit_w;   /* compact: how wide the keys together are (0: the Search column's 552) */
} Keyboard;

typedef enum { KB_IGNORED = 0, KB_CHANGED, KB_DONE, KB_LEAVE_UP } KeyboardResult;

void keyboard_init(Keyboard *kb, int password, const char *initial);
KeyboardResult keyboard_key(Keyboard *kb, KeyAction a);
void keyboard_text_input(Keyboard *kb, const char *utf8);
void keyboard_draw(const Keyboard *kb, float x, float y, int focused);
float keyboard_height(void);
float keyboard_width(void);
float keyboard_width_of(const Keyboard *kb);
float keyboard_height_of(const Keyboard *kb);
int keyboard_cols(const Keyboard *kb);   /* keys across: the right-hand column is keyboard_cols() - 1 */
/* Touch: each key is a hit target (ui_hit) with an id from KB_HIT up. A tapped key takes
 * the keyboard's focus (1); OK then presses it. 0 when the id is not a key. */
enum { KB_HIT = 0x7B000000 };   /* clear of HIT_TRAILER_SOUND (0x7E000000), which the key "a" (row 0, column 0) used to share */
int keyboard_tap(Keyboard *kb, int id);

#endif
