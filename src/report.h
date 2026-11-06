#ifndef SNTV_REPORT_H
#define SNTV_REPORT_H
#include <stddef.h>

/* Settings > About > Report a problem: this session's log, with anything private taken out, as a
 * file for the user to attach, and the issue page opened with the facts a report needs. */

/* One line with addresses' queries, keys, tokens, long random-looking strings and e-mail
 * addresses replaced by [removed] (out is as long as in at most). */
void report_redact(const char *in, char *out, size_t n);

/* Writes Cuelight-report.txt (the Desktop, else the data folder; SNTV_REPORT_DIR overrides) and opens
 * the issue page. Returns 0 and the file's path, or -1. */
int report_problem(const char *backend, char *path_out, size_t n);   /* backend: the video player's name, for the facts */

#endif
