#ifndef SNTV_VAULT_H
#define SNTV_VAULT_H

/* Secrets on disk. The sign-in key and the keys of the services are written to settings.json
 * sealed for the user who owns them (Windows: DPAPI, the same protection the browser's saved
 * passwords have) and are plain again in memory. Elsewhere there is no sealing (the file's
 * permissions are all there is); SNTV_FAKE_VAULT=1 seals with a reversible stand-in, for tests. */

int   vault_available(void);                 /* 1 when secrets can be sealed here */
char *vault_seal(const char *plain);         /* malloc'd sealed text, NULL when it cannot be */
char *vault_open(const char *sealed);        /* malloc'd plain text, NULL when it is not sealed for this user */
int   vault_is_sealed(const char *text);
int   vault_is_secret_key(const char *name); /* names that hold a secret: ...Key, ...Token, ...Secret */

#endif
