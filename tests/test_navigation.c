#define SDL_MAIN_HANDLED
#include "app.h"
#include "ui/theme.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Theme g_theme;
static int suspended, resumed, destroyed;
void island_drop_focus(void) {}

static void suspend_screen(Screen *s) { (void)s; suspended++; }
static void resume_screen(Screen *s) { (void)s; resumed++; }
static void destroy_screen(Screen *s) { free(s->st); free(s); destroyed++; }

static Screen *screen(const char *name, int value)
{
    Screen *s = calloc(1, sizeof *s);
    s->name = name; s->st = malloc(sizeof value); *(int *)s->st = value;
    s->suspend = suspend_screen; s->resume = resume_screen; s->destroy = destroy_screen;
    return s;
}

int main(void)
{
    Screen *home = screen("home", 1), *search = screen("search", 42);
    app_push(home);
    app_switch_tab(search);
    app_switch_tab(screen("settings", 9));
    app_switch_tab(NULL);
    assert(app_top() == home && g_app.depth == 1 && !g_app.quit);
    app_switch_tab(screen("search", 0));
    assert(app_top() == search && *(int *)app_top()->st == 42);
    app_push(screen("search", 99));
    app_switch_tab(screen("search", 0));
    assert(app_top() == search && *(int *)app_top()->st == 42 && g_app.depth == 2);
    for (int i = 0; i < 100; i++) {
        app_switch_tab(screen("settings", 0));
        assert(g_app.depth == 2 && *(int *)app_top()->st == 9);
        app_switch_tab(screen("search", 0));
        assert(app_top() == search && g_app.depth == 2);
    }
    int before = destroyed;
    app_push(screen("detail", 0));
    app_switch_tab(screen("library", 3));
    assert(destroyed == before + 1 && g_app.depth == 2);
    app_switch_tab(NULL);
    assert(app_top() == home && !g_app.quit && suspended > 200 && resumed > 200);
    puts("mobile navigation: passed");
    return 0;
}
