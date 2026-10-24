#ifndef SNTV_SETTINGS_PREVIEW_H
#define SNTV_SETTINGS_PREVIEW_H

/* Small live drawings of what a setting does, shown beside the Settings rows.
 * Each reads the current value from g_app, so it changes as the value does. */
typedef enum {
    PV_NONE, PV_THEME, PV_HOME_STYLE, PV_SUBS, PV_CLOCK, PV_CARD_DETAILS, PV_HIDE_WATCHED, PV_BLUR,
    PV_FX, PV_OLED, PV_RES_CAP, PV_QUALITY, PV_SKIP, PV_AUTOPLAY, PV_PIP, PV_SCRUB, PV_AMBIENT,
    PV_BREAK, PV_LANG_AUDIO, PV_LANG_SUBS, PV_TRANSITIONS, PV_MEMORY, PV_SUBS_SYNC, PV_PREFLIGHT,
    PV_AUTO_REC, PV_ADDON_WAIT, PV_SRC_AUDIO, PV_P2P, PV_UPDATE, PV_PARENTAL, PV_ADVISORY, PV_LETTERBOX, PV_COUNT
} PreviewKind;

/* Draws the preview in the box; nothing for PV_NONE. */
void settings_preview_draw(PreviewKind kind, float x, float y, float w, float h);

#endif
