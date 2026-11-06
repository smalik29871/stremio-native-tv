#include "report.h"
#include "app.h"
#include "log.h"
#include "store.h"
#include "platform/platform.h"
#include <SDL.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define ISSUES "https://github.com/smalik29871/cuelight-releases/issues/new"

static int token_char(unsigned char c) { return isalnum(c) || c == '_' || c == '-' || c == '+' || c == '/' || c == '=' || c == '.'; }
static int word_char(unsigned char c) { return isalnum(c) || c == '.' || c == '_' || c == '-' || c == '+'; }

void report_redact(const char *in, char *out, size_t n)
{
    size_t o = 0;
#define PUT(c) do { if (o + 1 < n) out[o++] = (char)(c); } while (0)
#define PUTS(s) do { for (const char *q_ = (s); *q_; q_++) PUT(*q_); } while (0)
    for (const char *p = in; *p; ) {
        /* a query string: the address stays, what follows ? goes */
        if (*p == '?' && p[1] && !isspace((unsigned char)p[1])) {
            PUTS("?[removed]");
            p++;
            while (*p && !isspace((unsigned char)*p) && *p != '"' && *p != '\\' && *p != ')') p++;
            continue;
        }
        /* an e-mail address */
        if (*p == '@' && o > 0 && word_char((unsigned char)out[o - 1]) && isalnum((unsigned char)p[1])) {
            const char *e = p + 1;
            while (word_char((unsigned char)*e)) e++;
            if (memchr(p + 1, '.', (size_t)(e - p - 1))) {
                while (o > 0 && word_char((unsigned char)out[o - 1])) o--;
                PUTS("[removed]");
                p = e;
                continue;
            }
        }
        /* a word that is mostly one long token: 28+ letters and digits, or name=value with a long value in an address */
        if (token_char((unsigned char)*p)) {
            const char *e = p;
            int alnum = 0;
            while (token_char((unsigned char)*e)) { alnum += isalnum((unsigned char)*e) != 0; e++; }
            const char *eq = memchr(p, '=', (size_t)(e - p));
            if (eq && eq > p) {   /* name=value in an address: a long value goes, the rest of the address stays */
                const char *v = eq + 1;
                while (v < e && *v != '/') v++;
                if ((size_t)(v - eq - 1) >= 12) { for (const char *q = p; q <= eq; q++) PUT(*q); PUTS("[removed]"); p = v; continue; }
            }
            if (alnum >= 28 && (size_t)(e - p) <= 400 && !memchr(p, '/', (size_t)(e - p))) { PUTS("[removed]"); p = e; continue; }
            for (; p < e; p++) PUT(*p);
            continue;
        }
        PUT(*p);
        p++;
    }
    if (n) out[o < n ? o : n - 1] = 0;
#undef PUT
#undef PUTS
}

static void pct(const char *s, char *out, size_t n)
{
    size_t o = 0;
    for (; *s && o + 4 < n; s++) {
        unsigned char c = (unsigned char)*s;
        if (isalnum(c) || c == '-' || c == '_' || c == '.') out[o++] = (char)c;
        else o += (size_t)snprintf(out + o, n - o, "%%%02X", c);
    }
    out[o] = 0;
}

int report_problem(const char *backend, char *path_out, size_t n)
{
    LogLine *lines = malloc(sizeof *lines * LOG_RING_LINES);
    if (!lines) return -1;
    int count = log_snapshot(lines, LOG_RING_LINES);
    char sys[300] = "";
    log_system_info(sys, sizeof sys);

    const char *own = getenv("SNTV_REPORT_DIR");
    char dir[1100] = "";
    if (own && *own) snprintf(dir, sizeof dir, "%s", own);
    else {
        const char *home = getenv("USERPROFILE");
        if (!home || !*home) home = getenv("HOME");
        char desk[1100];
        struct stat st;
        if (home && *home) { snprintf(desk, sizeof desk, "%s/Desktop", home); if (stat(desk, &st) == 0) snprintf(dir, sizeof dir, "%s", desk); }
        if (!dir[0]) snprintf(dir, sizeof dir, "%s", store_dir());
    }
    char path[1200];
    snprintf(path, sizeof path, "%s/Cuelight-report.txt", dir);
    FILE *f = fopen(path, "wb");
    if (!f) { free(lines); return -1; }
    fprintf(f, "Cuelight %s\n%s\n%s\nlog of this session, private details removed (addresses' queries, keys, tokens, e-mail addresses)\n---\n", SNTV_APP_VERSION, backend, sys);
    for (int i = 0; i < count; i++) {
        char clean[LOG_LINE_MAX + 40];
        report_redact(lines[i].text, clean, sizeof clean);
        fprintf(f, "%c %s\n", lines[i].level, clean);
    }
    fclose(f);
    free(lines);
    if (path_out) snprintf(path_out, n, "%s", path);

    char body[600], enc[1900], url[2200];
    snprintf(body, sizeof body, "What happened:\n\n\nCuelight %s, %s\n%s\n\nI attached Cuelight-report.txt (it is in %s).", SNTV_APP_VERSION, backend, sys, dir);
    pct(body, enc, sizeof enc);
    snprintf(url, sizeof url, ISSUES "?title=%%5BReport%%5D%%20&body=%s", enc);
    LOGI("report: %d log lines written to %s", count, path);
    if (!getenv("SNTV_REPORT_DIR")) SDL_OpenURL(url);   /* tests only write the file */
    return 0;
}
