#ifdef _WIN32
#include "wintask.h"
#include "log.h"
#include <SDL_syswm.h>
#include <windows.h>
#include <shobjidl.h>
#include <string.h>

#define BTN_BACK 100
#define BTN_PLAY 101
#define BTN_FWD 102
#define ICON_PX 32

static ITaskbarList3 *g_tb;
static HWND g_hwnd;
static HICON g_icon_play, g_icon_pause, g_icon_back, g_icon_fwd;
static int g_shown_state = -1, g_buttons_added, g_was_paused = -1;

/* a 32 x 32 icon painted from a mask: white shapes on transparent */
static HICON make_icon(const char *rows[ICON_PX])
{
    BITMAPV5HEADER bi;
    memset(&bi, 0, sizeof bi);
    bi.bV5Size = sizeof bi; bi.bV5Width = ICON_PX; bi.bV5Height = -ICON_PX; bi.bV5Planes = 1; bi.bV5BitCount = 32; bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask = 0x00FF0000; bi.bV5GreenMask = 0x0000FF00; bi.bV5BlueMask = 0x000000FF; bi.bV5AlphaMask = 0xFF000000;
    void *bits = NULL;
    HDC dc = GetDC(NULL);
    HBITMAP color = CreateDIBSection(dc, (BITMAPINFO *)&bi, DIB_RGB_COLORS, &bits, NULL, 0);
    ReleaseDC(NULL, dc);
    HBITMAP mask = CreateBitmap(ICON_PX, ICON_PX, 1, 1, NULL);
    if (!color || !mask || !bits) { if (color) DeleteObject(color); if (mask) DeleteObject(mask); return NULL; }
    DWORD *px = (DWORD *)bits;
    for (int y = 0; y < ICON_PX; y++) for (int x = 0; x < ICON_PX; x++) px[y * ICON_PX + x] = rows[y][x] == '#' ? 0xFFFFFFFFu : 0x00000000u;
    ICONINFO ii = { TRUE, 0, 0, mask, color };
    HICON icon = CreateIconIndirect(&ii);
    DeleteObject(color); DeleteObject(mask);
    return icon;
}

/* the shapes, from simple tests per pixel */
static HICON shape_icon(int kind)
{
    static char rows[ICON_PX][ICON_PX + 1];
    const char *ptr[ICON_PX];
    for (int y = 0; y < ICON_PX; y++) {
        for (int x = 0; x < ICON_PX; x++) {
            int on = 0, cy = y - 16, ax = cy < 0 ? -cy : cy;
            switch (kind) {
            case 0: on = x >= 9 && x <= 25 && ax <= (25 - x) * 12 / 16 + 0 && ax <= 11; on = x >= 9 && ax * 16 <= (25 - x) * 11 && x <= 25; break;   /* play: a triangle pointing right */
            case 1: on = ((x >= 8 && x <= 13) || (x >= 19 && x <= 24)) && ax <= 10; break;                                                            /* pause: two bars */
            case 2: on = (x >= 5 && x <= 8 && ax <= 9) || (x >= 10 && x <= 26 && ax * 16 <= (x - 10) * 9) ; break;                                   /* back: a bar and a triangle pointing left */
            case 3: on = (x >= 23 && x <= 26 && ax <= 9) || (x >= 6 && x <= 22 && ax * 16 <= (22 - x) * 9); break;                                    /* forward */
            }
            rows[y][x] = on ? '#' : '.';
        }
        rows[y][ICON_PX] = 0;
        ptr[y] = rows[y];
    }
    return make_icon(ptr);
}

static void set_play_button(int paused)
{
    if (!g_tb || !g_buttons_added) return;
    THUMBBUTTON b;
    memset(&b, 0, sizeof b);
    b.dwMask = THB_ICON | THB_TOOLTIP | THB_FLAGS;
    b.iId = BTN_PLAY;
    b.hIcon = paused ? g_icon_play : g_icon_pause;
    wcscpy(b.szTip, paused ? L"Play" : L"Pause");
    b.dwFlags = THBF_ENABLED;
    g_tb->lpVtbl->ThumbBarUpdateButtons(g_tb, g_hwnd, 1, &b);
}

void wintask_init(SDL_Window *win)
{
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (!win || !SDL_GetWindowWMInfo(win, &info) || info.subsystem != SDL_SYSWM_WINDOWS) return;
    g_hwnd = info.info.win.window;
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(CoCreateInstance(&CLSID_TaskbarList, NULL, CLSCTX_INPROC_SERVER, &IID_ITaskbarList3, (void **)&g_tb)) || !g_tb) { g_tb = NULL; LOGW("taskbar: no taskbar list"); return; }
    if (FAILED(g_tb->lpVtbl->HrInit(g_tb))) { g_tb->lpVtbl->Release(g_tb); g_tb = NULL; return; }
    g_icon_play = shape_icon(0); g_icon_pause = shape_icon(1); g_icon_back = shape_icon(2); g_icon_fwd = shape_icon(3);
    SDL_EventState(SDL_SYSWMEVENT, SDL_ENABLE);   /* the buttons' presses arrive as window messages */
    LOGI("taskbar: progress and thumbnail buttons ready");
}

static void add_buttons(void)
{
    if (!g_tb || g_buttons_added || !g_icon_play) return;
    THUMBBUTTON b[3];
    memset(b, 0, sizeof b);
    for (int i = 0; i < 3; i++) { b[i].dwMask = THB_ICON | THB_TOOLTIP | THB_FLAGS; b[i].dwFlags = THBF_ENABLED; }
    b[0].iId = BTN_BACK; b[0].hIcon = g_icon_back; wcscpy(b[0].szTip, L"Back 10 s");
    b[1].iId = BTN_PLAY; b[1].hIcon = g_icon_pause; wcscpy(b[1].szTip, L"Pause");
    b[2].iId = BTN_FWD; b[2].hIcon = g_icon_fwd; wcscpy(b[2].szTip, L"On 10 s");
    if (SUCCEEDED(g_tb->lpVtbl->ThumbBarAddButtons(g_tb, g_hwnd, 3, b))) g_buttons_added = 1;
}

void wintask_update(int playing, int paused, double position, double duration)
{
    if (!g_tb) return;
    int state = !playing ? 0 : paused ? 1 : 2;
    if (playing) add_buttons();
    if (state != g_shown_state) {
        g_tb->lpVtbl->SetProgressState(g_tb, g_hwnd, state == 0 ? TBPF_NOPROGRESS : state == 1 ? TBPF_PAUSED : TBPF_NORMAL);
        g_shown_state = state;
    }
    if (playing && duration > 0) g_tb->lpVtbl->SetProgressValue(g_tb, g_hwnd, (ULONGLONG)(position * 10), (ULONGLONG)(duration * 10));
    if (playing && paused != g_was_paused) { set_play_button(paused); g_was_paused = paused; }
}

int wintask_message(const SDL_SysWMmsg *msg)
{
    if (!msg || msg->subsystem != SDL_SYSWM_WINDOWS) return WINTASK_NONE;
    if (msg->msg.win.msg != WM_COMMAND || HIWORD(msg->msg.win.wParam) != THBN_CLICKED) return WINTASK_NONE;
    switch (LOWORD(msg->msg.win.wParam)) {
    case BTN_PLAY: return WINTASK_PLAYPAUSE;
    case BTN_BACK: return WINTASK_BACK;
    case BTN_FWD: return WINTASK_FORWARD;
    }
    return WINTASK_NONE;
}

void wintask_shutdown(void)
{
    if (g_tb) { g_tb->lpVtbl->SetProgressState(g_tb, g_hwnd, TBPF_NOPROGRESS); g_tb->lpVtbl->Release(g_tb); g_tb = NULL; }
}
#endif
