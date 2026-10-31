/* The notes of one CHANGELOG.md section, for one kind of device.
 *
 *   - [phone] **A title.** What it does, in a sentence or two.
 *     More lines of the same note are indented.
 *
 * An item with no tag is for everyone. [phone] is Android phones and tablets, [tv] the TVs (LG, Samsung,
 * Android TV, Fire TV), [desktop] the computers; "[phone,tv]" names several. A device sees only its own.
 * tools/release_notes.py groups the same tags for the release page. Pure: tests/test_whatsnew_notes.c. */
#ifndef SNTV_WHATSNEW_NOTES_H
#define SNTV_WHATSNEW_NOTES_H
#include <stddef.h>
#include <string.h>

enum { NOTE_PHONE = 1, NOTE_TV = 2, NOTE_DESKTOP = 4, NOTE_ALL = 7 };

typedef struct { char title[160]; char body[900]; int mask; } Note;

/* "[phone,tv] rest" -> the devices it names (NOTE_ALL when untagged), and where the rest starts */
static inline int notes_tags(const char *s, const char **rest)
{
    *rest = s;
    if (*s != '[') return NOTE_ALL;
    const char *end = strchr(s, ']');
    if (!end || end - s > 40) return NOTE_ALL;
    int mask = 0;
    for (const char *p = s + 1; p < end;) {
        const char *q = p;
        while (q < end && *q != ',') q++;
        size_t n = (size_t)(q - p);
        while (n && *p == ' ') { p++; n--; }
        while (n && p[n - 1] == ' ') n--;
        if (n == 5 && !strncmp(p, "phone", 5)) mask |= NOTE_PHONE;
        else if (n == 6 && !strncmp(p, "mobile", 6)) mask |= NOTE_PHONE;
        else if (n == 2 && !strncmp(p, "tv", 2)) mask |= NOTE_TV;
        else if (n == 7 && !strncmp(p, "desktop", 7)) mask |= NOTE_DESKTOP;
        else if (n == 3 && !strncmp(p, "all", 3)) mask |= NOTE_ALL;
        else return NOTE_ALL;   /* not a tag: a bracket in the text */
        p = q + (q < end);
    }
    if (!mask) return NOTE_ALL;
    end++;
    while (*end == ' ') end++;
    *rest = end;
    return mask;
}

/* One note out of "**Title.** body": the bold lead is the title, markup removed. */
static inline void notes_split(const char *raw, Note *n)
{
    n->title[0] = n->body[0] = 0;
    const char *body = raw;
    if (raw[0] == '*' && raw[1] == '*') {
        const char *end = strstr(raw + 2, "**");
        if (end) {
            size_t len = (size_t)(end - raw - 2);
            if (len >= sizeof n->title) len = sizeof n->title - 1;
            size_t o = 0;
            for (size_t i = 0; i < len; i++) if (raw[2 + i] != '`') n->title[o++] = raw[2 + i];
            n->title[o] = 0;
            body = end + 2;
        }
    }
    while (*body == ' ') body++;
    size_t o = 0;
    for (const char *p = body; *p && o + 1 < sizeof n->body; p++) {
        if (*p == '`') continue;
        if (p[0] == '*' && p[1] == '*') { p++; continue; }
        n->body[o++] = *p;
    }
    n->body[o] = 0;
}

/* The notes in `text` (a section's lines) that device `device` (NOTE_PHONE, NOTE_TV or NOTE_DESKTOP) should
 * see, up to `max`; how many there are. */
static inline int notes_parse(const char *text, int device, Note *out, int max)
{
    int count = 0;
    char raw[1200];
    size_t have = 0;
    int mask = NOTE_ALL, open = 0;
#define FLUSH() do { \
        if (open && (mask & device) && count < max) { raw[have] = 0; notes_split(raw, &out[count]); out[count].mask = mask; count++; } \
        have = 0; open = 0; } while (0)
    for (const char *p = text; *p;) {
        size_t len = strcspn(p, "\n");
        const char *line = p;
        p += len + (p[len] == '\n');
        while (len && (*line == ' ' || *line == '\t')) { line++; len--; }
        if (!len) continue;
        int starts = len > 1 && line[0] == '-' && line[1] == ' ';
        if (starts || !open) {
            FLUSH();
            if (starts) { line += 2; len -= 2; }
            char first[1200];
            size_t fl = len < sizeof first - 1 ? len : sizeof first - 1;
            memcpy(first, line, fl); first[fl] = 0;
            const char *rest;
            mask = notes_tags(first, &rest);
            line = line + (rest - first);
            len -= (size_t)(rest - first);
            open = 1;
        }
        if (have && have + 1 < sizeof raw) raw[have++] = ' ';
        size_t take = len < sizeof raw - 1 - have ? len : sizeof raw - 1 - have;
        memcpy(raw + have, line, take);
        have += take;
    }
    FLUSH();
#undef FLUSH
    return count;
}

#endif
