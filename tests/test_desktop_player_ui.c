#define SDL_MAIN_HANDLED
#include "../src/ui/screen_player.c"

App g_app;
Theme g_theme;
int g_ui_w = 1920, g_ui_h = 1080, g_ui_portrait, g_ui_mobile;
static VideoInfo test_video;
static int rate_calls, failures;
static double last_rate;
#define CHECK(c) do { if (!(c)) { failures++; fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); } } while (0)

Screen *app_top(void) { return g_app.depth ? g_app.stack[g_app.depth - 1] : NULL; }
void app_redraw(void) {}
void app_toast(const char *fmt, ...) { (void)fmt; }
void video_info(VideoInfo *v) { *v = test_video; }
void video_play(void) { test_video.state = VIDEO_PLAYING; }
void video_pause(void) { test_video.state = VIDEO_PAUSED; }
void video_set_rate(double r) { rate_calls++; last_rate = r; }
int gfx_visible_rect(float *x, float *y, float *w, float *h) { return *w > 0 && *h > 0; }
void gfx_clip_push(float x, float y, float w, float h) {}
void gfx_clip_pop(void) {}
void gfx_rrect(float x, float y, float w, float h, float r, Color c) {}
void gfx_rect(float x, float y, float w, float h, Color c) {}
void gfx_circle(float x, float y, float r, Color c) {}
void gfx_line(float x0, float y0, float x1, float y1, float t, Color c) {}
void gfx_play_triangle(float x, float y, float w, float h, Color c) {}
void gfx_rrect_ring(float x, float y, float w, float h, float r, float t, Color c) {}
float font_height(int size) { return 32; }
void font_draw_fit(const char *s, float x, float y, int size, Color c, float max_w) {}
float ui_ease(float from, float to, float *vel, float dt, float speed) { return to; }

int main(void)
{
    Player p = { 0 };
    Screen s = { .name = "player", .st = &p };
    g_live = &p; g_app.stack[0] = &s; g_app.depth = 1; g_app.time = 10;
    p.ph_pic[2] = 1920; p.ph_pic[3] = 1080;
    p.dk_click_at = 9.8f; p.dk_click_state = VIDEO_PLAYING;
    test_video.state = VIDEO_PAUSED;
    CHECK(player_pointer_double_click(&s, 600, 400));
    CHECK(test_video.state == VIDEO_PLAYING);
    p.dk_click_state = VIDEO_PAUSED;
    CHECK(player_pointer_double_click(&s, 600, 400));
    CHECK(test_video.state == VIDEO_PAUSED && p.viewer_paused);
    p.ph_sheet = PH_TRACKS;
    CHECK(!player_pointer_double_click(&s, 600, 400));
    p.ph_sheet = 0;
    p.fs_barr[0] = 100; p.fs_barr[1] = 900; p.fs_barr[2] = 1600; p.fs_barr[3] = 100;
    CHECK(!player_pointer_double_click(&s, 600, 950));
    CHECK(!player_pointer_double_click(&s, -1, 400));
    ui_hits_begin(&s);
    ui_hit(500, 300, 200, 200, PHH_PLAY);
    CHECK(!player_pointer_double_click(&s, 600, 400));
    CHECK(test_video.state == VIDEO_PAUSED);

    p.rate_idx = RATE_NORMAL;
    set_rate(&p, RATE_NORMAL + 1);
    CHECK(p.rate_idx == RATE_NORMAL && rate_calls == 0);
    p.tch = 2; p.tch_at = 1; test_video.state = VIDEO_PLAYING;
    ph_hold_fast(&p, &test_video);
    CHECK(!p.tch_fast && rate_calls == 0);
    set_rate(&p, RATE_NORMAL);
    CHECK(rate_calls == 1 && last_rate == 1);

    PhRow rows[32] = { 0 };
    ui_hits_begin(&s);
    p.ph_focus = 15;
    desk_rows(&p, rows, 0, 16, 100, 100, 500, 500, 1);
    desk_rows(&p, rows, 16, 32, 900, 100, 500, 500, 0);
    CHECK(p.dk_audio_scroll == 880 && p.ph_scroll == 0);
    CHECK(p.dk_audio_pan.max == 880 && p.ph_pan.max == 880);
    CHECK(ui_pan_wheel(950, 200, -0.5f, 0));
    CHECK(p.ph_scroll == 70 && p.dk_audio_scroll == 880);
    CHECK(ui_pan_wheel(150, 200, 0.5f, 0));
    CHECK(p.dk_audio_scroll == 810 && p.ph_scroll == 70);
    ui_pan_keys(); p.ph_focus = 31;
    desk_rows(&p, rows, 0, 16, 100, 100, 500, 500, 1);
    desk_rows(&p, rows, 16, 32, 900, 100, 500, 500, 0);
    CHECK(p.ph_scroll == 880);
    if (failures) return 1;
    puts("desktop player UI checks passed");
    return 0;
}
