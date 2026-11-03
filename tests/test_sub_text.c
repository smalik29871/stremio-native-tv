/* The one cleaner for subtitle text (src/sub_text.h). */
#include "sub_text.h"
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(c) do { if (!(c)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
/* in -> out (and whether it asked for the top) */
#define IS(in, want, want_top) do { char b[400]; snprintf(b, sizeof b, "%s", in); int t = subtext_clean(b); \
    if (strcmp(b, want) || t != (want_top)) { failures++; fprintf(stderr, "FAIL %s:%d: \"%s\" -> \"%s\" (top %d), wanted \"%s\" (top %d)\n", __FILE__, __LINE__, in, b, t, want, want_top); } } while (0)

int main(void)
{
    IS("{\\an8}On August 14", "On August 14", 1);                      /* the one on the screenshot */
    IS("{\\an8} On August 14", "On August 14", 1);
    IS("{\\an7}Left", "Left", 1);
    IS("{\\an9}Right", "Right", 1);
    IS("{\\an2}Bottom", "Bottom", 0);                                   /* bottom centre: where it already goes */
    IS("{\\i1}Italic{\\i0} plain", "Italic plain", 0);
    IS("{\\pos(192,10)}Over a sign", "Over a sign", 0);
    IS("{\\an8}{\\i1}Both{\\i0}", "Both", 1);
    IS("{ \\an8 }Spaces inside", "Spaces inside", 1);
    IS("{\\\\an8}Doubled backslash", "Doubled backslash", 1);
    IS("{\\an8 On August, with no end", "On August, with no end", 1);   /* the closing brace lost */
    IS("{an8}Lost backslash", "Lost backslash", 1);
    IS("{AN8}Upper case", "Upper case", 1);
    IS("&#123;\\an8&#125;Entities", "Entities", 1);
    IS("&lbrace;\\an8&rbrace;Named", "Named", 1);
    IS("\xEF\xBD\x9B\\an8\xEF\xBD\x9D" "Fullwidth", "Fullwidth", 1);
    IS("<i>Italic</i> and <font color=\"#ff0\">colour</font> and <c.yellow>cue</c>", "Italic and colour and cue", 0);
    IS("{\\an8}<i>Both kinds</i>", "Both kinds", 1);
    IS("\xE3\x80\x88" "font color=\"#00ffff\"" "\xE3\x80\x89" ":::::[GWC]:::::" "\xE3\x80\x88" "/font" "\xE3\x80\x89", ":::::[GWC]:::::", 0);   /* the one on the photo */
    IS("&lt;font color=\"#00ffff\"&gt;hi&lt;/font&gt; there", "hi there", 0);       /* a tag written escaped */
    IS("\xEF\xBC\x9Ci\xEF\xBC\x9Ex\xEF\xBC\x9C/i\xEF\xBC\x9E", "x", 0);                /* fullwidth signs */
    IS("\xE2\x80\xB9" "b" "\xE2\x80\xBA" "bold" "\xE2\x80\xB9/b" "\xE2\x80\xBA", "bold", 0);
    IS("I \xE2\x80\xB9" "3 you", "I \xE2\x80\xB9" "3 you", 0);                           /* a sign that opens no tag stays */
    IS("&lt;3 and &lt;&gt;", "&lt;3 and &lt;&gt;", 0);
    IS("First\\NSecond\\nThird", "First\nSecond\nThird", 0);
    IS("A\\hB", "A B", 0);
    IS("{\\p1}m 0 0 l 10 0 10 10 0 10{\\p0}Visible", "Visible", 0);   /* an ASS drawing is not text */
    IS("{sighs} Fine.", "{sighs} Fine.", 0);                          /* words in braces stay */
    IS("{music playing}", "{music playing}", 0);
    IS("I <3 you", "I <3 you", 0);                                    /* a lone < stays */
    IS("1 < 2 and 3 > 2", "1 < 2 and 3 > 2", 0);
    IS("&amp; stays as the other cleaner left it", "&amp; stays as the other cleaner left it", 0);
    IS("  \n{\\an8}  Padded  \n ", "Padded", 1);                       /* the edges */
    IS("{\\an8}", "", 1);                                             /* nothing left */
    IS("", "", 0);
    IS("Caf\xC3\xA9 {\\an8}cr\xC3\xA8me", "Caf\xC3\xA9 cr\xC3\xA8me", 1);   /* UTF-8 intact */
    IS("{\\fnArial}{\\fs20}{\\c&HFFFFFF&}Styled", "Styled", 0);
    IS("Line one\r\nLine two", "Line one\nLine two", 0);
    IS("{\\an8}{\\an8}Twice", "Twice", 1);
    IS("{{\\an8}}Nested", "{}Nested", 1);                             /* the tag goes, the stray braces are text */
    char big[3000]; memset(big, 'x', sizeof big - 1); big[sizeof big - 1] = 0; memcpy(big, "{\\an8}", 6);
    CHECK(subtext_clean(big) == 1 && strlen(big) == sizeof big - 1 - 6);
    if (failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
    puts("sub_text: ok");
    return 0;
}
