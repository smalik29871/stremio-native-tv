#include "lang.h"
#include <string.h>
#include <strings.h>

const char *const LANG_CODES[] = { "", "eng", "spa", "por", "fra", "deu", "ita", "rus", "tur", "ara", "hin", "urd",
                                   "jpn", "kor", "zho", "pol", "nld", NULL };
/* Latin script only: the bundled font atlas covers Latin, so other scripts use English names */
const char *const LANG_NAMES[] = { "Off", "English", "Espa\xC3\xB1ol", "Portugu\xC3\xAAs", "Fran\xC3\xA7" "ais", "Deutsch", "Italiano",
                                   "Russian", "T\xC3\xBCrk\xC3\xA7" "e", "Arabic", "Hindi", "Urdu",
                                   "Japanese", "Korean", "Chinese", "Polski", "Nederlands", NULL };

/* canonical, 639-1, 639-2/B, English name */
static const char *const ALIASES[][4] = {
    { "eng", "en", "eng", "english" },   { "spa", "es", "spa", "spanish" },   { "por", "pt", "por", "portuguese" },
    { "fra", "fr", "fre", "french" },    { "deu", "de", "ger", "german" },    { "ita", "it", "ita", "italian" },
    { "rus", "ru", "rus", "russian" },   { "tur", "tr", "tur", "turkish" },   { "ara", "ar", "ara", "arabic" },
    { "hin", "hi", "hin", "hindi" },     { "urd", "ur", "urd", "urdu" },      { "jpn", "ja", "jpn", "japanese" },
    { "kor", "ko", "kor", "korean" },    { "zho", "zh", "chi", "chinese" },   { "pol", "pl", "pol", "polish" },
    { "nld", "nl", "dut", "dutch" },     { "swe", "sv", "swe", "swedish" },   { "nor", "no", "nor", "norwegian" },
    { "dan", "da", "dan", "danish" },    { "fin", "fi", "fin", "finnish" },   { "ell", "el", "gre", "greek" },
    { "heb", "he", "heb", "hebrew" },    { "ces", "cs", "cze", "czech" },     { "hun", "hu", "hun", "hungarian" },
    { "ron", "ro", "rum", "romanian" },  { "ukr", "uk", "ukr", "ukrainian" }, { "vie", "vi", "vie", "vietnamese" },
    { "tha", "th", "tha", "thai" },      { "ind", "id", "ind", "indonesian" }, { "msa", "ms", "may", "malay" },
    { "fas", "fa", "per", "persian" },   { "ben", "bn", "ben", "bengali" },   { "tam", "ta", "tam", "tamil" },
    { "tel", "te", "tel", "telugu" },    { "bul", "bg", "bul", "bulgarian" }, { "hrv", "hr", "hrv", "croatian" },
    { "srp", "sr", "srp", "serbian" },   { "slk", "sk", "slo", "slovak" },
};

int lang_count(void)
{
    int n = 0;
    while (LANG_CODES[n]) n++;
    return n;
}

static int is_brazilian(const char *code)
{
    return code && (!strcasecmp(code, "pob") || !strcasecmp(code, "pt-br") || !strcasecmp(code, "pt_br") || !strcasecmp(code, "ptbr") || !strcasecmp(code, "por-br"));
}

const char *lang_canonical(const char *code)
{
    if (!code || !*code) return NULL;
    if (is_brazilian(code)) return "por";
    char c[24];
    size_t n = 0;
    /* "en-US", "pt_BR", "eng (forced)" -> the language part */
    while (code[n] && n < sizeof c - 1 && code[n] != '-' && code[n] != '_' && code[n] != ' ' && code[n] != '(') { c[n] = code[n]; n++; }
    c[n] = 0;
    for (size_t i = 0; i < sizeof ALIASES / sizeof ALIASES[0]; i++)
        for (int k = 0; k < 4; k++)
            if (strcasecmp(c, ALIASES[i][k]) == 0) return ALIASES[i][0];
    return NULL;
}

int lang_known(const char *code)
{
    if (!code || !*code) return 0;
    static const char *const NONE[] = { "und", "mis", "zxx", "mul", "unknown", "(null)", NULL };
    for (int i = 0; NONE[i]; i++) if (strcasecmp(code, NONE[i]) == 0) return 0;
    return 1;
}

int lang_same(const char *a, const char *b)
{
    if (!lang_known(a) || !lang_known(b)) return 0;
    const char *ca = lang_canonical(a), *cb = lang_canonical(b);
    if (ca && cb) return ca == cb;
    return strcasecmp(a, b) == 0;
}

int lang_index(const char *code)
{
    const char *c = lang_canonical(code);
    if (!c) return 0;
    for (int i = 1; LANG_CODES[i]; i++) if (strcmp(LANG_CODES[i], c) == 0) return i;
    return 0;
}

const char *lang_display(const char *code)
{
    if (is_brazilian(code)) return "Portugu\xC3\xAAs (Brasil)";
    int i = lang_index(code);
    return i ? LANG_NAMES[i] : code;
}
