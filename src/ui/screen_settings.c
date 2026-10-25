/* Settings: categories down the left, their rows on the right. Left/Right change a
 * choice, OK toggles or opens, Back returns to the categories. Home rows can be
 * reordered and hidden; addons can be reordered (synced to the account). */
#include "../app.h"
#include "../depth.h"
#include "../breaks.h"
#include "../tmdb.h"
#include "../ai.h"
#include "../http.h"
#include "../memwatch.h"
#include "../phone.h"
#include "../youtube.h"
#include "../torrent.h"
#include "qrcodegen.h"
#include "../suntimes.h"
#include "../torbox.h"
#include "../image.h"
#include "../lang.h"
#include "../log.h"
#include "../store.h"
#include "../tracking.h"
#include "../opensubs.h"
#include "../platform/platform.h"
#include "../video/video.h"
#include "font.h"
#include "keyboard.h"
#include "rail.h"
#include "theme.h"
#include "widgets.h"
#include "screen_options.h"
#include "settings_preview.h"
#include "clockface.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef SNTV_PLATFORM_WEBOS
#define TV_KB_DEFAULT 1   /* LG's keyboard for Search, unless turned off */
#else
#define TV_KB_DEFAULT 0
#endif

enum { CAT_ACCOUNT, CAT_PLAYBACK, CAT_BREAKS, CAT_LANG, CAT_SOURCES, CAT_INTERFACE, CAT_ROWS, CAT_ADDONS, CAT_PARENTAL, CAT_ABOUT, CAT_COUNT };
static const char *const CAT_NAMES[CAT_COUNT] = { "Accounts", "Playback", "Take breaks", "Audio & subtitles", "Sources", "Interface", "Home rows", "Addons", "Parental", "About" };
static const char *const CAT_DESC[CAT_COUNT] = {
    "Every account and key in one place: sync, streaming, subtitles, artwork, AI.",
    "What happens while a video plays.",
    "Pause for what matters: prayer times computed for your place, a stretch, bedtime, or your own times. A banner warns first; playback continues by itself.",
    "Preferred languages and how subtitles look.",
    "How sources are ranked and checked for this TV. P2P fallback plays a torrent here when TorBox has no cached copy (peers see your IP while it plays) and goes back to TorBox once it has one.",
    "The look of the app.",
    "Which rows Home shows, and in what order. OK shows or hides a row, Left/Right moves it.",
    "Installed addons, in the order they answer. Left/Right moves an addon; the order is kept in your account.",
    "A PIN that keeps Settings closed and adult addons out of the catalogs.",
    "What " SNTV_APP_NAME " is, what is new, updates, logs and setup." };
static const Icon CAT_ICONS[CAT_COUNT] = { ICON_USER, ICON_PLAY, ICON_CLOCK, ICON_SUBS, ICON_SWAP, ICON_PALETTE, ICON_LIST, ICON_LIBRARY, ICON_LOCK, ICON_INFO };

enum { IT_ACCOUNT, IT_SERVER, IT_AUTOPLAY, IT_SKIP, IT_CLOCK, IT_SCRUB, IT_PREF_AUDIO, IT_PREF_SUBS, IT_SUBS_WHEN, IT_SUBS_SIZE, IT_SUBS_STYLE, IT_SUBS_COLOUR,
       IT_QUALITY, IT_RES_CAP, IT_PREFLIGHT, IT_AUTO_REC, IT_HOME_STYLE, IT_HIDE_WATCHED, IT_FX, IT_OLED, IT_THEME, IT_ACCENT, IT_CARD_DETAILS, IT_AUTO_TRAILER, IT_AMBIENT, IT_BLUR,
       IT_INSTALL, IT_DIAG, IT_STORAGE, IT_SETUP, IT_WHATSNEW, IT_ABOUT_APP, IT_THUMBS, IT_PIN, IT_PIN_SETTINGS, IT_HIDE_ADULT, IT_TMDB, IT_MEMORY, IT_TRANSITIONS, IT_PIP, IT_SUBS_SYNC, IT_SUBS_FILES, IT_BUFFER, IT_TV_KB, IT_SUBS_XL, IT_BR_VIDEO, IT_ADDON_WAIT, IT_SRC_AUDIO, IT_SRC_SIZE, IT_SRC_HDR, IT_SRC_CACHED, IT_SRC_EXCLUDE, IT_TVCAP, IT_TRAKT, IT_SIMKL, IT_MDBLIST, IT_OS_KEY, IT_OS_LOGIN, IT_PLAY_LINK, IT_BACKUP, IT_P2P, IT_UPDATE, IT_AUTO_UPDATE, IT_AI_PROVIDER, IT_AI_KEY, IT_AI_BASE, IT_AI_MODEL,
       IT_BR_TEST, IT_BR_ON, IT_BR_SCHED, IT_BR_METHOD, IT_BR_ASR, IT_BR_LOCATION, IT_BR_DETECT, IT_BR_TODAY, IT_BR_LEAD, IT_BR_LEN, IT_BR_SOUND, IT_BR_AUDIO, IT_BR_END,
       IT_BR_HEALTH, IT_BR_HEALTH_LEN, IT_BR_BED, IT_BR_CUSTOM1, IT_BR_CUSTOM2, IT_BR_CUSTOM3, IT_BR_WHERE, IT_BG, IT_AMBIENT_STYLE, IT_FIRE_VIDEO, IT_TORBOX, IT_TORBOX_FILES, IT_FIRE_TORBOX, IT_BR_TORBOX, IT_ASK_PROFILE, IT_ISLAND_STYLE, IT_CLOCK_FACE, IT_NIGHT, IT_PALETTE, IT_AMBIENT_DIM, IT_AMBIENT_OFF, IT_ADVISORY, IT_LETTERBOX, IT_DEPTH, IT_SYNC_DEVICES,
       IT_SEC_BASE = 900, IT_ROWS_BASE = 1000, IT_ADDON_BASE = 2000, IT_ACCT_BASE = 3000 };
/* Section headings inside a category: rows of their own that focus skips. */
enum { SEC_STREMIO, SEC_SERVER, SEC_EPISODES, SEC_PLAYER, SEC_LANGS, SEC_SUBS_LOOK, SEC_SUBS_SMART, SEC_RANKING, SEC_STARTING, SEC_FALLBACK,
       SEC_LOOK, SEC_HOME, SEC_MOTION, SEC_SYSTEM, SEC_BR_MAIN, SEC_BR_PLACE, SEC_BR_ALERT, SEC_BR_MORE, SEC_META, SEC_AI, SEC_UPDATES, SEC_HELP, SEC_CUELIGHT, SEC_SUBS_ACCT, SEC_COUNT };
static const char *const SEC_NAMES[SEC_COUNT] = { "Sync", "Streaming", "Episodes", "Player", "Languages", "Subtitle look", "Smart subtitles",
    "Ranking", "Starting playback", "Fallback", "Look", "Home", "Motion", "Screen and memory", "Breaks", "Your place", "Reminder and sound",
    "Stretch, bedtime and your own times", "Artwork and ratings", "AI assistant", "Updates", "Help and setup", SNTV_APP_NAME, "Subtitles" };
#define IS_SEC(id) ((id) >= IT_SEC_BASE && (id) < IT_ROWS_BASE)
enum { EDIT_NONE, EDIT_SERVER, EDIT_INSTALL, EDIT_THUMBS, EDIT_TMDB, EDIT_LOCATION, EDIT_BR_AUDIO, EDIT_BR_END, EDIT_AI_KEY, EDIT_AI_BASE, EDIT_AI_MODEL, EDIT_SEARCH, EDIT_EXCLUDE, EDIT_PLAY_LINK, EDIT_BACKUP, EDIT_TRAKT_APP, EDIT_SIMKL_APP, EDIT_MDBLIST, EDIT_OS_KEY, EDIT_OS_LOGIN, EDIT_FIRE_VIDEO, EDIT_TORBOX };
enum { MAX_ROWS = 160 };
#define ROW_H 100.0f
enum { HIT_CAT = 0x1000, HIT_FIND = 0x2000, HIT_LEFT = 0x3000, HIT_BACK = 0x4000, HIT_DONE = 0x4001 };   /* touch targets besides the rows (their index) */
static int s_hit_row;   /* the row being drawn, for its choice's back arrow */
#define SEC_H 70.0f
/* Choosing a category the rows start at COL_X0; inside one the category list shrinks
 * to its icons, the rows move left to COL_X1..COL_R1 and a preview panel takes the
 * right side from PREV_X. */
#define COL_X0 560.0f
#define COL_X1 250.0f
#define COL_R1 1170.0f
#define PREV_X 1226.0f
#define ROWS_TOP (SNTV_PHONE ? 150.0f : 230.0f)   /* a phone: under the page header */
#define PH_CAT_Y 254.0f   /* ...and its list of categories */
#define PH_CAT_H 124.0f

typedef struct {
    int cat;                 /* the category shown */
    int zone;                /* 0 categories, 1 rows */
    int focus;               /* row index within the category */
    int confirm_remove;      /* addon index awaiting the second OK */
    int edit;
    Keyboard kb;
    float scroll, scroll_target, scroll_max;
    UiPan pan;   /* a finger on the rows */
    float cat_y; UiPan cat_pan;   /* upright on a phone: the list of categories */
    float pane_t; int pane_cat;   /* rows slide in when the category changes */
    float wide_t;                 /* 0 choosing a category .. 1 inside one (preview shown) */
    int cat_focus[CAT_COUNT];     /* the row each category had focused, for coming back */
    int search_foc;               /* the "Find a setting" field beside the heading has the focus */
    int panel_id; float panel_t;  /* the preview's setting, and how far its drawing has come in */
    uint8_t qr[qrcodegen_BUFFER_LEN_MAX]; int qr_ready; char phone_url[96];   /* type on your phone */
    HomeRowDef rows[MAX_ROWS]; int n_rows; int rows_seen;
    char accounts[8][128]; int n_accounts;
} Settings;

static int rows_for(Settings *st, int cat, int *ids, int max)
{
    int n = 0;
#define SEC(k) (ids[n++] = IT_SEC_BASE + (k))
    switch (cat) {
    case CAT_ACCOUNT:   /* every account and key, grouped by what it does */
        SEC(SEC_STREMIO);
        ids[n++] = IT_ACCOUNT;
        st->n_accounts = app_saved_accounts(st->accounts, 8);
        for (int i = 0; i < st->n_accounts && n < max - 3; i++) ids[n++] = IT_ACCT_BASE + i;
        if (st->n_accounts && g_app.auth_key) ids[n++] = IT_ASK_PROFILE;
        ids[n++] = IT_SYNC_DEVICES;
        ids[n++] = IT_TRAKT; ids[n++] = IT_SIMKL;
        SEC(SEC_SERVER); ids[n++] = IT_TORBOX; if (torbox_enabled()) ids[n++] = IT_TORBOX_FILES; ids[n++] = IT_SERVER;
        SEC(SEC_SUBS_ACCT); ids[n++] = IT_OS_KEY; if (opensubs_enabled()) ids[n++] = IT_OS_LOGIN;
        SEC(SEC_META); ids[n++] = IT_TMDB; ids[n++] = IT_MDBLIST;
        SEC(SEC_AI); ids[n++] = IT_AI_PROVIDER;
        if (g_app.ai_provider) { ids[n++] = IT_AI_KEY; if (g_app.ai_provider == AI_CUSTOM) ids[n++] = IT_AI_BASE; ids[n++] = IT_AI_MODEL; }
        break;
    case CAT_PLAYBACK:
        SEC(SEC_EPISODES); ids[n++] = IT_AUTOPLAY; ids[n++] = IT_SKIP;
        SEC(SEC_PLAYER); ids[n++] = IT_PIP; ids[n++] = IT_CLOCK; if (g_app.clock_mode != 2) ids[n++] = IT_ISLAND_STYLE; ids[n++] = IT_SCRUB; ids[n++] = IT_THUMBS; ids[n++] = IT_LETTERBOX; ids[n++] = IT_NIGHT; ids[n++] = IT_ADVISORY; ids[n++] = IT_PLAY_LINK; ids[n++] = IT_BUFFER;
        break;
    case CAT_PARENTAL: ids[n++] = IT_PIN; if (g_app.parental_pin[0]) { ids[n++] = IT_PIN_SETTINGS; ids[n++] = IT_HIDE_ADULT; } break;
    case CAT_LANG:
        SEC(SEC_LANGS); ids[n++] = IT_PREF_AUDIO; ids[n++] = IT_PREF_SUBS; ids[n++] = IT_SUBS_WHEN;
        SEC(SEC_SUBS_LOOK); ids[n++] = IT_SUBS_SIZE; ids[n++] = IT_SUBS_STYLE; ids[n++] = IT_SUBS_COLOUR;
        SEC(SEC_SUBS_SMART); ids[n++] = IT_SUBS_SYNC; if (ai_enabled()) ids[n++] = IT_SUBS_XL; ids[n++] = IT_SUBS_FILES;
        break;
    case CAT_SOURCES:
        SEC(SEC_RANKING); ids[n++] = IT_QUALITY; ids[n++] = IT_RES_CAP; ids[n++] = IT_SRC_AUDIO;
        ids[n++] = IT_SRC_SIZE; ids[n++] = IT_SRC_HDR; ids[n++] = IT_SRC_CACHED; ids[n++] = IT_SRC_EXCLUDE; ids[n++] = IT_TVCAP;
        SEC(SEC_STARTING); ids[n++] = IT_PREFLIGHT; ids[n++] = IT_AUTO_REC; ids[n++] = IT_ADDON_WAIT;
        SEC(SEC_FALLBACK); ids[n++] = IT_P2P;
        break;
    case CAT_INTERFACE:
        SEC(SEC_LOOK); ids[n++] = IT_THEME; ids[n++] = IT_PALETTE; ids[n++] = IT_ACCENT; ids[n++] = IT_BG; ids[n++] = IT_OLED;
        SEC(SEC_HOME); ids[n++] = IT_HOME_STYLE; ids[n++] = IT_CARD_DETAILS; ids[n++] = IT_AUTO_TRAILER; ids[n++] = IT_HIDE_WATCHED; ids[n++] = IT_BLUR;
        SEC(SEC_MOTION); ids[n++] = IT_FX; ids[n++] = IT_TRANSITIONS; ids[n++] = IT_DEPTH;
        SEC(SEC_SYSTEM); ids[n++] = IT_TV_KB; ids[n++] = IT_AMBIENT; if (g_app.ambient_minutes) ids[n++] = IT_AMBIENT_STYLE; if (g_app.ambient_minutes && g_app.ambient_style == AMBIENT_CLOCK) ids[n++] = IT_CLOCK_FACE; if (g_app.ambient_minutes && g_app.ambient_style == AMBIENT_FIRE) { ids[n++] = IT_FIRE_VIDEO; if (torbox_enabled()) ids[n++] = IT_FIRE_TORBOX; } if (g_app.ambient_minutes) { ids[n++] = IT_AMBIENT_DIM; ids[n++] = IT_AMBIENT_OFF; } ids[n++] = IT_MEMORY; ids[n++] = IT_BACKUP;
        break;
    case CAT_BREAKS:
        SEC(SEC_BR_MAIN); ids[n++] = IT_BR_TEST; ids[n++] = IT_BR_ON; ids[n++] = IT_BR_SCHED;
        if (g_app.break_sched == BRK_ISLAM) { ids[n++] = IT_BR_METHOD; ids[n++] = IT_BR_ASR; }
        ids[n++] = IT_BR_WHERE;
        SEC(SEC_BR_PLACE); ids[n++] = IT_BR_LOCATION; ids[n++] = IT_BR_DETECT; ids[n++] = IT_BR_TODAY;
        SEC(SEC_BR_ALERT); ids[n++] = IT_BR_LEAD; ids[n++] = IT_BR_LEN; ids[n++] = IT_BR_SOUND;
        if (g_app.break_sound == 1) { ids[n++] = IT_BR_AUDIO; if (torbox_enabled()) ids[n++] = IT_BR_TORBOX; ids[n++] = IT_BR_END; ids[n++] = IT_BR_VIDEO; }
        SEC(SEC_BR_MORE); ids[n++] = IT_BR_HEALTH; if (g_app.break_health_min) ids[n++] = IT_BR_HEALTH_LEN;
        ids[n++] = IT_BR_BED; ids[n++] = IT_BR_CUSTOM1; ids[n++] = IT_BR_CUSTOM2; ids[n++] = IT_BR_CUSTOM3;
        break;
    case CAT_ROWS:
        if (st->rows_seen != g_app.home_rows_version + g_app.addons_version * 1000) { st->n_rows = app_home_rows(st->rows, MAX_ROWS); st->rows_seen = g_app.home_rows_version + g_app.addons_version * 1000; }
        for (int i = 0; i < st->n_rows && n < max; i++) ids[n++] = IT_ROWS_BASE + i;
        break;
    case CAT_ADDONS: ids[n++] = IT_INSTALL; for (int i = 0; i < g_app.n_addons && n < max; i++) ids[n++] = IT_ADDON_BASE + i; break;
    case CAT_ABOUT: SEC(SEC_CUELIGHT); ids[n++] = IT_ABOUT_APP; ids[n++] = IT_WHATSNEW;
        SEC(SEC_UPDATES); ids[n++] = IT_UPDATE; ids[n++] = IT_AUTO_UPDATE; SEC(SEC_HELP); ids[n++] = IT_DIAG; ids[n++] = IT_STORAGE; ids[n++] = IT_SETUP; break;
    }
#undef SEC
    return n;
}

/* The line of help under a row's title; NULL when the title and value say it all. */
/* A row's name, for finding it (the rows draw their own, some of them changing with the value). */
static const char *row_title(int id)
{
    switch (id) {
    case IT_ACCOUNT: return "Sync with Stremio";
    case IT_SYNC_DEVICES: return "Sync with another device";
    case IT_SERVER: return "Streaming server";
    case IT_PREF_AUDIO: return "Preferred audio";
    case IT_PREF_SUBS: return "Preferred subtitles";
    case IT_SUBS_SIZE: return "Subtitle size";
    case IT_SUBS_STYLE: return "Subtitle style";
    case IT_SUBS_COLOUR: return "Subtitle colour";
    case IT_SUBS_SYNC: return "Auto-sync subtitles to the file";
    case IT_SUBS_FILES: return "Subtitle files over the video's own";
    case IT_BUFFER: return "Load ahead";
    case IT_TV_KB: return "Type with the TV's keyboard";
    case IT_BR_VIDEO: return "Show the video";
    case IT_P2P: return "P2P fallback on this TV";
    case IT_SRC_AUDIO: return "Sources in my audio language first";
    case IT_ADDON_WAIT: return "Autoplay waits for slow addons";
    case IT_SUBS_XL: return "Translate with AI when nobody offers my language";
    case IT_AUTOPLAY: return "Play next episode automatically";
    case IT_SKIP: return "Skip intros and recaps";
    case IT_NIGHT: return "Night mode";
    case IT_ADVISORY: return "Content advisory";
    case IT_LETTERBOX: return "Living letterbox (experimental)";
    case IT_SUBS_WHEN: return "Turn subtitles on";
    case IT_CLOCK: return "Clock";
    case IT_ISLAND_STYLE: return "Clock style";
    case IT_CLOCK_FACE: return "Clock face";
    case IT_QUALITY: return "Sort sources by";
    case IT_RES_CAP: return "Resolution for recommendations";
    case IT_SRC_SIZE: return "Largest file to pick";
    case IT_SRC_HDR: return "HDR and Dolby Vision";
    case IT_SRC_CACHED: return "Only cached or direct sources";
    case IT_SRC_EXCLUDE: return "Words to avoid in source names";
    case IT_TVCAP: return "What this TV can play";
    case IT_TRAKT: return "Trakt";
    case IT_SIMKL: return "SimKL";
    case IT_MDBLIST: return "MDBList ratings";
    case IT_TORBOX: return "TorBox";
    case IT_TORBOX_FILES: return "My TorBox videos";
    case IT_FIRE_TORBOX: case IT_BR_TORBOX: return "Choose from my TorBox";
    case IT_OS_KEY: return "OpenSubtitles.com";
    case IT_OS_LOGIN: return "OpenSubtitles account";
    case IT_PLAY_LINK: return "Play a link from your phone";
    case IT_BACKUP: return "Back up or restore settings";
    case IT_PREFLIGHT: return "Check sources before recommending";
    case IT_SCRUB: return "Show the picture while seeking";
    case IT_THUMBS: return "Home helper (optional)";
    case IT_PIN: return "Parental PIN";
    case IT_PIN_SETTINGS: return "Ask for the PIN to open Settings";
    case IT_HIDE_ADULT: return "Hide catalogs of addons flagged adult";
    case IT_TMDB: return "The Movie Database (TMDB)";
    case IT_AI_PROVIDER: return "AI assistant";
    case IT_AI_KEY: return "AI API key";
    case IT_AI_BASE: return "AI endpoint";
    case IT_AI_MODEL: return "AI model";
    case IT_MEMORY: return "Memory use";
    case IT_TRANSITIONS: return "Animated screen changes";
    case IT_DEPTH: return "Depth on backdrops";
    case IT_PIP: return "Picture-in-picture";
    case IT_BR_TEST: return "Try a break now";
    case IT_BR_ON: return "Take breaks";
    case IT_BR_SCHED: return "Break schedule";
    case IT_BR_METHOD: return "Calculation method";
    case IT_BR_ASR: return "Asr";
    case IT_BR_LOCATION: return "Location";
    case IT_BR_DETECT: return "Find my location from the internet";
    case IT_BR_TODAY: return "Today's breaks";
    case IT_BR_LEAD: return "Reminder banner";
    case IT_BR_LEN: return "Break length";
    case IT_BR_SOUND: return "Break sound";
    case IT_BR_AUDIO: return "Recording at the start";
    case IT_BR_END: return "Recording at the end";
    case IT_BR_HEALTH: return "Stretch reminder";
    case IT_BR_HEALTH_LEN: return "Stretch length";
    case IT_BR_BED: return "Bedtime";
    case IT_BR_CUSTOM1: case IT_BR_CUSTOM2: case IT_BR_CUSTOM3: return "Your own break time";
    case IT_BR_WHERE: return "Breaks apply";
    case IT_AUTO_REC: return "Play the recommended source automatically";
    case IT_THEME: return "Theme";
    case IT_PALETTE: return "Colour palette";
    case IT_ACCENT: return "Accent colour";
    case IT_BG: return "Background";
    case IT_CARD_DETAILS: return "Show details when a card stays focused";
    case IT_AUTO_TRAILER: return "Trailers play on their own";
    case IT_HOME_STYLE: return "Home screen";
    case IT_HIDE_WATCHED: return "Hide watched titles in catalogs";
    case IT_FX: return "Visual effects";
    case IT_OLED: return "Pure black (OLED)";
    case IT_ASK_PROFILE: return "Ask who's watching at start";
    case IT_AMBIENT: return "Screensaver";
    case IT_AMBIENT_STYLE: return "Screensaver style";
    case IT_AMBIENT_DIM: return "Dim the screensaver";
    case IT_AMBIENT_OFF: return "Picture off";
    case IT_FIRE_VIDEO: return "Fireplace video";
    case IT_BLUR: return "Blur unwatched episodes";
    case IT_DIAG: return "Diagnostics";
    case IT_STORAGE: return "Storage";
    case IT_WHATSNEW: return "What's new";
    case IT_ABOUT_APP: return "About " SNTV_APP_NAME;
    case IT_UPDATE: return "Update";
    case IT_AUTO_UPDATE: return "Ask me when an update is out";
    case IT_SETUP: return "Run quick setup again";
    case IT_INSTALL: return "Install addon from URL";
    default: return NULL;
    }
}

static const char *row_desc(int id)
{
    switch (id) {
    case IT_STORAGE: return "Artwork, addon replies and YouTube data kept so screens open faster. Clearing them frees the space for the TV's own apps; "
                            "they come back as you browse. Your sign-in, settings and Library stay. When the TV runs low on storage the cache clears by itself. OK clears it now.";
    case IT_ABOUT_APP: return SNTV_APP_NAME " is a fast player made for TVs, written from scratch in C for the TV's own hardware. It plays from addons in the open Stremio addon format, "
                              "has its own catalog of films and series (from TMDB), needs no account, and can sync with Stremio, Trakt and SimKL if you want it to. Not affiliated with Stremio.";
    case IT_WHATSNEW: return "This version's changes, as shown once after it was installed.";
    case IT_SYNC_DEVICES: return "Scan a code with your phone: sign-ins, addons, library, progress, lists and ratings, between this " SNTV_APP_NAME " and another. No account needed.";
    case IT_ACCOUNT: if (g_app.auth_key) {
            static char d[300];
            snprintf(d, sizeof d, "Signed in as %s. Your addons, Library and Continue Watching follow that account; turning it off keeps them all on this TV.", g_app.user_email ? g_app.user_email : "your account");
            return d;
        }
        return "Cuelight keeps your addons, Library and progress on this TV. Turn on to use a Stremio account's instead (they join yours).";
    case IT_SERVER: return "Stremio's streaming service on your network plays torrents.";
    case IT_AUTOPLAY: return "Starts the next episode when the credits roll.";
    case IT_SKIP: return "Uses known intro, recap and credits times (IntroDB, AniSkip).";
    case IT_LETTERBOX: return "Experimental: it can make playback less smooth on some TVs. A film wider or narrower than the screen leaves bars. Glow: a soft blur of the picture. One colour: the scene's main colour. Black: plain bars. Except Off, subtitles move into the bar below. A film's built-in subtitles are drawn by the app so it stays on; where that can't be done, it steps aside for them.";
    case IT_ADVISORY: return "When a video starts, its age rating and what it contains (violence, language, nudity...) show for a few seconds, from IMDb's parents guide.";
    case IT_NIGHT: return "Videos play quieter and subtitles one size larger, so late viewing stays easy to follow without waking anyone.";
    case IT_PIP: return "Back keeps the video playing in a corner while you browse.";
    case IT_CLOCK: return "In the corner of every screen; hidden while a video plays.";
    case IT_ISLAND_STYLE: return "Just the time, the day too, seconds, or a small dial beside it.";
    case IT_CLOCK_FACE: return "Digital, flip cards, a dial, words, rings, nixie tubes, stacked digits, an LED alarm clock, dot matrix, neon, rolling drums, planets, the sky of the hour, a station clock, liquid digits, the weather, an aurora, a solar system that tells the time, the day as an orbit with the prayers on it, the horizon, or the planets where they stand tonight.";
    case IT_SCRUB: return "The picture follows the timeline while you seek.";
    case IT_PREF_AUDIO: return "Picked by itself when a video has it.";
    case IT_PREF_SUBS: return "Picked by itself from the video and your addons.";
    case IT_SUBS_WHEN: return "Always, or only when the audio is in another language.";
    case IT_SUBS_SIZE: return "For subtitles from addons and those inside the video.";
    case IT_SUBS_STYLE: return "Colour, outline or a box behind the text.";
    case IT_SUBS_COLOUR: return "The letters in a caption colour, over any style; easier to tell apart for some eyes.";
    case IT_SUBS_SYNC: return "Lines addon subtitles up with the video's own timing.";
    case IT_SUBS_FILES: return "Off: a video's own subtitles first; always in sync, drawn by the TV. On: a file matched to the video first; your style, though it can be a little off.";
    case IT_TV_KB: return "Search opens the TV's own keyboard, with its suggestions. Off: the app's keyboard on screen.";
    case IT_BUFFER: return "Up to 2 minutes kept ready in the TV's memory, so a slow or dropped connection doesn't stop the picture. Only what the TV can spare.";
    case IT_QUALITY: return "Which source comes first in the list.";
    case IT_RES_CAP: return "Recommended sources stay at or below this.";
    case IT_SRC_SIZE: return "Bigger files stay in the list but are never picked for you.";
    case IT_SRC_HDR: return "Avoid sources this screen shows badly. They stay in the list.";
    case IT_SRC_CACHED: return "Torrents that need a download first are never picked for you.";
    case IT_TRAKT: return "Scrobbles what you watch; your watchlist and recommendations become Home rows.";
    case IT_SIMKL: return "Titles you finish are added to your SimKL history.";
    case IT_OS_KEY: return "Built-in subtitle search, with release names to find the subtitle made for your file's release.";
    case IT_OS_LOGIN: return "Optional: a free account allows more subtitle downloads a day.";
    case IT_MDBLIST: return "Rotten Tomatoes (critics and audience), Metacritic and Letterboxd scores on title pages and the paused screen. Free key at mdblist.com.";
    case IT_TORBOX: return "Torrent sources TorBox has cached play from it instead of peers, and what you keep there plays here: films, the fireplace video, a break's recording.";
    case IT_TORBOX_FILES: return "Everything in your TorBox, to watch here at full quality.";
    case IT_FIRE_TORBOX: return "A video in your TorBox as the fireplace; it plays through a fresh link each time.";
    case IT_BR_TORBOX: return "A video in your TorBox at the start of each break; it plays through a fresh link each time.";
    case IT_PLAY_LINK: return "Send a video link from your phone and it plays here.";
    case IT_BACKUP: return "Save your settings to your phone, or bring a backup back.";
    case IT_TVCAP: return "4K, HEVC, AV1, Dolby Vision, DTS and TrueHD: what plays here, learned from what you watch.";
    case IT_SRC_EXCLUDE: return "Comma-separated, e.g. CAM, HDTS, 3D. Matching sources are never picked.";
    case IT_PREFLIGHT: return "Makes sure a link answers before it is recommended.";
    case IT_AUTO_REC: return "Skips the list and starts the best match.";
    case IT_ADDON_WAIT: return "How long autoplay waits for slow addons before choosing.";
    case IT_P2P: return "When TorBox has no cached copy. Peers see your IP while it plays.";
    case IT_THEME: return "Colours, type and how focus looks.";
    case IT_OLED: return "No grey behind the screens; kind to OLED panels.";
    case IT_ASK_PROFILE: return "The saved accounts as profiles when the app opens.";
    case IT_BG: return "A tint for the screens behind the artwork.";
    case IT_PALETTE: return "An accent and a background that go together. Accent and Background below mix your own.";
    case IT_ACCENT: return "Focus, highlights and progress.";
    case IT_HOME_STYLE: return "How much room the focused title's artwork takes.";
    case IT_CARD_DETAILS: return "After a moment on a card it widens with the rating, runtime and synopsis; the cards beside it make room.";
    case IT_AUTO_TRAILER: return "When you stay on a title, its trailer plays behind the artwork on Home and the title page, then the artwork comes back.";
    case IT_HIDE_WATCHED: return "Films and series you finished leave the catalog rows.";
    case IT_BLUR: return "Episode stills stay blurred until you watch them.";
    case IT_FX: return "Light drops shadows and glows; Automatic decides from frame times.";
    case IT_TRANSITIONS: return "Screens slide and fade as you move between them.";
    case IT_DEPTH: return depth_off() == 1 && mem_saver_lasting() ? "Off while the memory saver is on (Memory use, further down): Depth needs room for its maps."
                        : depth_off() == 1 ? "Off for now: the TV is short of memory. It comes back by itself when there is room again."
                        : depth_off() == 2 ? "Off for now: the TV's storage is nearly full. It comes back by itself when there is room again."
                        : "Home's backdrop moves slowly into the picture: the people come towards you, the background slides behind. For popular titles.";
    case IT_AMBIENT: return "When the remote has been quiet this long.";
    case IT_AMBIENT_STYLE: return "Artwork, posters, a clock, the logo, a fireplace, northern lights, a rainy window, a cinema marquee, a starfield, depth, a gallery of museum paintings, or a different one each time.";
    case IT_AMBIENT_DIM: return "The screensaver darkens this long after it comes up: kinder to the panel on a long evening.";
    case IT_AMBIENT_OFF: return "The picture goes black, with a faint clock, this long after the screensaver comes up. Any key brings it back.";
    case IT_FIRE_VIDEO: return "A YouTube or video link of a real fire, played full screen and looped with the clock.";
    case IT_MEMORY: return "Rows and artwork kept; Automatic saves when memory gets tight.";
    case IT_AI_PROVIDER: return "Ask about a title, describe a mood, translate subtitles.";
    case IT_AUTO_UPDATE: return "A popup at a quiet moment: Update now or Later. Never while something plays.";
    case IT_BR_ON: return "Playback pauses at the times below.";
    case IT_BR_SCHED: return "Where break times come from.";
    case IT_BR_METHOD: return "Auto follows your country's own authority.";
    case IT_BR_WHERE: return "Only during videos, or while browsing too.";
    case IT_BR_LEAD: return "A banner warns this long before a break.";
    case IT_BR_LEN: return "How long playback stays paused.";
    case IT_BR_SOUND: return "What plays when a break starts.";
    case IT_BR_VIDEO: return "For YouTube links and video recordings; off plays sound only.";
    case IT_BR_HEALTH: return "A short pause to stand up and stretch.";
    case IT_BR_BED: return "Playback stops at this time, with a warning first.";
    case IT_BR_CUSTOM1: case IT_BR_CUSTOM2: case IT_BR_CUSTOM3: return "A daily break at a time you choose.";
    default: return NULL;
    }
}

static float row_h(int id) { return IS_SEC(id) ? SEC_H : ROW_H; }

/* What Left/Right do on a row, known from its id (keys must not depend on what was drawn). */
static int row_takes_left(int id)
{
    if (id >= IT_ROWS_BASE && id < IT_ACCT_BASE) return 1;   /* home rows and addons move */
    switch (id) {
    case IT_PREF_AUDIO: case IT_PREF_SUBS: case IT_SUBS_SIZE: case IT_SUBS_STYLE: case IT_SUBS_COLOUR: case IT_ADDON_WAIT: case IT_SKIP: case IT_NIGHT:
    case IT_SUBS_WHEN: case IT_CLOCK: case IT_ISLAND_STYLE: case IT_CLOCK_FACE: case IT_QUALITY: case IT_RES_CAP: case IT_SRC_SIZE: case IT_SRC_HDR: case IT_AI_PROVIDER: case IT_MEMORY:
    case IT_BR_SCHED: case IT_BR_METHOD: case IT_BR_ASR: case IT_BR_LEAD: case IT_BR_LEN: case IT_BR_SOUND:
    case IT_BR_HEALTH: case IT_BR_HEALTH_LEN: case IT_BR_BED: case IT_BR_CUSTOM1: case IT_BR_CUSTOM2: case IT_BR_CUSTOM3:
    case IT_BR_WHERE: case IT_THEME: case IT_PALETTE: case IT_ACCENT: case IT_BG: case IT_HOME_STYLE: case IT_FX: case IT_AMBIENT: case IT_AMBIENT_STYLE: case IT_LETTERBOX: case IT_AMBIENT_DIM: case IT_AMBIENT_OFF:
        return 1;
    default: return 0;   /* switches and actions: Left goes back to the categories */
    }
}

/* Focus never rests on a heading: the next focusable row from i in direction dir, or -1. */
static int focusable(const int *ids, int n, int i, int dir)
{
    for (; i >= 0 && i < n; i += dir) if (!IS_SEC(ids[i])) return i;
    return -1;
}

/* The rows changed under the focus (a row went away): move it to the nearest row, never a heading. */
static void fix_focus(Settings *st, const int *ids, int n)
{
    if (st->zone != 1) return;
    if (st->focus < n && st->focus >= 0 && !IS_SEC(ids[st->focus])) return;
    int f = focusable(ids, n, st->focus < n ? (st->focus < 0 ? 0 : st->focus) : n - 1, 1);
    st->focus = f >= 0 ? f : focusable(ids, n, n - 1, -1);
    if (st->focus < 0) { st->zone = 0; st->focus = 0; }
}

static const char *lang_name(const char *code) { return code[0] ? lang_display(code) : "Off"; }

static void cycle_lang(char *code, size_t n, int dir)
{
    int count = lang_count(), i = (lang_index(code) + dir + count) % count;
    snprintf(code, n, "%s", LANG_CODES[i]);
}

typedef enum { KIND_ACTION, KIND_TOGGLE, KIND_CHOICE, KIND_MOVABLE } RowKind;

static float s_x = COL_X0, s_r = SNTV_UI_BASE_W - THEME_MARGIN;   /* the rows' left and right edges this frame */
static const char *s_desc;   /* help line of the row being drawn (set per row in draw) */
static int s_steps, s_step;  /* a scale: how many steps, and where the value is (a slider under it) */

/* Settings that are a scale, low to high: Left/Right walk them and the row draws a slider. */
static const int ADDON_WAITS[] = { 3, 5, 8, 15, 30, 99 }, BR_LEADS[] = { 1, 2, 3, 5, 10, 15 },
                 BR_LENS[] = { -1, 0, 3, 5, 10, 15, 20, 30 }, BR_HEALTH_LENS[] = { 2, 3, 5, 10 }, AMBIENT_STEPS[] = { 0, 2, 5, 10, 20 }, DIM_STEPS[] = { 0, 5, 10, 20, 30, 60 }, OFF_STEPS[] = { 0, 15, 30, 60, 120, 240 }, SRC_SIZES[] = { 0, 2, 5, 10, 20, 40 };
#define N_OF(a) ((int)(sizeof (a) / sizeof (a)[0]))
static int step_in(const int *l, int n, int v, int dflt) { for (int i = 0; i < n; i++) if (l[i] == v) return i; return dflt; }
static int step_move(const int *l, int n, int v, int dflt, int dir) { return l[(step_in(l, n, v, dflt) + dir + n) % n]; }

static int scale_of(int id, int *step)
{
    switch (id) {
    case IT_SUBS_SIZE: *step = g_app.subs_size; return 4;
    case IT_RES_CAP: *step = g_app.res_cap <= 720 ? 0 : g_app.res_cap <= 1080 ? 1 : 2; return 3;
    case IT_SRC_SIZE: *step = step_in(SRC_SIZES, N_OF(SRC_SIZES), g_app.src_max_gb, 0); return N_OF(SRC_SIZES);
    case IT_ADDON_WAIT: *step = step_in(ADDON_WAITS, N_OF(ADDON_WAITS), g_app.addon_wait, 2); return N_OF(ADDON_WAITS);
    case IT_BR_LEAD: *step = step_in(BR_LEADS, N_OF(BR_LEADS), g_app.break_lead_min, 3); return N_OF(BR_LEADS);
    case IT_BR_LEN: *step = step_in(BR_LENS, N_OF(BR_LENS), g_app.break_len_min, 4); return N_OF(BR_LENS);
    case IT_BR_HEALTH_LEN: *step = step_in(BR_HEALTH_LENS, N_OF(BR_HEALTH_LENS), g_app.break_health_len, 1); return N_OF(BR_HEALTH_LENS);
    case IT_AMBIENT: *step = step_in(AMBIENT_STEPS, N_OF(AMBIENT_STEPS), g_app.ambient_minutes, 0); return N_OF(AMBIENT_STEPS);
    case IT_AMBIENT_DIM: *step = step_in(DIM_STEPS, N_OF(DIM_STEPS), g_app.ambient_dim_min, 0); return N_OF(DIM_STEPS);
    case IT_AMBIENT_OFF: *step = step_in(OFF_STEPS, N_OF(OFF_STEPS), g_app.ambient_off_min, 0); return N_OF(OFF_STEPS);
    default: *step = 0; return 0;
    }
}
/* what the focused row drew, for the preview panel */
static char s_ftitle[200], s_fvalue[300]; static Icon s_ficon; static int s_fkind, s_fon;
static int s_chevron;        /* actions end in an arrow, except addons (their arrows move them) */

static void draw_row_body(float y, Icon icon, const char *title, const char *value, int focused, RowKind kind, int on);

/* Aperture draws the same row unfilled: ivory type on the page, the focused one
 * outlined and underlined in coral. */
static void draw_row(float y, Icon icon, const char *title, const char *value, int focused, RowKind kind, int on)
{
    if (g_theme.id != THEME_APERTURE || !focused) { draw_row_body(y, icon, title, value, focused, kind, on); return; }
    Color fo = g_theme.focus, onf = g_theme.on_focus;
    g_theme.focus = COLOR_ALPHA(g_theme.text, 0x0C); g_theme.on_focus = g_theme.text;
    draw_row_body(y, icon, title, value, focused, kind, on);
    g_theme.focus = fo; g_theme.on_focus = onf;
    float h = ROW_H - 12;
    gfx_rrect_ring(s_x, y, s_r - s_x, h, 3, 2, THEME_TEXT);
}

static void draw_row_body(float y, Icon icon, const char *title, const char *value, int focused, RowKind kind, int on)
{
    float x = s_x, w = s_r - s_x, h = ROW_H - 12;
    if (focused) {
        snprintf(s_ftitle, sizeof s_ftitle, "%s", title);
        snprintf(s_fvalue, sizeof s_fvalue, "%s", value ? value : "");
        s_ficon = icon; s_fkind = kind; s_fon = on;
    }
    if (focused && !g_app.fx_light) gfx_shadow(x, y + 6, w, h, R_PANEL, 24, RGBA(0, 0, 0, 0x80));
    if (focused) ui_focus_under(x, y, w, h, R_PANEL, 1);
    gfx_rrect(x, y, w, h, R_PANEL, focused ? THEME_FOCUS : COLOR_ALPHA(THEME_TEXT, 0x0F));
    Color fg = focused ? THEME_ON_FOCUS : THEME_TEXT, dim = focused ? COLOR_ALPHA(THEME_ON_FOCUS, 0xA8) : THEME_TEXT_MUTED;
    gfx_rrect(x + 20, y + h / 2 - 26, 52, 52, theme_pill_r(52), focused ? COLOR_ALPHA(THEME_ON_FOCUS, 0x14) : COLOR_ALPHA(THEME_ACCENT, 0x1C));
    ui_icon(icon, x + 46, y + h / 2, 26, focused ? fg : THEME_ACCENT);
    /* an action's value is its second line (unless it has help of its own), with an arrow at the end */
    const char *sub = s_desc;
    if (kind == KIND_ACTION && !sub && value) { sub = value; value = NULL; }
    float th = font_height(FONT_S | FONT_B), dh = font_height(FONT_XS);
    float cvw = kind == KIND_CHOICE ? font_measure(value, FONT_S | FONT_B) : 0;
    if (cvw > w * 0.42f) cvw = w * 0.42f;   /* a long value gives way to the title (a phone) */
    float right = kind == KIND_TOGGLE ? 130 : kind == KIND_MOVABLE ? 300 : kind == KIND_CHOICE ? cvw + 110
                : value ? (font_measure(value, FONT_XS) < w * 0.45f - 90 ? font_measure(value, FONT_XS) : w * 0.45f - 90) + 80 : 60;
    float text_w = w - 92 - right - 16;
    if (sub) {
        float ty = y + (h - th - dh - 4) / 2;
        font_draw_fit(title, x + 92, ty, FONT_S | FONT_B, fg, text_w);
        font_draw_fit(sub, x + 92, ty + th + 4, FONT_XS, focused ? COLOR_ALPHA(THEME_ON_FOCUS, 0xB8) : THEME_TEXT_DIM, text_w);
    } else font_draw_fit(title, x + 92, y + (h - th) / 2, FONT_S | FONT_B, fg, text_w);
    float rx = x + w - 30;
    if (kind == KIND_TOGGLE) { ui_toggle(rx - 76, y + (h - 40) / 2, on, focused); return; }
    if (kind == KIND_MOVABLE) {   /* shown/hidden toggle plus move arrows */
        ui_toggle(rx - 76, y + (h - 40) / 2, on, focused);
        ui_icon(ICON_CHEVRON_LEFT, rx - 130, y + h / 2, 22, dim);
        ui_icon(ICON_CHEVRON_RIGHT, rx - 100, y + h / 2, 22, dim);
        if (value) font_draw_fit(value, x + 92 + w * 0.5f + 10, y + (h - font_height(FONT_XS)) / 2, FONT_XS, dim, w * 0.5f - 260);
        return;
    }
    if (kind == KIND_CHOICE) {
        float vy = y + (h - th) / 2;
        if (s_steps > 1) {   /* a scale: the value above a track with a tick a step and the knob at it */
            vy = y + h / 2 - th + 2;
            float tw = 30.0f * (s_steps - 1) < 200 ? 30.0f * (s_steps - 1) : 200, t1 = rx - 12, t0 = t1 - tw, ty = y + h / 2 + 16;
            float kx = t0 + tw * s_step / (s_steps - 1);
            Color track = focused ? COLOR_ALPHA(THEME_ON_FOCUS, 0x30) : COLOR_ALPHA(THEME_TEXT, 0x26), lit = focused ? THEME_ON_FOCUS : THEME_ACCENT;
            gfx_rrect(t0, ty - 2, tw, 4, 2, track);
            if (kx > t0 + 2) gfx_rrect(t0, ty - 2, kx - t0, 4, 2, lit);
            for (int i = 0; i < s_steps; i++) gfx_circle(t0 + tw * i / (s_steps - 1), ty, 3.5f, i <= s_step ? lit : track);
            gfx_circle(kx, ty, 8, lit);
            if (focused) gfx_circle(kx, ty, 3.5f, THEME_FOCUS);
        }
        ui_icon(ICON_CHEVRON_RIGHT, rx - 10, vy + th / 2, 22, dim);
        float vw = cvw;
        font_draw_fit(value, rx - 36 - vw, vy, FONT_S | FONT_B, fg, vw + 1);
        ui_icon(ICON_CHEVRON_LEFT, rx - 60 - vw, vy + th / 2, 22, dim);
        ui_hit(rx - 100 - vw, y, 80, h, HIT_LEFT + s_hit_row);   /* the rest of the row steps forward */
        return;
    }
    if (s_chevron) ui_icon(ICON_CHEVRON_RIGHT, rx - 10, y + h / 2, 22, dim);
    if (value) {
        float vw = font_measure(value, FONT_XS), mw = w * 0.45f - 90;
        if (vw > mw) vw = mw;
        font_draw_fit(value, rx - 40 - vw, y + (h - dh) / 2, FONT_XS, dim, mw);
    }
}

/* A heading in the accent's tone with a hairline to the edge. */
static void draw_section(float y, const char *name)
{
    char up[96]; ui_upper(name, up, sizeof up);
    float x = s_x + 4, base = y + SEC_H - 34;
    font_draw_tracked(up, x, base, FONT_XS | FONT_B, THEME_ACCENT, 1.5f);
    float tw = font_measure_tracked(up, FONT_XS | FONT_B, 1.5f);
    gfx_rect(x + tw + 20, base + font_height(FONT_XS | FONT_B) / 2, s_r - (x + tw + 20), 1, THEME_LINE);
}

static void finish_edit(Settings *st, int save);
static const char *edit_title(int which);
static const char *edit_hint(int which);
static void update(Screen *s, float dt)
{
    Settings *st = s->st;
    if (st->edit == EDIT_BACKUP) {
        char *json = phone_take_restore();
        if (json) { int n = app_restore_settings(json); free(json); app_toast(n > 0 ? "Settings restored from your phone (%d)" : "That backup could not be read", n); finish_edit(st, 0); app_redraw(); }
        return;
    }
    if (st->edit != EDIT_NONE) {   /* text sent from the phone fills the field and saves */
        char got[600];
        if (phone_take(got, sizeof got)) { snprintf(st->kb.text, sizeof st->kb.text, "%.255s", got); app_toast("Received from your phone"); finish_edit(st, 1); app_redraw(); }
    }
    if (st->edit == EDIT_NONE) {   /* keep the focused row inside the pane, two rows from its foot */
        int ids[MAX_ROWS + 8];
        int n = rows_for(st, st->cat, ids, MAX_ROWS + 8);
        fix_focus(st, ids, n);
        float yf = 0;
        for (int i = 0; i < n && i < st->focus; i++) yf += row_h(ids[i]);
        float t = yf - (SNTV_UI_H - ROWS_TOP - 2.5f * ROW_H), total = 0;
        st->scroll_target = t < 0 ? 0 : t;
        for (int i = 0; i < n; i++) total += row_h(ids[i]);
        st->scroll_max = total - (SNTV_UI_H - ROWS_TOP) + 1.5f * ROW_H;
        int fid = st->zone == 1 && st->focus < n ? ids[st->focus] : -1;
        if (fid != st->panel_id) { st->panel_id = fid; st->panel_t = g_app.fx_light ? 1 : 0; }
    }
    ui_pan(&st->pan, &st->scroll, st->scroll_target, st->scroll_max, dt, 12);
    if (SNTV_PHONE) {   /* the categories: the focused one kept above the tab bar */
        float view = SNTV_UI_H - PHONE_TABS_H, fy = PH_CAT_Y + st->cat * PH_CAT_H, want = st->cat_y;
        if (fy + PH_CAT_H > want + view - 20) want = fy + PH_CAT_H - view + 20;
        if (fy - 40 < want) want = fy - 40;
        if (st->search_foc || want < 0) want = 0;
        ui_pan(&st->cat_pan, &st->cat_y, want, PH_CAT_Y + CAT_COUNT * PH_CAT_H + 20 - view, dt, 14);
    }
    if (st->pane_cat != st->cat) { st->pane_cat = st->cat; st->pane_t = g_app.fx_light ? 1 : 0; }
    if (st->pane_t < 1) { st->pane_t += dt * 5; if (st->pane_t > 1) st->pane_t = 1; app_redraw(); }
    if (st->panel_t < 1) { st->panel_t += dt * 6; if (st->panel_t > 1) st->panel_t = 1; app_redraw(); }
    float want = st->zone == 1 && st->edit == EDIT_NONE ? 1.0f : 0.0f;
    if (g_app.fx_light || !g_app.transitions) st->wide_t = want;
    else if (st->wide_t != want) {
        st->wide_t += (want > st->wide_t ? 1 : -1) * dt * 6;
        if (st->wide_t < 0) st->wide_t = 0;
        if (st->wide_t > 1) st->wide_t = 1;
        app_redraw();
    }
}

static const char *edit_title(int which);
static const char *edit_hint(int which);
static void draw_categories(const Settings *st, float e)
{
    font_draw("Settings", THEME_MARGIN, g_theme.id == THEME_APERTURE ? 86 : 100, g_theme.id == THEME_APERTURE ? FONT_XL | FONT_ALT : FONT_XL | FONT_B, COLOR_ALPHA(THEME_TEXT, (int)(255 * (1 - e))));
    if (e < 1) {   /* Find a setting: under the heading, Up from the first category */
        const char *t = "Find a setting";
        float fx = THEME_MARGIN, fh = 50, fw = COL_X0 - THEME_MARGIN - 70, fy = ROWS_TOP - fh - 16;
        int foc = st->zone == 0 && st->search_foc;
        gfx_set_alpha(1 - e);
        ui_hit(fx, fy, fw, fh, HIT_FIND);
        if (foc) { ui_focus_under(fx, fy, fw, fh, theme_pill_r(fh), 1); gfx_rrect(fx, fy, fw, fh, theme_pill_r(fh), THEME_FOCUS); }
        else { gfx_rrect(fx, fy, fw, fh, theme_pill_r(fh), COLOR_ALPHA(THEME_TEXT, 0x10)); gfx_rrect_ring(fx, fy, fw, fh, theme_pill_r(fh), 1, THEME_LINE); }
        ui_icon(ICON_SEARCH, fx + 36, fy + fh / 2, 22, foc ? THEME_ON_FOCUS : THEME_TEXT_MUTED);
        font_draw(t, fx + 70, fy + (fh - font_height(FONT_S)) / 2, FONT_S, foc ? THEME_ON_FOCUS : THEME_TEXT_MUTED);
        gfx_set_alpha(1);
    }
    float step = (SNTV_UI_H - ROWS_TOP - 24) / CAT_COUNT;   /* closer together on a short screen (a phone) */
    if (step > 76) step = 76;
    for (int i = 0; i < CAT_COUNT; i++) {
        float y = ROWS_TOP + i * step, h = step - 12, w = (COL_X0 - THEME_MARGIN - 70) * (1 - e) + 72 * e;
        int foc = st->zone == 0 && st->cat == i && !st->search_foc, act = st->cat == i;
        int ap = g_theme.id == THEME_APERTURE;
        ui_hit(THEME_MARGIN, y, w, h, HIT_CAT + i);
        if (ap) {
            if (act) gfx_rect(THEME_MARGIN - 16, y + 10, 4, h - 20, THEME_ACCENT);
            if (foc) gfx_rrect_ring(THEME_MARGIN, y, w, h, 3, 2, THEME_TEXT);
        } else if (foc) { ui_focus_under(THEME_MARGIN, y, w, h, theme_pill_r(h), 1); gfx_rrect(THEME_MARGIN, y, w, h, theme_pill_r(h), THEME_FOCUS); }
        else if (act) gfx_rrect(THEME_MARGIN, y, w, h, R_PANEL, e > 0.5f ? COLOR_ALPHA(THEME_ACCENT, 0x30) : COLOR_ALPHA(THEME_TEXT, 0x16));
        Color fg = ap ? (foc || act ? THEME_TEXT : THEME_TEXT_DIM) : foc ? THEME_ON_FOCUS : act ? (e > 0.5f ? THEME_ACCENT : THEME_TEXT) : THEME_TEXT_DIM;
        ui_icon(CAT_ICONS[i], THEME_MARGIN + 36, y + h / 2, 26, fg);
        if (e >= 1) continue;   /* collapsed: icons only */
        gfx_set_alpha(1 - e);
        gfx_clip_push(THEME_MARGIN, y, w, h);
        font_draw(CAT_NAMES[i], THEME_MARGIN + 70, y + (h - font_height(FONT_S | FONT_B)) / 2, FONT_S | FONT_B, fg);
        if (i == CAT_ADDONS) { char c[16]; snprintf(c, sizeof c, "%d", g_app.n_addons); font_draw(c, THEME_MARGIN + w - 24 - font_measure(c, FONT_XS | FONT_B), y + (h - font_height(FONT_XS | FONT_B)) / 2, FONT_XS | FONT_B, foc ? COLOR_ALPHA(THEME_BG, 0xA8) : THEME_TEXT_MUTED); }
        gfx_clip_pop();
        gfx_set_alpha(1);
    }
}

static PreviewKind preview_for(int id)
{
    switch (id) {
    case IT_THEME: case IT_PALETTE: case IT_ACCENT: case IT_BG: return PV_THEME;
    case IT_HOME_STYLE: return PV_HOME_STYLE;
    case IT_SUBS_SIZE: case IT_SUBS_STYLE: case IT_SUBS_COLOUR: return PV_SUBS;
    case IT_CLOCK: case IT_ISLAND_STYLE: return PV_CLOCK;
    case IT_CLOCK_FACE: return PV_AMBIENT;
    case IT_CARD_DETAILS: return PV_CARD_DETAILS;
    case IT_HIDE_WATCHED: return PV_HIDE_WATCHED;
    case IT_BLUR: return PV_BLUR;
    case IT_ADVISORY: return PV_ADVISORY;
    case IT_LETTERBOX: return PV_LETTERBOX;
    case IT_FX: return PV_FX;
    case IT_OLED: return PV_OLED;
    case IT_RES_CAP: return PV_RES_CAP;
    case IT_QUALITY: return PV_QUALITY;
    case IT_SKIP: return PV_SKIP;
    case IT_AUTOPLAY: return PV_AUTOPLAY;
    case IT_PIP: return PV_PIP;
    case IT_SCRUB: return PV_SCRUB;
    case IT_AMBIENT: case IT_AMBIENT_STYLE: case IT_AMBIENT_DIM: case IT_AMBIENT_OFF: return PV_AMBIENT;
    case IT_TRANSITIONS: return PV_TRANSITIONS;
    case IT_DEPTH: return PV_DEPTH;
    case IT_PREF_AUDIO: return PV_LANG_AUDIO;
    case IT_PREF_SUBS: case IT_SUBS_WHEN: return PV_LANG_SUBS;
    case IT_BR_ON: case IT_BR_LEAD: case IT_BR_TEST: case IT_BR_SCHED: case IT_BR_WHERE: return PV_BREAK;
    case IT_MEMORY: return PV_MEMORY;
    case IT_SUBS_SYNC: return PV_SUBS_SYNC;
    case IT_PREFLIGHT: return PV_PREFLIGHT;
    case IT_AUTO_REC: return PV_AUTO_REC;
    case IT_SRC_AUDIO: return PV_SRC_AUDIO;
    case IT_ADDON_WAIT: return PV_ADDON_WAIT;
    case IT_P2P: return PV_P2P;
    case IT_UPDATE: case IT_AUTO_UPDATE: return PV_UPDATE;
    case IT_PIN: case IT_PIN_SETTINGS: case IT_HIDE_ADULT: return PV_PARENTAL;
    default: return PV_NONE;
    }
}

/* A key and what it does, as a small keycap and a label; returns the x after it. */
static float key_hint(const char *cap, const char *what, float x, float y)
{
    /* cap NULL: the Left and Right arrows */
    float cw = cap ? font_measure(cap, FONT_XS | FONT_B) + 20 : 56, h = 32;
    gfx_rrect(x, y, cw, h, R_KEY, COLOR_ALPHA(THEME_TEXT, 0x1C));
    if (cap) font_draw_center(cap, x + cw / 2, y + (h - font_height(FONT_XS | FONT_B)) / 2, FONT_XS | FONT_B, THEME_TEXT);
    else { ui_icon(ICON_CHEVRON_LEFT, x + 18, y + h / 2, 18, THEME_TEXT); ui_icon(ICON_CHEVRON_RIGHT, x + cw - 18, y + h / 2, 18, THEME_TEXT); }
    font_draw(what, x + cw + 10, y + (h - font_height(FONT_XS)) / 2, FONT_XS, THEME_TEXT_DIM);
    return x + cw + 10 + font_measure(what, FONT_XS) + 26;
}

/* Beside the rows: what the focused setting does, drawn, then its name, help, value and keys. */
static void draw_panel(const Settings *st, const int *ids, int n, float e)
{
    if (st->zone != 1 || st->focus < 0 || st->focus >= n || IS_SEC(ids[st->focus])) return;
    int id = ids[st->focus];
    float x = PREV_X + 60 * (1 - e), y = 120, w = SNTV_UI_W - THEME_MARGIN - PREV_X, h = SNTV_UI_H - 80 - y;
    gfx_set_alpha(e);
    if (g_theme.id != THEME_APERTURE) gfx_rrect(x, y, w, h, R_PANEL, COLOR_ALPHA(THEME_TEXT, 0x0A));   /* Aperture's preview sits on the page */
    PreviewKind kind = preview_for(id);
    float ih = 470, pt = 1 - (1 - st->panel_t) * (1 - st->panel_t) * (1 - st->panel_t);
    gfx_set_alpha(e * pt);   /* a new setting's drawing rises into place */
    gfx_set_translate(0, 18 * (1 - pt));
    if (kind != PV_NONE) settings_preview_draw(kind, x, y + 8, w, ih);
    else {   /* no drawing for this one: its icon, large */
        gfx_circle(x + w / 2, y + ih / 2, 96, COLOR_ALPHA(THEME_ACCENT, 0x1C));
        ui_icon(s_ficon, x + w / 2, y + ih / 2, 96, THEME_ACCENT);
    }
    gfx_set_translate(0, 0);
    gfx_set_alpha(e);
    float tx = x + 32, tw = w - 64, ty = y + ih + 28;
    const char *sec = NULL;
    for (int i = st->focus; i >= 0 && !sec; i--) if (IS_SEC(ids[i])) sec = SEC_NAMES[ids[i] - IT_SEC_BASE];
    char crumb[160], up[160];
    snprintf(crumb, sizeof crumb, "%s%s%s", CAT_NAMES[st->cat], sec ? "  \xC2\xB7  " : "", sec ? sec : "");
    ui_upper(crumb, up, sizeof up);
    font_draw_tracked(up, tx, ty, FONT_XS | FONT_B, THEME_ACCENT, 1.2f);
    ty += 40;
    int lines = font_draw_wrapped(s_ftitle, tx, ty, FONT_M | FONT_B, THEME_TEXT, tw, 2);
    ty += (lines > 0 ? lines : 1) * (font_height(FONT_M | FONT_B) + 6) + 10;
    const char *help = row_desc(id);
    if (help) {
        int hl = font_draw_wrapped(help, tx, ty, FONT_S, THEME_TEXT_DIM, tw, 3);
        ty += (hl > 0 ? hl : 1) * (font_height(FONT_S) + 6) + 14;
    }
    const char *now = s_fkind == KIND_TOGGLE ? (s_fon ? "On" : "Off") : s_fvalue[0] ? s_fvalue : NULL;
    if (now && ty < y + h - 110) {
        font_draw("Now", tx, ty, FONT_XS | FONT_B, THEME_TEXT_MUTED);
        font_draw_wrapped(now, tx + 70, ty - 2, FONT_S | FONT_B, THEME_TEXT, tw - 70, 2);
    }
    float ky = y + h - 56, kx = tx;
    if (s_fkind == KIND_CHOICE) kx = key_hint(NULL, "Change", kx, ky);
    else if (s_fkind == KIND_TOGGLE) kx = key_hint("OK", "Switch", kx, ky);
    else if (s_fkind == KIND_MOVABLE) { kx = key_hint("OK", "Show or hide", kx, ky); kx = key_hint(NULL, "Move", kx, ky); }
    else kx = key_hint("OK", "Open", kx, ky);
    key_hint("Back", "Categories", kx, ky);
    gfx_set_alpha(1);
}

/* ---- upright on a phone: the categories as a list, each opening its rows on a page of
 * its own; typing with the phone's keyboard ---- */

static void phone_categories(Settings *st)
{
    float W = SNTV_UI_W, x = 40, w = W - 80;
    ui_pan_area(&st->cat_pan, 0, 0, W, SNTV_UI_H - PHONE_TABS_H, 1);
    gfx_set_translate(0, -st->cat_y);
    font_draw("Settings", x, 36, FONT_XL | FONT_B, THEME_TEXT);
    float fy = 132, fh = 92;
    int ff = st->search_foc;
    gfx_rrect(x, fy, w, fh, fh / 2, COLOR_ALPHA(THEME_TEXT, ff ? 0x26 : 0x18));
    if (ff) gfx_rrect_ring(x, fy, w, fh, fh / 2, 3, THEME_ACCENT2);
    ui_icon(ICON_SEARCH, x + 48, fy + fh / 2, 32, THEME_TEXT_MUTED);
    font_draw("Find a setting", x + 90, fy + (fh - font_height(FONT_M)) / 2, FONT_M, THEME_TEXT_MUTED);
    ui_hit(x, fy, w, fh, HIT_FIND);
    float y = PH_CAT_Y, rh = PH_CAT_H;
    for (int i = 0; i < CAT_COUNT; i++, y += rh) {
        int foc = !st->search_foc && st->cat == i;
        if (foc) { ui_focus_under(x - 12, y, w + 24, rh - 8, R_PANEL, 1); gfx_rrect(x - 12, y, w + 24, rh - 8, R_PANEL, COLOR_ALPHA(THEME_TEXT, 0x14)); }
        gfx_rrect(x, y + (rh - 8 - 76) / 2, 76, 76, 20, COLOR_ALPHA(THEME_ACCENT, 0x24));
        ui_icon(CAT_ICONS[i], x + 38, y + (rh - 8) / 2, 34, THEME_ACCENT);
        font_draw(CAT_NAMES[i], x + 104, y + 22, FONT_M | FONT_B, THEME_TEXT);
        font_draw_fit(CAT_DESC[i], x + 104, y + 66, FONT_XS, THEME_TEXT_DIM, w - 104 - 50);
        ui_icon(ICON_CHEVRON_RIGHT, x + w - 14, y + (rh - 8) / 2, 26, THEME_TEXT_MUTED);
        gfx_rect(x + 104, y + rh - 4, w - 104, 2, THEME_LINE);
        ui_hit(x - 12, y, w + 24, rh - 8, HIT_CAT + i);
    }
    gfx_set_translate(0, 0);
    rail_draw(RAIL_SETTINGS, -1);
}

static void phone_header(const char *title)
{
    gfx_rect(0, 0, SNTV_UI_W, 140, THEME_BG);
    gfx_circle(80, 76, 40, COLOR_ALPHA(THEME_TEXT, 0x16));
    ui_icon(ICON_CHEVRON_LEFT, 78, 76, 32, THEME_TEXT);
    ui_hit(30, 26, 100, 100, HIT_BACK);
    font_draw_fit(title, 144, 76 - font_height(FONT_L | FONT_B) / 2, FONT_L | FONT_B, THEME_TEXT, SNTV_UI_W - 184);
}

static void phone_edit(const Settings *st)
{
    float x = 40, w = SNTV_UI_W - 80;
    gfx_rect(0, 0, SNTV_UI_W, SNTV_UI_H, THEME_BG);
    phone_header(edit_title(st->edit));
    float y = 170;
    y += font_draw_wrapped(edit_hint(st->edit), x, y, FONT_S, THEME_TEXT_DIM, w, 5) * font_height(FONT_S) + 30;
    gfx_rrect(x, y, w, 100, R_PANEL, COLOR_ALPHA(THEME_TEXT, 0x1C));
    gfx_rrect_ring(x, y, w, 100, R_PANEL, 3, THEME_ACCENT2);
    const char *t = st->kb.text;
    float tx = x + 30, tw = font_measure(t, FONT_M);
    gfx_clip_push(x + 20, y, w - 40, 100);
    if (tw > w - 70) tx -= tw - (w - 70);   /* the end of a long key in view */
    font_draw(t[0] ? t : " ", tx, y + (100 - font_height(FONT_M)) / 2, FONT_M, THEME_TEXT);
    if (fmodf(g_app.time, 1.0f) < 0.55f) gfx_rect(tx + tw + 4, y + 26, 3, 48, THEME_ACCENT2);
    gfx_clip_pop();
    app_redraw();
    y += 140;
    float bw = (w - 20) / 2;
    gfx_rrect(x, y, bw, 96, 48, COLOR_ALPHA(THEME_TEXT, 0x1C));
    font_draw_center("Cancel", x + bw / 2, y + (96 - font_height(FONT_M | FONT_B)) / 2, FONT_M | FONT_B, THEME_TEXT);
    ui_hit(x, y, bw, 96, HIT_BACK);
    gfx_rrect(x + bw + 20, y, bw, 96, 48, THEME_ACCENT);
    font_draw_center("Done", x + bw + 20 + bw / 2, y + (96 - font_height(FONT_M | FONT_B)) / 2, FONT_M | FONT_B, THEME_ON_ACCENT);
    ui_hit(x + bw + 20, y, bw, 96, HIT_DONE);
}

static void draw(Screen *s)
{
    Settings *st = s->st;
    ui_background(0);
    if (SNTV_PHONE && st->edit != EDIT_NONE && st->edit != EDIT_BACKUP) { phone_edit(st); return; }
    if (SNTV_PHONE && st->zone == 0 && st->edit == EDIT_NONE) { gfx_rect(0, 0, SNTV_UI_W, SNTV_UI_H, THEME_BG); phone_categories(st); return; }
    float e = st->wide_t * st->wide_t * (3 - 2 * st->wide_t);   /* smoothstep */
    if (SNTV_PHONE) { s_x = 40; s_r = SNTV_UI_W - 40; }
    else {
        draw_categories(st, e);
        s_x = COL_X0 + (COL_X1 - COL_X0) * e;
        s_r = SNTV_UI_W - THEME_MARGIN + (COL_R1 - (SNTV_UI_W - THEME_MARGIN)) * e;
    }

    if (st->edit == EDIT_BACKUP) {   /* no typing: the phone does it all */
        float x = COL_X0;
        font_draw(edit_title(st->edit), x, 100, FONT_L | FONT_B, THEME_TEXT);
        font_draw_wrapped(edit_hint(st->edit), x, 156, FONT_S, THEME_TEXT_DIM, 1100, 2);
        if (st->qr_ready) {
            ui_qr(st->qr, x, 260, 320);
            font_draw(st->phone_url, x + 360, 270, FONT_M | FONT_B, THEME_TEXT);
            font_draw_wrapped("Open this on a phone on the same network. A restored backup applies at once.", x + 360, 320, FONT_S, THEME_TEXT_DIM, 640, 3);
        } else font_draw("The phone page is not available on this network.", x, 280, FONT_S, THEME_TEXT_DIM);
        font_draw("Back closes", x, 640, FONT_S, THEME_TEXT_MUTED);
        return;
    }
    if (st->edit != EDIT_NONE) {
        const char *title = edit_title(st->edit), *hint = edit_hint(st->edit);
        float x = COL_X0;
        font_draw(title, x, 100, FONT_L | FONT_B, THEME_TEXT);
        font_draw(hint, x, 156, FONT_S, THEME_TEXT_DIM);
        gfx_rrect(x, 210, keyboard_width(), 76, R_PANEL, COLOR_ALPHA(THEME_TEXT, 0x18));
        font_draw_fit(st->kb.text[0] ? st->kb.text : " ", x + 32, 210 + (76 - font_height(FONT_M | FONT_B)) / 2, FONT_M | FONT_B, THEME_TEXT, keyboard_width() - 64);
        keyboard_draw(&st->kb, x, 320, 1);
        font_draw("Done saves  \xC2\xB7  Back cancels", x, 320 + keyboard_height() + 30, FONT_S, THEME_TEXT_MUTED);
        if (st->qr_ready) {   /* or type it on the phone */
            float qx = x + keyboard_width() + 70, qy = 320, qs = 250;
            if (qx + qs < SNTV_UI_W - 40) {
                ui_qr(st->qr, qx, qy, qs);
                font_draw("Or type it on your phone", qx, qy + qs + 22, FONT_S | FONT_B, THEME_TEXT);
                font_draw_wrapped("Scan, paste the text, Send. It lands here and saves.", qx, qy + qs + 56, FONT_XS, THEME_TEXT_DIM, 380, 2);
                font_draw(st->phone_url, qx, qy + qs + 112, FONT_XS, THEME_TEXT_MUTED);
            }
        }
        return;
    }

    /* pane header */
    if (SNTV_PHONE) {}
    else if (g_theme.id == THEME_APERTURE) font_draw(CAT_NAMES[st->cat], s_x, 82, FONT_XL | FONT_ALT, THEME_TEXT);
    else font_draw(CAT_NAMES[st->cat], s_x, 100, FONT_L | FONT_B, THEME_TEXT);
    if (!SNTV_PHONE) font_draw_wrapped(CAT_DESC[st->cat], s_x, 156, FONT_S, THEME_TEXT_DIM, s_r - s_x, 2);

    static const char *FX_NAMES[] = { "Automatic", "Full", "Light" };
    static const char *SIZE_NAMES[] = { "Small", "Medium", "Large", "Extra large" };
    static const char *CLOCK_NAMES[] = { "12-hour", "24-hour", "Off" };
    static const char *SKIP_NAMES[] = { "Show a Skip button", "Skip automatically", "Off" };
    int ids[MAX_ROWS + 8];
    int n = rows_for(st, st->cat, ids, MAX_ROWS + 8);
    if (st->focus >= n) st->focus = n ? n - 1 : 0;
    gfx_clip_push(s_x - 40, ROWS_TOP - 20, s_r - s_x + 80, SNTV_UI_H - ROWS_TOP + 20);
    ui_pan_area(&st->pan, s_x - 40, ROWS_TOP - 20, s_r - s_x + 80, SNTV_UI_H - ROWS_TOP + 20, 1);
    { float e = 1 - (1 - st->pane_t) * (1 - st->pane_t); gfx_set_translate(40 * (1 - e), 0); }
    float y = ROWS_TOP - st->scroll;
    for (int i = 0; i < n; y += row_h(ids[i]), i++) {
        int foc = st->zone == 1 && st->focus == i;
        if (!foc && (y + ROW_H < ROWS_TOP - 20 || y > SNTV_UI_H)) continue;   /* the focused row always draws (the clip hides it), so the panel describes it */
        int id = ids[i];
        char v[300];
        if (IS_SEC(id)) { draw_section(y, SEC_NAMES[id - IT_SEC_BASE]); continue; }
        ui_hit(s_x, y, s_r - s_x, ROW_H - 12, i);
        s_hit_row = i;
        s_desc = row_desc(id);
        s_steps = scale_of(id, &s_step);
        s_chevron = !(id >= IT_ADDON_BASE && id < IT_ACCT_BASE);
        switch (id) {
        case IT_SYNC_DEVICES: draw_row(y, ICON_SWAP, "Sync with another device", NULL, foc, KIND_ACTION, 0); break;
        case IT_ACCOUNT:
            draw_row(y, ICON_USER, "Sync with Stremio", NULL, foc, KIND_TOGGLE, g_app.auth_key != NULL);
            break;
        case IT_SERVER:
            draw_row(y, ICON_LINK, "Streaming server", g_app.streaming_server && *g_app.streaming_server ? g_app.streaming_server : "Not set  \xC2\xB7  needed for torrents", foc, KIND_ACTION, 0);
            break;
        case IT_PREF_AUDIO: draw_row(y, ICON_AUDIO, "Preferred audio", lang_name(g_app.pref_audio), foc, KIND_CHOICE, 0); break;
        case IT_PREF_SUBS: draw_row(y, ICON_SUBS, "Preferred subtitles", lang_name(g_app.pref_subs), foc, KIND_CHOICE, 0); break;
        case IT_SUBS_SIZE: draw_row(y, ICON_SUBS, "Subtitle size", SIZE_NAMES[g_app.subs_size], foc, KIND_CHOICE, 0); break;
        case IT_SUBS_STYLE: draw_row(y, ICON_SUBS, "Subtitle style", SUBS_STYLE_NAMES[g_app.subs_style], foc, KIND_CHOICE, 0); break;
        case IT_SUBS_COLOUR: draw_row(y, ICON_SUBS, "Subtitle colour", SUBS_COLOUR_NAMES[g_app.subs_colour], foc, KIND_CHOICE, 0); break;
        case IT_SUBS_SYNC: draw_row(y, ICON_SUBS, "Auto-sync subtitles to the file", NULL, foc, KIND_TOGGLE, g_app.subs_autosync); break;
        case IT_SUBS_FILES: draw_row(y, ICON_SUBS, "Subtitle files over the video's own", NULL, foc, KIND_TOGGLE, g_app.subs_prefer_files); break;
        case IT_BUFFER: draw_row(y, ICON_SPEED, "Load ahead", NULL, foc, KIND_TOGGLE, store_get_int("bufferAhead", 1)); break;
        case IT_TV_KB: draw_row(y, ICON_SEARCH, "Type with the TV's keyboard", NULL, foc, KIND_TOGGLE, store_get_int("tvKeyboard", TV_KB_DEFAULT)); break;
        case IT_BR_VIDEO: draw_row(y, ICON_TV, "Show the video", NULL, foc, KIND_TOGGLE, g_app.break_video); break;
        case IT_P2P: draw_row(y, ICON_SWAP, "P2P fallback on this TV", NULL, foc, KIND_TOGGLE, g_app.p2p_fallback); break;
        case IT_SRC_AUDIO: { char la[96]; snprintf(la, sizeof la, "Sources in my audio language first%s%s%s", lang_known(g_app.pref_audio) ? " (" : "", lang_known(g_app.pref_audio) ? lang_display(g_app.pref_audio) : "", lang_known(g_app.pref_audio) ? ")" : ""); draw_row(y, ICON_AUDIO, la, NULL, foc, KIND_TOGGLE, g_app.src_audio_lang); break; }
        case IT_ADDON_WAIT: if (g_app.addon_wait >= 99) snprintf(v, sizeof v, "Every addon"); else snprintf(v, sizeof v, "%d s", g_app.addon_wait); draw_row(y, ICON_CLOCK, "Autoplay waits for slow addons", v, foc, KIND_CHOICE, 0); break;
        case IT_SUBS_XL: draw_row(y, ICON_SPARK, "Translate with AI when nobody offers my language", NULL, foc, KIND_TOGGLE, g_app.subs_autotranslate); break;
        case IT_AUTOPLAY: draw_row(y, ICON_NEXT, "Play next episode automatically", NULL, foc, KIND_TOGGLE, g_app.autoplay_next); break;
        case IT_SKIP: draw_row(y, ICON_NEXT, "Skip intros and recaps", SKIP_NAMES[g_app.skip_mode], foc, KIND_CHOICE, 0); break;
        case IT_ADVISORY: draw_row(y, ICON_INFO, "Content advisory", NULL, foc, KIND_TOGGLE, g_app.advisory); break;
        case IT_LETTERBOX: draw_row(y, ICON_ASPECT, "Living letterbox (experimental)", LB_STYLE_NAMES[g_app.living_letterbox], foc, KIND_CHOICE, 0); break;
        case IT_NIGHT: draw_row(y, ICON_MOON, "Night mode", g_app.night_mode == 2 ? "Always" : g_app.night_mode ? "11 pm to 6 am" : "Off", foc, KIND_CHOICE, 0); break;
        case IT_SUBS_WHEN: draw_row(y, ICON_GLOBE, "Turn subtitles on", g_app.subs_always ? "Always" : "Only for other languages", foc, KIND_CHOICE, 0); break;
        case IT_CLOCK: draw_row(y, ICON_CLOCK, "Clock", CLOCK_NAMES[g_app.clock_mode], foc, KIND_CHOICE, 0); break;
        case IT_ISLAND_STYLE: draw_row(y, ICON_CLOCK, "Clock style", ISLAND_STYLE_NAMES[g_app.island_style], foc, KIND_CHOICE, 0); break;
        case IT_CLOCK_FACE: draw_row(y, ICON_CLOCK, "Clock face", CLOCKFACE_NAMES[g_app.clock_face], foc, KIND_CHOICE, 0); break;
        case IT_QUALITY: draw_row(y, ICON_SWAP, "Sort sources by", QUAL_MODE_NAMES[g_app.quality_mode], foc, KIND_CHOICE, 0); break;
        case IT_RES_CAP: {
            const char *cap = g_app.res_cap >= 2160 ? "Up to 4K" : g_app.res_cap >= 1080 ? "Up to 1080p" : "Up to 720p";
            draw_row(y, ICON_TV, "Resolution for recommendations", cap, foc, KIND_CHOICE, 0);
            break;
        }
        case IT_SRC_SIZE: if (g_app.src_max_gb) snprintf(v, sizeof v, "Up to %d GB", g_app.src_max_gb); else snprintf(v, sizeof v, "Any size"); draw_row(y, ICON_SPEED, "Largest file to pick", v, foc, KIND_CHOICE, 0); break;
        case IT_SRC_HDR: draw_row(y, ICON_TV, "HDR and Dolby Vision", g_app.src_hdr == 2 ? "Avoid HDR" : g_app.src_hdr == 1 ? "Avoid Dolby Vision" : "Any", foc, KIND_CHOICE, 0); break;
        case IT_SRC_CACHED: draw_row(y, ICON_CHECK, "Only cached or direct sources", NULL, foc, KIND_TOGGLE, g_app.src_cached_only); break;
        case IT_PLAY_LINK: draw_row(y, ICON_PLAY, "Play a link from your phone", NULL, foc, KIND_ACTION, 0); break;
        case IT_BACKUP: draw_row(y, ICON_SETTINGS, "Back up or restore settings", NULL, foc, KIND_ACTION, 0); break;
        case IT_TRAKT: case IT_SIMKL: {
            int svc = id == IT_TRAKT ? TRACK_TRAKT : TRACK_SIMKL;
            const char *st2 = tracking_connected(svc) ? "Connected  \xC2\xB7  OK to disconnect" : tracking_pending(svc) ? "Waiting for the code\xE2\x80\xA6"
                            : tracking_configured(svc) ? "OK to connect" : "Needs your app id first (OK)";
            draw_row(y, ICON_SPARK, TRACK_NAMES[svc], st2, foc, KIND_ACTION, 0);
            break;
        }
        case IT_OS_KEY: draw_row(y, ICON_SUBS, "OpenSubtitles.com", opensubs_enabled() ? "On  \xC2\xB7  searched for every video" : "Off  \xC2\xB7  OK to add an API key", foc, KIND_ACTION, 0); break;
        case IT_OS_LOGIN: { char u[96]; if (opensubs_signed_in()) snprintf(u, sizeof u, "Signed in as %s  \xC2\xB7  OK to sign out", opensubs_user()); else snprintf(u, sizeof u, "Not signed in  \xC2\xB7  a few downloads a day"); draw_row(y, ICON_KEY, "OpenSubtitles account", u, foc, KIND_ACTION, 0); break; }
        case IT_TORBOX_FILES: draw_row(y, ICON_LIBRARY, "My TorBox videos", "Watch anything you keep there", foc, KIND_ACTION, 0); break;
        case IT_TORBOX: draw_row(y, ICON_PLAY, "TorBox", torbox_enabled() ? "Key set  \xC2\xB7  cached torrents play from TorBox" : "Not set  \xC2\xB7  OK to add your API key", foc, KIND_ACTION, 0); break;
        case IT_FIRE_TORBOX: case IT_BR_TORBOX: draw_row(y, ICON_PLAY, "Choose from my TorBox", "Your videos, at full quality", foc, KIND_ACTION, 0); break;
        case IT_MDBLIST: draw_row(y, ICON_STAR, "MDBList ratings", mdblist_enabled() ? "Key set  \xC2\xB7  more ratings on title pages" : "Not set  \xC2\xB7  OK to add a key", foc, KIND_ACTION, 0); break;
        case IT_TVCAP: draw_row(y, ICON_TV, "What this TV can play", NULL, foc, KIND_ACTION, 0); break;
        case IT_SRC_EXCLUDE: draw_row(y, ICON_CLOSE, "Words to avoid in source names", g_app.src_exclude[0] ? g_app.src_exclude : "None  \xC2\xB7  OK to add, e.g. CAM, HDTS, 3D", foc, KIND_ACTION, 0); break;
        case IT_PREFLIGHT: draw_row(y, ICON_CHECK, "Check sources before recommending", NULL, foc, KIND_TOGGLE, g_app.preflight); break;
        case IT_SCRUB: draw_row(y, ICON_PLAY, "Show the picture while seeking", NULL, foc, KIND_TOGGLE, g_app.live_scrub); break;
        case IT_THUMBS: draw_row(y, ICON_ASPECT, "Home helper (optional)", g_app.thumb_server ? g_app.thumb_server : "Not needed: the TV makes seek previews and saves YouTube links itself", foc, KIND_ACTION, 0); break;
        case IT_PIN: draw_row(y, ICON_LOCK, g_app.parental_pin[0] ? "Parental PIN" : "Set a parental PIN", g_app.parental_pin[0] ? "Set  \xC2\xB7  OK to remove it (asks for the PIN)" : "Four digits; needed for the switches below", foc, KIND_ACTION, 0); break;
        case IT_PIN_SETTINGS: draw_row(y, ICON_SETTINGS, "Ask for the PIN to open Settings", NULL, foc, KIND_TOGGLE, g_app.pin_settings); break;
        case IT_HIDE_ADULT: draw_row(y, ICON_EYE, "Hide catalogs of addons flagged adult", NULL, foc, KIND_TOGGLE, g_app.hide_adult); break;
        case IT_TMDB: {
            char masked[96] = "Not set  \xC2\xB7  OK to add a key for the built-in catalog";
            if (tmdb_builtin()) snprintf(masked, sizeof masked, "Built-in key in use  \xC2\xB7  catalog, cast and ratings on");
            if (g_app.tmdb_key) snprintf(masked, sizeof masked, "Your key %.4s\xE2\x80\xA6%s  \xC2\xB7  catalog, cast and ratings on", g_app.tmdb_key, strlen(g_app.tmdb_key) > 4 ? g_app.tmdb_key + strlen(g_app.tmdb_key) - 4 : "");
            draw_row(y, ICON_KEY, "The Movie Database (TMDB)", masked, foc, KIND_ACTION, 0);
            break;
        }
        case IT_AI_PROVIDER: {
            draw_row(y, ICON_SPARK, "AI assistant", g_app.ai_provider ? AI_PROVIDER_NAMES[g_app.ai_provider] : "Off  \xC2\xB7  free keys from Groq, OpenRouter or Google", foc, KIND_CHOICE, 0);
            break;
        }
        case IT_AI_KEY: {
            char masked[160];
            if (g_app.ai_key) snprintf(masked, sizeof masked, "%.4s\xE2\x80\xA6%s  \xC2\xB7  %s", g_app.ai_key, strlen(g_app.ai_key) > 4 ? g_app.ai_key + strlen(g_app.ai_key) - 4 : "", ai_enabled() ? "on" : "not enough to run");
            else snprintf(masked, sizeof masked, "%s", g_app.ai_provider == AI_CUSTOM ? "Not set (fine for Ollama)" : ai_key_hint(g_app.ai_provider));
            draw_row(y, ICON_KEY, "AI API key", masked, foc, KIND_ACTION, 0);
            break;
        }
        case IT_AI_BASE: draw_row(y, ICON_LINK, "AI endpoint", g_app.ai_base ? g_app.ai_base : ai_key_hint(AI_CUSTOM), foc, KIND_ACTION, 0); break;
        case IT_AI_MODEL: {
            char m[160];
            snprintf(m, sizeof m, "%s%s", ai_model()[0] ? ai_model() : "Not set", g_app.ai_model && g_app.ai_model[0] ? "" : "  \xC2\xB7  preset; OK to change");
            draw_row(y, ICON_SETTINGS, "AI model", m, foc, KIND_ACTION, 0);
            break;
        }
        case IT_MEMORY: {
            static const char *MEM[] = { "Automatic", "Low", "Normal" };
            snprintf(v, sizeof v, "%s%s%s%s", MEM[g_app.memory_mode], g_app.memory_mode == 0 ? (g_app.low_memory ? " (Low  \xC2\xB7  " : " (Normal  \xC2\xB7  ") : "", g_app.memory_mode == 0 ? mem_reason_text() : "", g_app.memory_mode == 0 ? ")" : "");
            draw_row(y, ICON_SETTINGS, "Memory use", v, foc, KIND_CHOICE, 0);
            break;
        }
        case IT_TRANSITIONS: draw_row(y, ICON_SPARK, "Animated screen changes", NULL, foc, KIND_TOGGLE, g_app.transitions); break;
        case IT_DEPTH: draw_row(y, ICON_SPARK, "Depth on backdrops", NULL, foc, KIND_TOGGLE, g_app.depth_motion); break;
        case IT_PIP: draw_row(y, ICON_ASPECT, "Picture-in-picture", NULL, foc, KIND_TOGGLE, g_app.pip_enabled); break;
        case IT_BR_TEST: draw_row(y, ICON_PLAY, "Try a break now", "See and hear what a break looks like", foc, KIND_ACTION, 0); break;
        case IT_BR_ON: draw_row(y, ICON_CLOCK, "Take breaks", NULL, foc, KIND_TOGGLE, g_app.breaks_on); break;
        case IT_BR_SCHED: draw_row(y, ICON_CALENDAR, "Schedule", BRK_SCHED_NAMES[g_app.break_sched], foc, KIND_CHOICE, 0); break;
        case IT_BR_METHOD: {
            char mv[96];
            if (g_app.break_method < 0) snprintf(mv, sizeof mv, "Auto \xC2\xB7 %s", SUN_METHOD_NAMES[breaks_method()]);
            else snprintf(mv, sizeof mv, "%s", SUN_METHOD_NAMES[g_app.break_method]);
            draw_row(y, ICON_COMPASS, "Calculation method", mv, foc, KIND_CHOICE, 0);
            break;
        }
        case IT_BR_ASR: draw_row(y, ICON_COMPASS, "Asr", g_app.break_asr_hanafi ? "Hanafi (later)" : "Standard (Shafi, Maliki, Hanbali)", foc, KIND_CHOICE, 0); break;
        case IT_BR_LOCATION:
            if (breaks_location_known()) snprintf(v, sizeof v, "%.3f, %.3f%s%s", g_app.break_lat, g_app.break_lon, g_app.break_city[0] ? "  \xC2\xB7  " : "", g_app.break_city);
            else snprintf(v, sizeof v, "Not set  \xC2\xB7  needed for sun-based times");
            draw_row(y, ICON_COMPASS, "Location", v, foc, KIND_ACTION, 0);
            break;
        case IT_BR_DETECT: draw_row(y, ICON_GLOBE, breaks_detecting() ? "Finding your location\xE2\x80\xA6" : "Find my location from the internet", "Uses ip-api.com; usually right to the city", foc, KIND_ACTION, 0); break;
        case IT_BR_TODAY: { char t[300]; breaks_describe_today(t, sizeof t); draw_row(y, ICON_CALENDAR, "Today", t, foc, KIND_ACTION, 0); break; }
        case IT_BR_LEAD: snprintf(v, sizeof v, "%d min before", g_app.break_lead_min); draw_row(y, ICON_BELL, "Reminder banner", v, foc, KIND_CHOICE, 0); break;
        case IT_BR_LEN:
            if (g_app.break_len_min < 0) snprintf(v, sizeof v, "%s", g_app.break_sound == 1 ? "Same as the recording" : "Same as the recording (1 min)");
            else if (g_app.break_len_min) snprintf(v, sizeof v, "%d min", g_app.break_len_min);
            else snprintf(v, sizeof v, "Until I press OK");
            draw_row(y, ICON_CLOCK, "Break length", v, foc, KIND_CHOICE, 0);
            break;
        case IT_BR_SOUND: { static const char *S[] = { "Soft chime", "My own recording", "Silent" }; draw_row(y, ICON_BELL, "Sound", S[g_app.break_sound], foc, KIND_CHOICE, 0); break; }
        case IT_BR_AUDIO: if (torbox_is_link(g_app.break_audio)) { char tb[300], nm[256]; snprintf(tb, sizeof tb, "TorBox: %s", torbox_name(g_app.break_audio, nm, sizeof nm)); draw_row(y, ICON_AUDIO, "Recording at the start", tb, foc, KIND_ACTION, 0); break; }
            draw_row(y, ICON_AUDIO, "Recording at the start", !g_app.break_audio ? "Not set  \xC2\xB7  send one from your phone, or a link (adhan, bell, chant)" : strncmp(g_app.break_audio, "sntv-sound://", 13) == 0 ? "Your recording, stored on the TV  \xC2\xB7  OK to replace" : g_app.break_audio, foc, KIND_ACTION, 0); break;
        case IT_BR_END: draw_row(y, ICON_AUDIO, "Recording at the end", !g_app.break_end ? "Not set  \xC2\xB7  played before playback continues (a dua, a closing bell)" : strncmp(g_app.break_end, "sntv-sound://", 13) == 0 ? "Your recording, stored on the TV  \xC2\xB7  OK to replace" : g_app.break_end, foc, KIND_ACTION, 0); break;
        case IT_BR_HEALTH: if (g_app.break_health_min) snprintf(v, sizeof v, "Every %d min of watching", g_app.break_health_min); else snprintf(v, sizeof v, "Off"); draw_row(y, ICON_STAR, "Stretch reminder", v, foc, KIND_CHOICE, 0); break;
        case IT_BR_HEALTH_LEN: snprintf(v, sizeof v, "%d min", g_app.break_health_len); draw_row(y, ICON_STAR, "Stretch length", v, foc, KIND_CHOICE, 0); break;
        case IT_BR_BED: if (g_app.break_bed >= 0) snprintf(v, sizeof v, "%d:%02d", g_app.break_bed / 60, g_app.break_bed % 60); else snprintf(v, sizeof v, "Off"); draw_row(y, ICON_MOON, "Bedtime", v, foc, KIND_CHOICE, 0); break;
        case IT_BR_CUSTOM1: case IT_BR_CUSTOM2: case IT_BR_CUSTOM3: {
            int m = g_app.break_custom[id - IT_BR_CUSTOM1]; char t[32];
            snprintf(t, sizeof t, "My time %d", id - IT_BR_CUSTOM1 + 1);
            if (m >= 0) snprintf(v, sizeof v, "%d:%02d", m / 60, m % 60); else snprintf(v, sizeof v, "Off");
            draw_row(y, ICON_CLOCK, t, v, foc, KIND_CHOICE, 0);
            break;
        }
        case IT_BR_WHERE: draw_row(y, ICON_HOME, "Breaks apply", g_app.breaks_everywhere ? "Everywhere in the app" : "Only while a video plays", foc, KIND_CHOICE, 0); break;
        case IT_AUTO_REC: draw_row(y, ICON_PLAY, "Play the recommended source automatically", NULL, foc, KIND_TOGGLE, g_app.auto_recommended); break;
        case IT_THEME: draw_row(y, ICON_PALETTE, "Theme", THEME_NAMES[g_app.theme], foc, KIND_CHOICE, 0); break;
        case IT_CARD_DETAILS: draw_row(y, ICON_INFO, "Show details when a card stays focused", NULL, foc, KIND_TOGGLE, g_app.card_details); break;
        case IT_AUTO_TRAILER:   /* Aperture's artwork windows stay still */
            draw_row(y, ICON_PLAY, "Trailers play on their own", g_app.auto_trailers == 2 ? "With sound" : g_app.auto_trailers ? "Muted" : "Off", foc, KIND_CHOICE, 0);
            break;
        case IT_PALETTE: {   /* each palette as its background with the accent across it, the current one framed */
            int cur = theme_palette_index(g_app.theme_accent, g_app.theme_bg);
            char name[80];
            if (cur >= 0) snprintf(name, sizeof name, "%s on %s", theme_accent_name(g_app.theme, THEME_PALETTE[cur][0]), THEME_BG_NAMES[g_app.theme][THEME_PALETTE[cur][1]]);
            else snprintf(name, sizeof name, "Your own mix");
            s_desc = " ";
            draw_row(y, ICON_PALETTE, "Colour palette", name, foc, KIND_CHOICE, 0);
            float sw = 40, sh = 24, step = 52, sx = s_x + 92, sy = y + (ROW_H - 12) / 2 + 4;
            for (int i = 0; i < THEME_PALETTES; i++) {
                float bx = sx + i * step;
                if (i == cur) gfx_rrect_ring(bx - 3, sy - 3, sw + 6, sh + 6, 5, 2.5f, foc && g_theme.id != THEME_APERTURE ? THEME_ON_FOCUS : THEME_TEXT);
                gfx_rrect(bx, sy, sw, sh, 3, theme_bg_swatch(g_app.theme, THEME_PALETTE[i][1]));
                gfx_rrect(bx + 6, sy + sh / 2 - 3, sw - 12, 6, 3, theme_accent_swatch(g_app.theme, THEME_PALETTE[i][0]));
                gfx_rrect_ring(bx, sy, sw, sh, 3, 1, COLOR_ALPHA(THEME_TEXT, 0x40));
            }
            break;
        }
        case IT_ACCENT: case IT_BG: {
            /* the swatches sit on the help line, the current one framed */
            int bg = id == IT_BG, n = bg ? THEME_BGS : THEME_ACCENTS + 1, cur = bg ? g_app.theme_bg : g_app.theme_accent;
            s_desc = " ";
            draw_row(y, ICON_PALETTE, bg ? "Background" : "Accent colour", bg ? THEME_BG_NAMES[g_app.theme][cur] : theme_accent_name(g_app.theme, cur), foc, KIND_CHOICE, 0);
            float sz = bg ? 26 : 18, step = bg ? 40 : 28, sx = s_x + 92, sy = y + (ROW_H - 12) / 2 + 6;
            for (int i = 0; i < n; i++) {
                float bx = sx + i * step;
                Color c = bg ? theme_bg_swatch(g_app.theme, i) : theme_accent_swatch(g_app.theme, i);
                if (i == cur) gfx_rrect_ring(bx - 3, sy - 3, sz + 6, sz + 6, theme_pill_r(sz + 6), 2.5f, foc ? THEME_ON_FOCUS : THEME_TEXT);
                if (!bg && i == THEME_ACCENT_ART) {   /* the artwork's colour: a little spectrum */
                    gfx_rrect_grad_h(bx, sy, sz / 2, sz, theme_pill_r(sz) * 0.5f, RGBA(0xFF, 0x5C, 0x7A, 0xFF), RGBA(0xFF, 0xD8, 0x3D, 0xFF));
                    gfx_rrect_grad_h(bx + sz / 2, sy, sz / 2, sz, theme_pill_r(sz) * 0.5f, RGBA(0x3D, 0xE0, 0xFF, 0xFF), RGBA(0x9D, 0x7B, 0xFF, 0xFF));
                    continue;
                }
                gfx_rrect(bx, sy, bg ? sz * 1.4f : sz, sz, theme_pill_r(sz), c);
                gfx_rrect_ring(bx, sy, bg ? sz * 1.4f : sz, sz, theme_pill_r(sz), 1, COLOR_ALPHA(foc ? THEME_ON_FOCUS : THEME_TEXT, 0x50));   /* a white swatch on a white row */
            }
            break;
        }
        case IT_HOME_STYLE: draw_row(y, ICON_HOME, "Home screen", HOME_STYLE_NAMES[g_app.home_style], foc, KIND_CHOICE, 0); break;
        case IT_HIDE_WATCHED: draw_row(y, ICON_EYE, "Hide watched titles in catalogs", NULL, foc, KIND_TOGGLE, g_app.hide_watched); break;
        case IT_FX:
            snprintf(v, sizeof v, "%s%s", FX_NAMES[g_app.fx_setting], g_app.fx_setting == FX_AUTO ? (g_app.fx_light ? " (Light)" : g_app.frames_measured >= 240 ? " (Full)" : "") : "");
            draw_row(y, ICON_SPARK, "Visual effects", v, foc, KIND_CHOICE, 0);
            break;
        case IT_OLED: draw_row(y, ICON_MOON, "Pure black (OLED)", NULL, foc, KIND_TOGGLE, g_app.oled); break;
        case IT_ASK_PROFILE: draw_row(y, ICON_USER, "Ask who's watching at start", NULL, foc, KIND_TOGGLE, g_app.ask_profile); break;
        case IT_AMBIENT:
            if (g_app.ambient_minutes) snprintf(v, sizeof v, "After %d min", g_app.ambient_minutes); else snprintf(v, sizeof v, "Off");
            draw_row(y, ICON_TV, "Screensaver", v, foc, KIND_CHOICE, 0);
            break;
        case IT_FIRE_VIDEO: {
            const char *fv = store_get_str("fireplaceVideo", "");
            char tb[300], nm[256];
            if (torbox_is_link(fv)) snprintf(tb, sizeof tb, "TorBox: %s", torbox_name(fv, nm, sizeof nm));
            draw_row(y, ICON_PLAY, "Fireplace video", torbox_is_link(fv) ? tb : fv && *fv ? fv : "Not set  \xC2\xB7  OK to add a YouTube link", foc, KIND_ACTION, 0);
            break;
        }
        case IT_AMBIENT_STYLE: draw_row(y, ICON_TV, "Screensaver style", AMBIENT_STYLE_NAMES[g_app.ambient_style], foc, KIND_CHOICE, 0); break;
        case IT_AMBIENT_DIM:
            if (g_app.ambient_dim_min) snprintf(v, sizeof v, "After %d min", g_app.ambient_dim_min); else snprintf(v, sizeof v, "Never");
            draw_row(y, ICON_MOON, "Dim the screensaver", v, foc, KIND_CHOICE, 0);
            break;
        case IT_AMBIENT_OFF:
            if (g_app.ambient_off_min >= 60) snprintf(v, sizeof v, "After %d h", g_app.ambient_off_min / 60); else if (g_app.ambient_off_min) snprintf(v, sizeof v, "After %d min", g_app.ambient_off_min); else snprintf(v, sizeof v, "Never");
            draw_row(y, ICON_MOON, "Picture off", v, foc, KIND_CHOICE, 0);
            break;
        case IT_BLUR: draw_row(y, ICON_EYE, "Blur unwatched episodes", NULL, foc, KIND_TOGGLE, g_app.spoiler_blur); break;
        case IT_DIAG: {   /* what this TV runs, at a glance */
            char mem[160], info[300];
            mem_describe(mem, sizeof mem);
            snprintf(info, sizeof info, "%s  \xC2\xB7  %zu MB artwork  \xC2\xB7  %s", video_backend_name(), image_cache_bytes() >> 20, mem);
            draw_row(y, ICON_SETTINGS, "Diagnostics", info, foc, KIND_ACTION, 0);
            break;
        }
        case IT_STORAGE: {   /* what the app keeps here; the cache comes back by itself */
            long long total, cache, disk_free;
            char v[200], fr[48] = "";
            app_storage(&total, &cache, &disk_free);
            if (disk_free >= 0) snprintf(fr, sizeof fr, disk_free >= (1LL << 30) ? "%.1f GB free" : "%.0f MB free", disk_free >= (1LL << 30) ? disk_free / 1073741824.0 : disk_free / 1048576.0);
            if (total < 0) snprintf(v, sizeof v, "Counting\xE2\x80\xA6");
            else if (SNTV_PHONE) snprintf(v, sizeof v, "%lld MB  \xC2\xB7  %lld MB cache", (total + (1 << 19)) >> 20, (cache + (1 << 19)) >> 20);
            else snprintf(v, sizeof v, "%lld MB  \xC2\xB7  %lld MB cache%s%s", (total + (1 << 19)) >> 20, (cache + (1 << 19)) >> 20, fr[0] ? "  \xC2\xB7  TV: " : "", fr);
            draw_row(y, ICON_LIBRARY, "Storage", v, foc, KIND_ACTION, 0);
            break;
        }
        case IT_ABOUT_APP: draw_row(y, ICON_INFO, "About " SNTV_APP_NAME, "Version " SNTV_APP_VERSION, foc, KIND_ACTION, 0); break;
        case IT_WHATSNEW: draw_row(y, ICON_CHECK, "What's new in " SNTV_APP_VERSION, NULL, foc, KIND_ACTION, 0); break;
        case IT_UPDATE: {
            char v[160];
            if (g_update_version[0]) snprintf(v, sizeof v, app_update_installs_here() ? "Version %s is ready  \xC2\xB7  OK installs it now" : "Version %s is ready  \xC2\xB7  OK shows how to install it", g_update_version);
            else snprintf(v, sizeof v, "%s is the newest we know of  \xC2\xB7  OK checks", SNTV_APP_VERSION);
            draw_row(y, ICON_RELOAD, "Update", v, foc, KIND_ACTION, 0);
            break;
        }
        case IT_AUTO_UPDATE: draw_row(y, ICON_RELOAD, "Ask me when an update is out", NULL, foc, KIND_TOGGLE, g_app.auto_update); break;
        case IT_SETUP: draw_row(y, ICON_CHECK, "Run quick setup again", "Languages, skipping, account", foc, KIND_ACTION, 0); break;
        case IT_INSTALL: draw_row(y, ICON_PLUS, "Install addon from URL", "manifest.json", foc, KIND_ACTION, 0); break;
        default:
            if (id >= IT_ACCT_BASE) {
                char t[160]; snprintf(t, sizeof t, "Switch to %s", st->accounts[id - IT_ACCT_BASE]);
                draw_row(y, ICON_USER, t, "Saved sign-in  \xC2\xB7  OK to switch or forget", foc, KIND_ACTION, 0);
            } else if (id >= IT_ADDON_BASE) {
                const Addon *a = &g_app.addons[id - IT_ADDON_BASE];
                char title[200];
                snprintf(title, sizeof title, "%d.  %s", id - IT_ADDON_BASE + 1, a->manifest.name);
                if (st->confirm_remove == id - IT_ADDON_BASE) snprintf(v, sizeof v, "Press OK again to remove");
                else {
                    snprintf(v, sizeof v, "v%s  \xC2\xB7  ", a->manifest.version ? a->manifest.version : "?");
                    for (int r = 0; r < a->manifest.n_resources; r++) snprintf(v + strlen(v), sizeof v - strlen(v), "%s%s", r ? ", " : "", a->manifest.resources[r].name);
                    if (a->flag_protected) strncat(v, "  \xC2\xB7  built-in", sizeof v - strlen(v) - 1);
                }
                draw_row(y, ICON_LIBRARY, title, v, foc, KIND_ACTION, 0);
                if (foc) { ui_icon(ICON_CHEVRON_LEFT, s_r - 90, y + (ROW_H - 12) / 2, 22, COLOR_ALPHA(THEME_BG, 0xA8)); ui_icon(ICON_CHEVRON_RIGHT, s_r - 56, y + (ROW_H - 12) / 2, 22, COLOR_ALPHA(THEME_BG, 0xA8)); }
            } else if (id >= IT_ROWS_BASE) {
                const HomeRowDef *d = &st->rows[id - IT_ROWS_BASE];
                char pos[32]; snprintf(pos, sizeof pos, "%d of %d", id - IT_ROWS_BASE + 1, st->n_rows);
                draw_row(y, d->kind == 2 ? ICON_LIBRARY : d->kind == 0 ? ICON_PLAY : ICON_STAR, d->title, pos, foc, KIND_MOVABLE, !d->hidden);
            }
        }
    }
    gfx_set_translate(0, 0);
    gfx_clip_pop();
    if (SNTV_PHONE) { phone_header(CAT_NAMES[st->cat]); return; }
    if (e > 0.01f) draw_panel(st, ids, n, e);
}

static const char *edit_title(int which)
{
    if (which == EDIT_SEARCH) return "Find a setting";
    return which == EDIT_AI_KEY ? "AI API key" : which == EDIT_AI_BASE ? "AI endpoint (OpenAI-compatible)" : which == EDIT_AI_MODEL ? "AI model"
         : which == EDIT_FIRE_VIDEO ? "Fireplace video" : which == EDIT_OS_KEY ? "OpenSubtitles.com API key" : which == EDIT_OS_LOGIN ? "OpenSubtitles account"
         : which == EDIT_TRAKT_APP ? "Trakt app" : which == EDIT_SIMKL_APP ? "SimKL app id" : which == EDIT_MDBLIST ? "MDBList API key" : which == EDIT_TORBOX ? "TorBox API key"
         : which == EDIT_PLAY_LINK ? "Play a link" : which == EDIT_BACKUP ? "Back up or restore settings"
         : which == EDIT_EXCLUDE ? "Words to avoid in source names" : which == EDIT_SERVER ? "Streaming server URL" : which == EDIT_THUMBS ? "Home helper" : which == EDIT_TMDB ? "TMDB API key"
         : which == EDIT_LOCATION ? "Your location" : which == EDIT_BR_AUDIO ? "Audio at the start of a break" : which == EDIT_BR_END ? "Audio at the end of a break" : "Addon manifest URL";
}
static const char *edit_hint(int which)
{
    if (which == EDIT_SEARCH) return "A word from its name or from what it does: subtitle, clock, black, PIN, trailer\xE2\x80\xA6";
    return which == EDIT_FIRE_VIDEO ? "Paste a YouTube link of a fireplace (or a direct MP4/M3U8 link). Scan the code to paste it from your phone. Empty removes it."
         : which == EDIT_OS_KEY ? "Free at opensubtitles.com > Profile > API consumers (create one, copy its API key). Empty removes it."
         : which == EDIT_OS_LOGIN ? "Your opensubtitles.com username, a space, then your password. Only the sign-in token is kept on the TV."
         : which == EDIT_TRAKT_APP ? "Create an app at developer.trakt.tv/apps (type Development, redirect http://localhost), then enter its Client ID (and, for older apps that have one, a space and the Client Secret)."
         : which == EDIT_SIMKL_APP ? "Create an app at simkl.com/settings/developer (redirect http://localhost) and enter its Client ID."
         : which == EDIT_MDBLIST ? "Free at mdblist.com > Preferences > API key. Empty removes it."
         : which == EDIT_TORBOX ? "At torbox.app > Settings > API key. Scan the code to paste it from your phone. Empty removes it."
         : which == EDIT_PLAY_LINK ? "A direct video link (MP4, MKV, M3U8). Scan the code to paste it on your phone; it plays at once."
         : which == EDIT_BACKUP ? "Scan the code with your phone: save a backup there, or send one back to restore it on this TV. Your Stremio sign-in is not included."
         : which == EDIT_EXCLUDE ? "Comma-separated, e.g. CAM, HDTS, 3D, HC. Sources with any of them in their name are never picked for you. Empty removes the rule."
         : which == EDIT_SERVER ? "Any machine on your network running Stremio, e.g. http://192.168.1.10:11470"
         : which == EDIT_THUMBS ? "Optional: tools/thumbs/server.py on a computer or NAS, e.g. http://192.168.1.10:11480. The TV already decodes seek previews and saves YouTube links; set this only to use the computer instead. Empty turns it off."
         : which == EDIT_TMDB ? "Free at themoviedb.org > Settings > API (the v3 key, 32 characters). Empty removes it."
         : which == EDIT_AI_KEY ? ai_key_hint(g_app.ai_provider)
         : which == EDIT_AI_BASE ? "The base URL that ends before /chat/completions, e.g. http://192.168.1.10:11434/v1 for Ollama."
         : which == EDIT_AI_MODEL ? "A model name the provider accepts. Empty goes back to the preset."
         : which == EDIT_LOCATION ? "Latitude, longitude  (e.g. 51.5074, -0.1278). Maps apps show them when you long-press a spot."
         : which == EDIT_BR_AUDIO ? "Scan the code to send an audio or video file from your phone, or paste a link: a direct MP3/M4A/MP4, or a YouTube link (the TV saves it once and plays it offline). Empty for none"
         : which == EDIT_BR_END ? "Played when the break ends, before playback continues (a dua, a closing bell). Send a file from your phone or paste a direct link. Empty for none"
         : "Paste or type the addon's manifest.json address";
}

static void phone_qr_make(Settings *st)
{
    st->qr_ready = 0;
    char url[96];
    if (!phone_url(url, sizeof url)) return;
    uint8_t tmp[qrcodegen_BUFFER_LEN_MAX];
    st->qr_ready = qrcodegen_encodeText(url, tmp, st->qr, qrcodegen_Ecc_MEDIUM, qrcodegen_VERSION_MIN, 6, qrcodegen_Mask_AUTO, true);
    snprintf(st->phone_url, sizeof st->phone_url, "%s", url);
}

/* A page link is fetched by the helper once, now, so the break starts instantly. */
static void on_warm(HttpResponse *r, void *user)
{
    (void)user;
    if (r->status == 200 || r->status == 206) app_toast("Ready: it plays at each break");
    else app_toast("The helper could not get it: %.120s", r->status && r->body && r->body[0] ? r->body : r->error);
}
static void warm_media(const char *stored)
{
    char url[1800];
    const char *u = breaks_media_url(stored, url, sizeof url, NULL);
    if (!u) return;
    HttpOptions o = { .range_bytes = 1, .timeout_s = 320, .no_json_accept = 1 };
    http_request_async_ex(u, &o, on_warm, NULL);
    app_toast("The helper is fetching it\xE2\x80\xA6");
}

/* A YouTube link is downloaded by the TV itself; the home helper, when set, is the fallback. */
static void on_youtube_saved(const char *name, const char *error, void *user)
{
    char *url = user;
    if (name) app_toast("Ready: saved on the TV, it plays at each break (offline too)");
    else if (g_app.thumb_server && *g_app.thumb_server) { app_toast("YouTube said no to the TV (%.80s). Trying the home helper\xE2\x80\xA6", error); warm_media(url); }
    else if (strstr(error, "not a bot")) app_toast("YouTube wants a sign-in from this TV right now. Send the file from your phone (scan the code), or try again later");
    else app_toast("Could not save it from YouTube: %.120s", error);
    free(url);
}

static void save_youtube(const char *url)
{
    app_toast("Saving it from YouTube on the TV\xE2\x80\xA6 the first link takes up to a minute");
    youtube_save(url, g_app.break_video, on_youtube_saved, strdup(url));
}

static void begin_edit(Settings *st, int which);

/* AI model: a list of what the key can use, with typing a name as the last choice. */
typedef struct { char ids[8][96]; int n; } ModelChoice;

static void on_model_choice(int c, void *user)
{
    ModelChoice *mc = user;
    Screen *top = app_top();
    if (c < mc->n) { free(g_app.ai_model); g_app.ai_model = strdup(mc->ids[c]); app_save_prefs(); app_toast("AI model: %s", mc->ids[c]); }
    else if (top && strcmp(top->name, "settings") == 0) begin_edit(top->st, EDIT_AI_MODEL);
    free(mc);
}

static void on_models(const char *const *ids, int n, const char *error, void *user)
{
    (void)user;
    Screen *top = app_top();
    if (!top || strcmp(top->name, "settings") != 0) return;
    if (!ids) { app_toast("%s", error ? error : "No models"); begin_edit(top->st, EDIT_AI_MODEL); return; }
    ModelChoice *mc = calloc(1, sizeof *mc);
    const char *items[8];
    for (int i = 0; i < n && mc->n < 7; i++) { snprintf(mc->ids[mc->n], 96, "%s", ids[i]); items[mc->n] = mc->ids[mc->n]; mc->n++; }
    items[mc->n] = "Type a model name\xE2\x80\xA6";
    app_push(screen_options_create("AI model", "The models your key can use, best first", items, mc->n + 1, on_model_choice, mc));
}

/* ---- finding a setting ---- */

#define FOUND_MAX 8   /* what the options sheet holds */
static struct { Settings *st; int n, cat[FOUND_MAX], id[FOUND_MAX]; char label[FOUND_MAX][64]; } g_found;

static int has_ci(const char *s, const char *w, size_t wn)
{
    for (; *s; s++) {
        size_t i = 0;
        while (i < wn && s[i] && tolower((unsigned char)s[i]) == tolower((unsigned char)w[i])) i++;
        if (i == wn) return 1;
    }
    return 0;
}

/* every word of q somewhere in s, ignoring case */
static int words_in(const char *q, const char *s)
{
    if (!s) return 0;
    while (*q) {
        while (*q == ' ') q++;
        size_t n = 0;
        while (q[n] && q[n] != ' ') n++;
        if (n && !has_ci(s, q, n)) return 0;
        q += n;
    }
    return 1;
}

static void jump_to(Settings *st, int cat, int id)
{
    int ids[MAX_ROWS + 8], n = rows_for(st, cat, ids, MAX_ROWS + 8);
    for (int i = 0; i < n; i++)
        if (ids[i] == id) {
            st->cat = cat; st->zone = 1; st->focus = i; st->search_foc = 0; st->scroll = st->scroll_target = 0;
            LOGI("settings: found %s in %s", row_title(id), CAT_NAMES[cat]);
            app_redraw();
            return;
        }
}

static void on_found(int c, void *user) { (void)user; if (c >= 0 && c < g_found.n) jump_to(g_found.st, g_found.cat[c], g_found.id[c]); }

static void find_setting(Settings *st, const char *q)
{
    while (*q == ' ') q++;
    if (!*q) return;
    g_found.st = st; g_found.n = 0;
    for (int pass = 0; pass < 2; pass++)   /* names first, then what the help lines say */
        for (int c = 0; c < CAT_COUNT; c++) {
            if (c == CAT_ROWS || c == CAT_ADDONS) continue;   /* lists of the viewer's own, not settings */
            int ids[MAX_ROWS + 8], n = rows_for(st, c, ids, MAX_ROWS + 8);
            for (int i = 0; i < n && g_found.n < FOUND_MAX; i++) {
                const char *t = row_title(ids[i]);
                if (pass == 1 && ids[i] == IT_ABOUT_APP) continue;   /* prose about the app, not a setting's help */
                if (!t || !(pass == 0 ? words_in(q, t) : !words_in(q, t) && words_in(q, row_desc(ids[i])))) continue;
                int k = g_found.n++;
                g_found.cat[k] = c; g_found.id[k] = ids[i];
                snprintf(g_found.label[k], sizeof g_found.label[k], "%s  \xC2\xB7  %s", t, CAT_NAMES[c]);
            }
        }
    LOGI("settings: \"%s\" matches %d", q, g_found.n);
    if (!g_found.n) { app_toast("No setting matches \"%s\"", q); return; }
    if (g_found.n == 1) { jump_to(st, g_found.cat[0], g_found.id[0]); return; }
    const char *items[FOUND_MAX];
    for (int i = 0; i < g_found.n; i++) items[i] = g_found.label[i];
    char sub[160];
    snprintf(sub, sizeof sub, "%s\"%.80s\"", g_found.n == FOUND_MAX ? "The first matches for " : "Settings that match ", q);   /* the sheet cuts a long one */
    app_push(screen_options_create("Find a setting", sub, items, g_found.n, on_found, NULL));
}

/* Trakt / SimKL: the app id first, then a code to type on the phone, or disconnect. */
static int g_track_svc;
static void on_track_sheet(int choice, void *user)
{
    (void)user;
    if (choice == 1) { tracking_disconnect(g_track_svc); app_toast("%s disconnected", TRACK_NAMES[g_track_svc]); }
    if (choice == 2) app_toast("Use Find a setting: %s app", TRACK_NAMES[g_track_svc]);
}

static void begin_edit(Settings *st, int which);

static void tracking_row_ok(Settings *st, int svc)
{
    g_track_svc = svc;
    if (!tracking_configured(svc)) { begin_edit(st, svc == TRACK_TRAKT ? EDIT_TRAKT_APP : EDIT_SIMKL_APP); return; }
    if (tracking_connected(svc)) {
        static const char *const items[] = { "Close", "Disconnect" };
        static const int icons[] = { ICON_CLOSE, ICON_CLOSE };
        app_push(screen_options_create_ex(TRACK_NAMES[svc], "Connected", NULL, items, icons, 2, on_track_sheet, NULL));
        return;
    }
    char code[32], url[160];
    if (!tracking_connect(svc, code, sizeof code, url, sizeof url)) {
        app_toast("%s refused the sign-in (%s). Check the app id", TRACK_NAMES[svc], tracking_last_error());
        begin_edit(st, svc == TRACK_TRAKT ? EDIT_TRAKT_APP : EDIT_SIMKL_APP);
        return;
    }
    char body[400];
    snprintf(body, sizeof body, "On your phone or computer open\n%s\nand enter\n\n%s\n\nThis connects by itself once the code is accepted.", url, code);
    static const char *const items[] = { "Close" };
    static const int icons[] = { ICON_CLOSE };
    app_push(screen_options_create_ex(TRACK_NAMES[svc], "Connect", body, items, icons, 1, NULL, NULL));
}

/* Stage 1 of breaks over other apps: arm, then leave Stremio; the sheet shows what webOS said. */
/* What this TV can play: each format the source ranking cares about, what the app
 * believes and why (seen playing, seen failing, or LG's tables for this webOS). */
static void on_tv_caps(int choice, void *user)
{
    (void)user;
    if (choice != 1) return;
    memset(g_app.tv.ok, 0, sizeof g_app.tv.ok);
    memset(g_app.tv.fail, 0, sizeof g_app.tv.fail);
    store_set_json("tvCaps", cJSON_CreateObject());
    store_save();
    app_toast("Forgotten: sources are ranked by LG's tables again");
}

static void show_tv_caps(void)
{
    static const char *const LABEL[TVCAP_COUNT] = { "DTS audio", "Dolby TrueHD audio", "AV1 video", "Dolby Vision", "HEVC (H.265) video", "4K resolution" };
    static const int ORDER[TVCAP_COUNT] = { TVCAP_4K, TVCAP_HEVC, TVCAP_AV1, TVCAP_DV, TVCAP_DTS, TVCAP_TRUEHD };
    char body[1400]; size_t o = 0;
    for (int k = 0; k < TVCAP_COUNT; k++) {
        int c = ORDER[k], ok = quality_cap_ok(&g_app.tv, c), seen_ok = g_app.tv.ok[c], seen_fail = g_app.tv.fail[c];
        char why[96];
        if (seen_ok || seen_fail) snprintf(why, sizeof why, "played %d time%s, failed %d", seen_ok, seen_ok == 1 ? "" : "s", seen_fail);
        else snprintf(why, sizeof why, g_app.tv.webos_major ? "not seen yet: LG's tables for webOS %d" : "not seen yet", g_app.tv.webos_major);
        o += (size_t)snprintf(body + o, sizeof body - o, "%s  \xC2\xB7  %s  (%s)\n", LABEL[c], ok ? "plays" : "avoided", why);
        if (o >= sizeof body) break;
    }
    if (o < sizeof body) snprintf(body + o, sizeof body - o, "\nSources that need something avoided rank lower but stay in the list. A format that plays fine later is trusted again.");
    static const char *const items[] = { "Close", "Forget what this TV learned" };
    static const int icons[] = { ICON_CLOSE, ICON_RELOAD };
    app_push(screen_options_create_ex("What this TV can play", NULL, body, items, icons, 2, on_tv_caps, NULL));
}

static void begin_edit(Settings *st, int which)
{
    st->edit = which;
    char loc[64] = "";
    if (breaks_location_known()) snprintf(loc, sizeof loc, "%.4f, %.4f", g_app.break_lat, g_app.break_lon);
    keyboard_init(&st->kb, 0, which == EDIT_SERVER ? g_app.streaming_server : which == EDIT_THUMBS ? (g_app.thumb_server ? g_app.thumb_server : "http://")
                  : which == EDIT_TMDB ? (g_app.tmdb_key ? g_app.tmdb_key : "")
                  : which == EDIT_AI_KEY ? (g_app.ai_key ? g_app.ai_key : "") : which == EDIT_AI_BASE ? (g_app.ai_base ? g_app.ai_base : "http://")
                  : which == EDIT_AI_MODEL ? (g_app.ai_model ? g_app.ai_model : "") : which == EDIT_LOCATION ? loc
                  : which == EDIT_SEARCH || which == EDIT_BACKUP || which == EDIT_TRAKT_APP || which == EDIT_SIMKL_APP ? ""
                  : which == EDIT_FIRE_VIDEO ? (torbox_is_link(store_get_str("fireplaceVideo", "")) ? "" : store_get_str("fireplaceVideo", "")) : which == EDIT_MDBLIST ? store_get_str("mdblistKey", "") : which == EDIT_TORBOX ? store_get_str("torboxKey", "") : which == EDIT_OS_KEY ? store_get_str("osApiKey", "") : which == EDIT_OS_LOGIN ? "" : which == EDIT_PLAY_LINK ? "https://" : which == EDIT_EXCLUDE ? g_app.src_exclude
                  : which == EDIT_BR_AUDIO ? (g_app.break_audio && strncmp(g_app.break_audio, "sntv-sound://", 13) ? g_app.break_audio : "https://") : which == EDIT_BR_END ? (g_app.break_end && strncmp(g_app.break_end, "sntv-sound://", 13) ? g_app.break_end : "https://") : "https://");
    input_text(1);
    phone_prompt(edit_title(which), edit_hint(which), st->kb.text);
    if (which == EDIT_BR_AUDIO || which == EDIT_BR_END) phone_accept_file(which == EDIT_BR_AUDIO ? "start" : "end");
    phone_qr_make(st);
    if (which == EDIT_BACKUP) { input_text(0); char *j = app_backup_settings(); phone_offer_backup(j ? j : "{}"); free(j); }
}

static void find_setting(Settings *st, const char *q);

static void finish_edit(Settings *st, int save)
{
    if (st->edit == EDIT_BACKUP) phone_offer_backup(NULL);
    if (st->edit == EDIT_SEARCH) {   /* the keyboard goes first: the matches open over the list */
        char q[sizeof st->kb.text];
        snprintf(q, sizeof q, "%s", st->kb.text);
        st->edit = EDIT_NONE;
        input_text(0);
        phone_prompt(NULL, NULL, NULL);
        if (save) find_setting(st, q);
        return;
    }
    if (save) {
        if (st->edit == EDIT_SERVER) {
            char *v = st->kb.text;
            size_t n = strlen(v);
            while (n && v[n - 1] == '/') v[--n] = 0;
            free(g_app.streaming_server);
            g_app.streaming_server = n ? strdup(v) : NULL;
            store_set_str("streamingServer", g_app.streaming_server);
            store_save();
            app_toast(n ? "Streaming server saved" : "Streaming server cleared");
        } else if (st->edit == EDIT_FIRE_VIDEO) {
            char *v = st->kb.text;
            while (*v == ' ') v++;
            size_t n = strlen(v); while (n && v[n - 1] == ' ') v[--n] = 0;
            store_set_str("fireplaceVideo", n ? v : NULL); store_save();
            app_toast(n ? "Fireplace video saved: it plays when the screensaver starts" : "Fireplace video removed");
        } else if (st->edit == EDIT_OS_KEY || st->edit == EDIT_OS_LOGIN) {
            char *v = st->kb.text;
            while (*v == ' ') v++;
            size_t n = strlen(v);
            while (n && v[n - 1] == ' ') v[--n] = 0;
            if (st->edit == EDIT_OS_KEY) { store_set_str("osApiKey", n ? v : NULL); store_save(); app_toast(n ? "OpenSubtitles.com on" : "OpenSubtitles.com key removed"); }
            else {
                char *sp = strchr(v, ' ');
                if (!sp) app_toast("Enter the username, a space, then the password");
                else {
                    *sp++ = 0;
                    char err[96];
                    if (opensubs_login(v, sp, err, sizeof err)) app_toast("Signed in to OpenSubtitles as %s", v);
                    else app_toast("OpenSubtitles sign-in failed: %s", err);
                }
                memset(st->kb.text, 0, sizeof st->kb.text);   /* the password goes no further */
            }
        } else if (st->edit == EDIT_TORBOX) {
            char *v = st->kb.text;
            while (*v == ' ') v++;
            size_t n = strlen(v);
            while (n && v[n - 1] == ' ') v[--n] = 0;
            store_set_str("torboxKey", n ? v : NULL); store_save();
            app_toast(n ? "TorBox key saved: Choose from my TorBox appears under the fireplace and break recording" : "TorBox key removed");
        } else if (st->edit == EDIT_TRAKT_APP || st->edit == EDIT_SIMKL_APP || st->edit == EDIT_MDBLIST) {
            char *v = st->kb.text;
            while (*v == ' ') v++;
            size_t n = strlen(v);
            while (n && v[n - 1] == ' ') v[--n] = 0;
            if (st->edit == EDIT_MDBLIST) {
                store_set_str("mdblistKey", n ? v : NULL); store_save();
                app_toast(n ? "MDBList key saved" : "MDBList key removed");
            } else if (st->edit == EDIT_SIMKL_APP) {
                tracking_set_app(TRACK_SIMKL, v, NULL);
                app_toast(n ? "SimKL app saved: OK on SimKL connects" : "SimKL app removed");
            } else {
                char *sp = strchr(v, ' ');
                {
                    if (sp) { *sp++ = 0; while (*sp == ' ') sp++; }
                    tracking_set_app(TRACK_TRAKT, v, sp);
                    app_toast(n ? "Trakt app saved: OK on Trakt connects" : "Trakt app removed");
                }
            }
        } else if (st->edit == EDIT_PLAY_LINK) {
            char *v = st->kb.text;
            while (*v == ' ') v++;
            if (strncmp(v, "http://", 7) == 0 || strncmp(v, "https://", 8) == 0) { if (strlen(v) > 9) app_play_link(v); }
            else app_toast("That is not a video link (it starts with http)");
        } else if (st->edit == EDIT_EXCLUDE) {
            char *v = st->kb.text;
            while (*v == ' ' || *v == ',') v++;
            snprintf(g_app.src_exclude, sizeof g_app.src_exclude, "%.127s", v);
            app_save_prefs();
            app_toast(g_app.src_exclude[0] ? "Sources named with these words are never picked for you" : "Word rule removed");
        } else if (st->edit == EDIT_TMDB) {
            char *v = st->kb.text;
            while (*v == ' ') v++;
            size_t n = strlen(v);
            while (n && v[n - 1] == ' ') v[--n] = 0;
            tmdb_key_set(n >= 20 ? v : NULL);
            app_save_prefs();
            app_toast(g_app.tmdb_key ? "TMDB key saved: cast photos and age ratings on" : n ? "That does not look like a TMDB key" : "TMDB key removed");
        } else if (st->edit == EDIT_AI_KEY || st->edit == EDIT_AI_BASE || st->edit == EDIT_AI_MODEL) {
            char *v = st->kb.text;
            while (*v == ' ') v++;
            size_t n = strlen(v);
            while (n && (v[n - 1] == ' ' || (st->edit == EDIT_AI_BASE && v[n - 1] == '/'))) v[--n] = 0;
            char **dst = st->edit == EDIT_AI_KEY ? &g_app.ai_key : st->edit == EDIT_AI_BASE ? &g_app.ai_base : &g_app.ai_model;
            free(*dst);
            *dst = n && !(st->edit == EDIT_AI_BASE && n < 8) ? strdup(v) : NULL;
            app_save_prefs();
            app_toast(ai_enabled() ? "AI assistant ready" : st->edit == EDIT_AI_KEY && !n ? "AI key removed" : "Saved  \xC2\xB7  the assistant still needs its key or endpoint");
        } else if (st->edit == EDIT_LOCATION) {
            double la, lo;
            if (sscanf(st->kb.text, " %lf , %lf", &la, &lo) == 2 && la >= -90 && la <= 90 && lo >= -180 && lo <= 180) { breaks_set_location(la, lo, ""); app_toast("Location saved"); }
            else if (!st->kb.text[0]) { breaks_set_location(0, 0, ""); app_toast("Location cleared"); }
            else app_toast("Please enter latitude, longitude");
        } else if (st->edit == EDIT_BR_AUDIO || st->edit == EDIT_BR_END) {
            char **slot = st->edit == EDIT_BR_AUDIO ? &g_app.break_audio : &g_app.break_end;
            char *v = st->kb.text; while (*v == ' ') v++;
            int is_file = strncmp(v, "sntv-sound://", 13) == 0;
            int is_page = breaks_is_page_link(v);
            free(*slot);
            *slot = strlen(v) > 10 && strcmp(v, "https://") ? strdup(v) : NULL;
            app_save_prefs();
            if (g_app.break_sound != 1 && *slot) g_app.break_sound = 1;
            app_tidy(0);   /* the recording it replaced goes */
            char yt[16];
            if (*slot && youtube_video_id(*slot, yt)) save_youtube(*slot);
            else if (*slot && is_page && !(g_app.thumb_server && *g_app.thumb_server))
                app_toast("Saved. A page link like YouTube needs the home helper: Settings > Playback > Home helper");
            else if (*slot && is_page) warm_media(*slot);
            else app_toast(is_file ? "Recording saved on the TV" : *slot ? "Link saved" : "No recording set");
        } else if (st->edit == EDIT_THUMBS) {
            char *v = st->kb.text;
            size_t n = strlen(v);
            while (n && v[n - 1] == '/') v[--n] = 0;
            free(g_app.thumb_server);
            g_app.thumb_server = n > 8 ? strdup(v) : NULL;
            app_save_prefs();
            app_toast(g_app.thumb_server ? "Home helper set: previews come from it" : "Home helper off: the TV makes previews itself");
        } else if (strlen(st->kb.text) > 10) {
            app_install_addon(st->kb.text);
        }
    }
    st->edit = EDIT_NONE;
    input_text(0);
    phone_prompt(NULL, NULL, NULL);
}

/* Left/Right on a row. Returns 1 when the row took it. */
static int adjust(Settings *st, int id, int dir)
{
    switch (id) {
    case IT_PREF_AUDIO: cycle_lang(g_app.pref_audio, sizeof g_app.pref_audio, dir); break;
    case IT_PREF_SUBS: cycle_lang(g_app.pref_subs, sizeof g_app.pref_subs, dir); break;
    case IT_SUBS_SIZE: g_app.subs_size = (g_app.subs_size + dir + 4) % 4; break;
    case IT_SUBS_STYLE: g_app.subs_style = (g_app.subs_style + dir + SUBS_STYLE_COUNT) % SUBS_STYLE_COUNT; break;
    case IT_SUBS_COLOUR: g_app.subs_colour = (g_app.subs_colour + dir + SUBS_COLOUR_COUNT) % SUBS_COLOUR_COUNT; break;
    case IT_FX: g_app.fx_setting = (FxLevel)(((int)g_app.fx_setting + dir + 3) % 3); break;
    case IT_AUTOPLAY: g_app.autoplay_next = !g_app.autoplay_next; break;
    case IT_SKIP: g_app.skip_mode = (g_app.skip_mode + dir + 3) % 3; break;
    case IT_NIGHT: g_app.night_mode = (g_app.night_mode + dir + 3) % 3; break;
    case IT_SUBS_WHEN: g_app.subs_always = !g_app.subs_always; break;
    case IT_CLOCK: g_app.clock_mode = (g_app.clock_mode + dir + 3) % 3; break;
    case IT_ISLAND_STYLE: g_app.island_style = (g_app.island_style + dir + ISLAND_STYLE_COUNT) % ISLAND_STYLE_COUNT; break;
    case IT_CLOCK_FACE: g_app.clock_face = (g_app.clock_face + dir + CLOCKFACE_COUNT) % CLOCKFACE_COUNT; break;
    case IT_QUALITY: g_app.quality_mode = (g_app.quality_mode + dir + QUAL_MODE_COUNT) % QUAL_MODE_COUNT; break;
    case IT_RES_CAP: {
        static const int CAPS[] = { 720, 1080, 2160 };   /* low to high, as the slider shows them */
        int k = g_app.res_cap <= 720 ? 0 : g_app.res_cap <= 1080 ? 1 : 2;
        g_app.res_cap = CAPS[(k + dir + 3) % 3];
        break;
    }
    case IT_AUTO_REC: g_app.auto_recommended = !g_app.auto_recommended; break;
    case IT_SRC_SIZE: g_app.src_max_gb = step_move(SRC_SIZES, N_OF(SRC_SIZES), g_app.src_max_gb, 0, dir); break;
    case IT_SRC_HDR: g_app.src_hdr = (g_app.src_hdr + dir + 3) % 3; break;
    case IT_SRC_CACHED: g_app.src_cached_only = !g_app.src_cached_only; break;
    case IT_PREFLIGHT: g_app.preflight = !g_app.preflight; break;
    case IT_SCRUB: g_app.live_scrub = !g_app.live_scrub; break;
    case IT_THEME:
        g_app.theme = (g_app.theme + dir + THEME_COUNT) % THEME_COUNT;
        g_app.theme_accent = 0;
        theme_apply(g_app.theme, g_app.theme_accent, g_app.theme_bg);
        theme_load_fonts(g_app.theme);
        break;
    case IT_CARD_DETAILS: g_app.card_details = !g_app.card_details; break;
    case IT_AUTO_TRAILER: g_app.auto_trailers = (g_app.auto_trailers + dir + 3) % 3; break;
    case IT_PALETTE: {   /* from a mix of one's own, Right starts at the first palette */
        int cur = theme_palette_index(g_app.theme_accent, g_app.theme_bg);
        int next = cur < 0 ? (dir > 0 ? 0 : THEME_PALETTES - 1) : (cur + dir + THEME_PALETTES) % THEME_PALETTES;
        g_app.theme_accent = THEME_PALETTE[next][0];
        g_app.theme_bg = THEME_PALETTE[next][1];
        theme_apply(g_app.theme, g_app.theme_accent, g_app.theme_bg);
        break;
    }
    case IT_ACCENT:
        g_app.theme_accent = (g_app.theme_accent + dir + THEME_ACCENTS + 1) % (THEME_ACCENTS + 1);   /* the last is the artwork's colour */
        theme_apply(g_app.theme, g_app.theme_accent, g_app.theme_bg);
        break;
    case IT_BG:
        g_app.theme_bg = (g_app.theme_bg + dir + THEME_BGS) % THEME_BGS;
        theme_apply(g_app.theme, g_app.theme_accent, g_app.theme_bg);
        break;
    case IT_HOME_STYLE: g_app.home_style = (g_app.home_style + dir + HOME_STYLE_COUNT) % HOME_STYLE_COUNT; break;
    case IT_HIDE_WATCHED: g_app.hide_watched = !g_app.hide_watched; break;
    case IT_TRANSITIONS: g_app.transitions = !g_app.transitions; break;
    case IT_DEPTH: g_app.depth_motion = !g_app.depth_motion; break;
    case IT_PIP: g_app.pip_enabled = !g_app.pip_enabled; break;
    case IT_SUBS_SYNC: g_app.subs_autosync = !g_app.subs_autosync; break;
    case IT_SUBS_FILES: g_app.subs_prefer_files = !g_app.subs_prefer_files; break;
    case IT_BUFFER: store_set_int("bufferAhead", !store_get_int("bufferAhead", 1)); store_save(); break;
    case IT_TV_KB: store_set_int("tvKeyboard", !store_get_int("tvKeyboard", TV_KB_DEFAULT)); store_save(); break;
    case IT_SUBS_XL: g_app.subs_autotranslate = !g_app.subs_autotranslate; break;
    case IT_BR_VIDEO:
        g_app.break_video = !g_app.break_video;
        for (int i = 0; i < 2; i++) {   /* a YouTube recording saved as sound only: fetch the video too */
            const char *u = i ? g_app.break_end : g_app.break_audio;
            char name[80]; int vid = 0;
            if (g_app.break_video && youtube_saved(u, 1, name, sizeof name, &vid) && !vid) youtube_save(u, 1, NULL, NULL);
        }
        break;
    case IT_SRC_AUDIO: g_app.src_audio_lang = !g_app.src_audio_lang; break;
    case IT_P2P: g_app.p2p_fallback = !g_app.p2p_fallback; if (!g_app.p2p_fallback) torrent_engine_stop(); break;
    case IT_AUTO_UPDATE: g_app.auto_update = !g_app.auto_update; break;
    case IT_ADDON_WAIT: g_app.addon_wait = step_move(ADDON_WAITS, N_OF(ADDON_WAITS), g_app.addon_wait, 2, dir); break;
    case IT_AI_PROVIDER: g_app.ai_provider = (g_app.ai_provider + dir + AI_PROVIDER_COUNT) % AI_PROVIDER_COUNT; break;
    case IT_MEMORY: {
        g_app.memory_mode = (g_app.memory_mode + dir + 3) % 3;
        mem_apply_mode();
        app_toast(g_app.low_memory ? "Memory saver on: fewer rows and posters kept" : g_app.memory_mode == 0 ? "Automatic: the saver turns on by itself when memory gets tight" : "Normal memory use");
        break;
    }
    case IT_TMDB: case IT_AI_KEY: case IT_AI_BASE: case IT_AI_MODEL: case IT_SRC_EXCLUDE: return 1;
    case IT_BR_ON: g_app.breaks_on = !g_app.breaks_on; break;
    case IT_BR_SCHED: g_app.break_sched = (g_app.break_sched + dir + BRK_SCHED_COUNT) % BRK_SCHED_COUNT; break;
    case IT_BR_METHOD: g_app.break_method = (g_app.break_method + 1 + dir + SUN_METHOD_COUNT + 1) % (SUN_METHOD_COUNT + 1) - 1; break;   /* Auto, then each method */
    case IT_BR_ASR: g_app.break_asr_hanafi = !g_app.break_asr_hanafi; break;
    case IT_BR_LEAD: g_app.break_lead_min = step_move(BR_LEADS, N_OF(BR_LEADS), g_app.break_lead_min, 3, dir); break;
    case IT_BR_LEN: g_app.break_len_min = step_move(BR_LENS, N_OF(BR_LENS), g_app.break_len_min, 4, dir); break;
    case IT_BR_SOUND: g_app.break_sound = (g_app.break_sound + dir + 3) % 3; break;
    case IT_BR_HEALTH: { static const int L[] = { 0, 30, 45, 60, 90, 120 }; int k = 0; for (int i = 0; i < 6; i++) if (L[i] == g_app.break_health_min) k = i; g_app.break_health_min = L[(k + dir + 6) % 6]; break; }
    case IT_BR_HEALTH_LEN: g_app.break_health_len = step_move(BR_HEALTH_LENS, N_OF(BR_HEALTH_LENS), g_app.break_health_len, 1, dir); break;
    case IT_BR_BED: {   /* Off, 20:00 ... 23:30, 0:00, 0:30, 1:00 */
        static const int L[] = { -1, 1200, 1230, 1260, 1290, 1320, 1350, 1380, 1410, 0, 30, 60 };
        int k = 0; for (int i = 0; i < 12; i++) if (L[i] == g_app.break_bed) k = i;
        g_app.break_bed = L[(k + dir + 12) % 12];
        break;
    }
    case IT_BR_CUSTOM1: case IT_BR_CUSTOM2: case IT_BR_CUSTOM3: {
        int *m = &g_app.break_custom[id - IT_BR_CUSTOM1];
        *m = *m < 0 ? (dir > 0 ? 0 : 23 * 60 + 45) : *m + dir * 15;
        if (*m >= 24 * 60 || *m < 0) *m = -1;
        break;
    }
    case IT_BR_WHERE: g_app.breaks_everywhere = !g_app.breaks_everywhere; break;
    case IT_BR_TEST: case IT_BR_LOCATION: case IT_BR_DETECT: case IT_BR_TODAY: case IT_BR_AUDIO: case IT_BR_END: return 1;
    case IT_PIN_SETTINGS: case IT_HIDE_ADULT: case IT_PIN: case IT_THUMBS: return 1;   /* OK (with the PIN) changes these */
    case IT_OLED: g_app.oled = !g_app.oled; break;
    case IT_ASK_PROFILE: g_app.ask_profile = !g_app.ask_profile; break;
    case IT_AMBIENT: g_app.ambient_minutes = step_move(AMBIENT_STEPS, N_OF(AMBIENT_STEPS), g_app.ambient_minutes, 0, dir); break;
    case IT_BLUR: g_app.spoiler_blur = !g_app.spoiler_blur; break;
    case IT_ADVISORY: g_app.advisory = !g_app.advisory; break;
    case IT_LETTERBOX: g_app.living_letterbox = (g_app.living_letterbox + dir + LB_STYLE_COUNT) % LB_STYLE_COUNT; break;
    case IT_AMBIENT_STYLE: g_app.ambient_style = (g_app.ambient_style + dir + AMBIENT_STYLE_COUNT) % AMBIENT_STYLE_COUNT; break;
    case IT_AMBIENT_DIM: g_app.ambient_dim_min = step_move(DIM_STEPS, N_OF(DIM_STEPS), g_app.ambient_dim_min, 0, dir); break;
    case IT_AMBIENT_OFF: g_app.ambient_off_min = step_move(OFF_STEPS, N_OF(OFF_STEPS), g_app.ambient_off_min, 0, dir); break;
    default:
        if (id >= IT_ACCT_BASE) return 0;   /* switching and forgetting are behind OK */
        if (id >= IT_ADDON_BASE) {
            int i = id - IT_ADDON_BASE;
            if (app_move_addon(i, dir)) st->focus += dir;
            st->confirm_remove = -1;
            return 1;
        }
        if (id >= IT_ROWS_BASE) {
            app_home_row_move(st->rows[id - IT_ROWS_BASE].key, dir);
            int nf = st->focus + dir;
            if (nf >= 0 && nf < st->n_rows) st->focus = nf;
            return 1;
        }
        return 0;
    }
    app_save_prefs();
    breaks_settings_changed();
    return 1;
}

static void on_account_choice(int c, void *user)
{
    int i = *(int *)user;
    if (c == 0) app_switch_account(i);
    else if (c == 1) { app_forget_account(i); app_toast("Saved sign-in forgotten"); }
}

static void after_pin_clear(int ok, void *user) { (void)user; if (ok) { app_set_parental_pin(NULL); app_toast("PIN removed"); } }
static void after_pin_toggle(int ok, void *user) { if (!ok) return; int *flag = user; *flag = !*flag; app_save_prefs(); }

/* Touch: a category opens its rows; a row takes the focus and OK follows. */
static int tap(Screen *s, int id)
{
    Settings *st = s->st;
    if (st->edit == EDIT_BACKUP) return 1;
    if (st->edit != EDIT_NONE && SNTV_PHONE) {   /* the phone's keyboard types; Done or Cancel */
        if (g_touch_aim) return 1;
        if (id == HIT_DONE) finish_edit(st, 1); else if (id == HIT_BACK) finish_edit(st, 0);
        return 1;
    }
    if (st->edit != EDIT_NONE) return keyboard_tap(&st->kb, id) ? 0 : 1;
    if (id == HIT_BACK) { if (!g_touch_aim) { st->cat_focus[st->cat] = st->focus; st->zone = 0; st->confirm_remove = -1; } return 1; }
    if (id == HIT_FIND) { st->zone = 0; st->search_foc = 1; return 0; }
    if (id >= HIT_CAT && id < HIT_CAT + CAT_COUNT) {
        if (st->zone == 1) st->cat_focus[st->cat] = st->focus;
        st->zone = 0; st->search_foc = 0;
        if (st->cat != id - HIT_CAT) { st->cat = id - HIT_CAT; st->focus = 0; st->scroll = 0; }
        return 0;   /* OK: into its rows */
    }
    int ids[MAX_ROWS + 8];
    int n = rows_for(st, st->cat, ids, MAX_ROWS + 8);
    int back = id >= HIT_LEFT && id < HIT_LEFT + n;   /* a choice's < */
    if (back) id -= HIT_LEFT;
    if (id < 0 || id >= n || IS_SEC(ids[id])) return 1;
    if (st->zone != 1 || st->focus != id) st->confirm_remove = -1;
    st->zone = 1; st->search_foc = 0; st->focus = id;
    if (back) { if (!g_touch_aim) adjust(st, ids[id], -1); return 1; }
    return 0;
}

static int key(Screen *s, KeyAction a)
{
    Settings *st = s->st;
    app_redraw();
    if (st->edit == EDIT_BACKUP) { if (a == KEY_BACK || a == KEY_OK) finish_edit(st, 0); return 1; }
    if (st->edit != EDIT_NONE) {
        if (a == KEY_BACK) { finish_edit(st, 0); return 1; }
        if (a == KEY_BACKSPACE) { keyboard_key(&st->kb, KEY_BACKSPACE); return 1; }
        if (SNTV_PHONE) { if (a == KEY_OK) finish_edit(st, 1); return 1; }   /* the phone's keyboard: Enter is Done */
        if (keyboard_key(&st->kb, a) == KB_DONE) finish_edit(st, 1);
        return 1;
    }
    int ids[MAX_ROWS + 8];
    int n = rows_for(st, st->cat, ids, MAX_ROWS + 8);
    if (st->zone == 0 && st->search_foc) {
        switch (a) {
        case KEY_DOWN: st->search_foc = 0; return 1;
        case KEY_OK: begin_edit(st, EDIT_SEARCH); return 1;
        case KEY_BACK: app_pop(); return 1;
        default: return 1;
        }
    }
    if (st->zone == 0) {
        switch (a) {
        case KEY_UP: if (st->cat > 0) { st->cat--; st->focus = 0; st->scroll = 0; } else st->search_foc = 1; return 1;
        case KEY_DOWN: if (st->cat < CAT_COUNT - 1) { st->cat++; st->focus = 0; st->scroll = 0; } return 1;
        case KEY_RIGHT: case KEY_OK: {   /* back to the row this category had */
            int f = focusable(ids, n, st->cat_focus[st->cat] < n ? st->cat_focus[st->cat] : 0, 1);
            if (f < 0) f = focusable(ids, n, 0, 1);
            if (f >= 0) { st->zone = 1; st->focus = f; }
            return 1;
        }
        case KEY_BACK: app_pop(); return 1;
        default: return 0;
        }
    }
    fix_focus(st, ids, n);   /* the rows may have changed under the focus */
    if (!n || st->zone == 0) { st->zone = 0; st->focus = 0; return 1; }
    int id = ids[st->focus];
    switch (a) {
    case KEY_UP: { int f = focusable(ids, n, st->focus - 1, -1); if (f >= 0) st->focus = f; st->confirm_remove = -1; return 1; }
    case KEY_DOWN: { int f = focusable(ids, n, st->focus + 1, 1); if (f >= 0) { st->focus = f; st->confirm_remove = -1; return 1; } return 0; }
    case KEY_LEFT:   /* choices and reorderable rows take Left; elsewhere it goes back to the categories */
        if (row_takes_left(id) && adjust(st, id, -1)) return 1;
        st->cat_focus[st->cat] = st->focus; st->zone = 0;
        return 1;
    case KEY_RIGHT: adjust(st, id, 1); return 1;
    case KEY_BACK: st->cat_focus[st->cat] = st->focus; st->zone = 0; st->confirm_remove = -1; return 1;
    case KEY_OK:
        if (id == IT_SYNC_DEVICES) app_push(screen_sync_create(NULL));
        else if (id == IT_ACCOUNT) { if (g_app.auth_key) { app_logout(); app_toast("Stopped syncing. Your addons and Library stay on this TV."); } else app_push(screen_login_create()); }
        else if (id == IT_SERVER) begin_edit(st, EDIT_SERVER);
        else if (id == IT_INSTALL) begin_edit(st, EDIT_INSTALL);
        else if (id == IT_DIAG) app_push(screen_diagnostics_create(0));
        else if (id == IT_STORAGE) app_clear_cache();
        else if (id == IT_WHATSNEW || id == IT_ABOUT_APP) app_push(screen_whatsnew_create(id == IT_ABOUT_APP));
        else if (id == IT_UPDATE) {
            if (g_update_version[0] && g_update_url[0] && g_update_sha256[0]) app_push(screen_update_create(0));
            else { app_toast("Checking for updates\xE2\x80\xA6"); app_update_check_now(); }
        }
        else if (id == IT_THUMBS) begin_edit(st, EDIT_THUMBS);
        else if (id == IT_TMDB) begin_edit(st, EDIT_TMDB);
        else if (id == IT_SRC_EXCLUDE) begin_edit(st, EDIT_EXCLUDE);
        else if (id == IT_TVCAP) show_tv_caps();
        else if (id == IT_PLAY_LINK) begin_edit(st, EDIT_PLAY_LINK);
        else if (id == IT_FIRE_VIDEO) begin_edit(st, EDIT_FIRE_VIDEO);
        else if (id == IT_MDBLIST) begin_edit(st, EDIT_MDBLIST);
        else if (id == IT_TORBOX) begin_edit(st, EDIT_TORBOX);
        else if (id == IT_TORBOX_FILES) app_push(screen_torbox_create(TORBOX_FOR_PLAY));
        else if (id == IT_FIRE_TORBOX) app_push(screen_torbox_create(TORBOX_FOR_FIREPLACE));
        else if (id == IT_BR_TORBOX) app_push(screen_torbox_create(TORBOX_FOR_BREAK));
        else if (id == IT_OS_KEY) begin_edit(st, EDIT_OS_KEY);
        else if (id == IT_OS_LOGIN) { if (opensubs_signed_in()) { char e[8]; opensubs_login(NULL, NULL, e, sizeof e); app_toast("Signed out of OpenSubtitles"); } else begin_edit(st, EDIT_OS_LOGIN); }
        else if (id == IT_TRAKT || id == IT_SIMKL) tracking_row_ok(st, id == IT_TRAKT ? TRACK_TRAKT : TRACK_SIMKL);
        else if (id == IT_BACKUP) begin_edit(st, EDIT_BACKUP);
        else if (id == IT_AI_KEY) begin_edit(st, EDIT_AI_KEY);
        else if (id == IT_AI_BASE) begin_edit(st, EDIT_AI_BASE);
        else if (id == IT_AI_MODEL) { app_toast("Asking %s for its models\xE2\x80\xA6", AI_PROVIDER_NAMES[g_app.ai_provider]); ai_list_models(on_models, NULL); }
        else if (id == IT_BR_TEST) breaks_fire_now(g_app.break_sched == BRK_ISLAM ? "Maghrib" : "Break", "This is how a break looks; OK continues", g_app.break_sched == BRK_ISLAM ? BRK_KIND_PRAYER : BRK_KIND_TEST);
        else if (id == IT_BR_LOCATION) begin_edit(st, EDIT_LOCATION);
        else if (id == IT_BR_DETECT) breaks_detect_location();
        else if (id == IT_BR_AUDIO) begin_edit(st, EDIT_BR_AUDIO);
        else if (id == IT_BR_END) begin_edit(st, EDIT_BR_END);
        else if (id == IT_BR_TODAY) { char t[300]; breaks_describe_today(t, sizeof t); app_toast("%s", t); }
        else if (id == IT_PIN) { if (g_app.parental_pin[0]) app_push(screen_pin_create("Remove the parental PIN", 0, after_pin_clear, NULL)); else app_push(screen_pin_create("Parental PIN", 1, NULL, NULL)); }
        else if (id == IT_PIN_SETTINGS) app_push(screen_pin_create("Parental PIN", 0, after_pin_toggle, &g_app.pin_settings));
        else if (id == IT_HIDE_ADULT) app_push(screen_pin_create("Parental PIN", 0, after_pin_toggle, &g_app.hide_adult));
        else if (id >= IT_ACCT_BASE) {
            static const char *const ITEMS[] = { "Switch to this account", "Forget this sign-in" };
            static const int ICONS[] = { ICON_USER, ICON_CLOSE };
            static int which;
            which = id - IT_ACCT_BASE;
            app_push(screen_options_create_ex(st->accounts[which], "Saved sign-in", NULL, ITEMS, ICONS, 2, on_account_choice, &which));
        }
        else if (id == IT_SETUP) app_push(screen_welcome_create());
        else if (id >= IT_ADDON_BASE) {
            int idx = id - IT_ADDON_BASE;
            if (st->confirm_remove == idx) { app_remove_addon(idx); st->confirm_remove = -1; }
            else if (g_app.addons[idx].flag_protected) app_toast("Built-in addons cannot be removed");
            else st->confirm_remove = idx;
        }
        else if (id >= IT_ROWS_BASE) app_home_row_toggle(st->rows[id - IT_ROWS_BASE].key);
        else adjust(st, id, 1);
        return 1;
    default: return 0;
    }
}

static void text(Screen *s, const char *utf8)
{
    Settings *st = s->st;
    if (st->edit != EDIT_NONE) { keyboard_text_input(&st->kb, utf8); app_redraw(); }
}

static void destroy(Screen *s)
{
    Settings *st = s->st;
    if (st->edit != EDIT_NONE) input_text(0);
    free(st);
    free(s);
}

Screen *screen_settings_create(void)
{
    Screen *s = calloc(1, sizeof *s);
    Settings *st = calloc(1, sizeof *st);
    st->confirm_remove = -1;
    st->rows_seen = -1;
    s->name = "settings"; s->st = st;
    s->update = update; s->draw = draw; s->key = key; s->tap = tap; s->text = text; s->destroy = destroy; s->phone = 1;
    return s;
}
