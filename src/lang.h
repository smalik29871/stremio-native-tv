/* Language codes as players and addons report them: ISO 639-2/T ("deu"),
 * 639-2/B ("ger"), 639-1 ("de"), or an English name ("German"). */
#ifndef SNTV_LANG_H
#define SNTV_LANG_H

/* Languages offered in Settings and the first-run setup; LANG_CODES[0] is "" (Off). */
extern const char *const LANG_CODES[];
extern const char *const LANG_NAMES[];
int lang_count(void);
int lang_index(const char *code);          /* index into LANG_CODES, 0 when unknown */
const char *lang_display(const char *code); /* native name, or the code itself */

/* Canonical 3-letter code, or NULL when the code is empty, "und" or unknown. */
const char *lang_canonical(const char *code);
/* English name ("German") for matching release names; the code when unknown. */
const char *lang_display_english(const char *code);
/* Same language? Unknown codes only match themselves (case-insensitive). */
int lang_same(const char *a, const char *b);
/* A real language (not empty, "und", "mis", "zxx", "mul"). */
int lang_known(const char *code);

#endif
