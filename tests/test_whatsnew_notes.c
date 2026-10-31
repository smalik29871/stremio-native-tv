/* The notes of a changelog section, per device (src/whatsnew_notes.h). */
#include "whatsnew_notes.h"
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(c) do { if (!(c)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static const char *SECTION =
    "- **Backup in Accounts.** Settings > Accounts, beside `Sync`. Save and\n"
    "  restore use the phone's own sheet.\n"
    "- [phone] **Flicks carry further.** A flick leaves faster.\n"
    "- [tv] **A calmer Home.** Fewer layers.\n"
    "- [phone,desktop] **Real refresh rate.** Paces to the screen.\n"
    "- [desktop] No bold lead here, just a sentence.\n"
    "- [not a tag] kept whole, a bracket in the text.\n";

int main(void)
{
    Note n[16];
    int c = notes_parse(SECTION, NOTE_PHONE, n, 16);
    CHECK(c == 4);   /* everyone's, the phone's, the phone+desktop one, the bracket text */
    CHECK(!strcmp(n[0].title, "Backup in Accounts."));
    CHECK(!strcmp(n[0].body, "Settings > Accounts, beside Sync. Save and restore use the phone's own sheet."));   /* continuation joined, markup gone */
    CHECK(n[0].mask == NOTE_ALL);
    CHECK(!strcmp(n[1].title, "Flicks carry further.") && n[1].mask == NOTE_PHONE);
    CHECK(!strcmp(n[2].title, "Real refresh rate.") && n[2].mask == (NOTE_PHONE | NOTE_DESKTOP));
    CHECK(!strcmp(n[3].body, "[not a tag] kept whole, a bracket in the text.") && n[3].title[0] == 0);

    c = notes_parse(SECTION, NOTE_TV, n, 16);
    CHECK(c == 3);   /* everyone's, the TV's, the bracket text */
    CHECK(!strcmp(n[1].title, "A calmer Home."));

    c = notes_parse(SECTION, NOTE_DESKTOP, n, 16);
    CHECK(c == 4);
    CHECK(!strcmp(n[1].title, "Real refresh rate."));
    CHECK(!strcmp(n[2].body, "No bold lead here, just a sentence.") && n[2].title[0] == 0);

    CHECK(notes_parse(SECTION, NOTE_PHONE, n, 2) == 2);   /* the room given */
    CHECK(notes_parse("", NOTE_TV, n, 16) == 0);
    CHECK(notes_parse("- [phone] **Only phones.** Nothing for TVs.\n", NOTE_TV, n, 16) == 0);   /* nothing for this device: nothing to show */

    const char *rest;
    CHECK(notes_tags("[phone, tv] x", &rest) == (NOTE_PHONE | NOTE_TV) && !strcmp(rest, "x"));   /* spaces allowed */
    CHECK(notes_tags("[mobile] x", &rest) == NOTE_PHONE);
    CHECK(notes_tags("[all] x", &rest) == NOTE_ALL);
    CHECK(notes_tags("plain", &rest) == NOTE_ALL && !strcmp(rest, "plain"));

    /* a changelog item from before tags existed: a title and a long body, untouched */
    c = notes_parse("- **AI search finds as many as you ask for.** Ask for \"top 50 food films\" and it now asks the\n  addon in batches.\n", NOTE_PHONE, n, 16);
    CHECK(c == 1 && !strcmp(n[0].title, "AI search finds as many as you ask for.") && strstr(n[0].body, "in batches."));

    if (failures) return 1;
    puts("whatsnew notes checks passed");
    return 0;
}
