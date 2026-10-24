/* The side navigation: a slim strip of icons on the left of the top-level
 * screens (Home, Explore, Library, Calendar) that opens into labels when it has
 * the focus. Search, Settings and the account live there too. */
#ifndef SNTV_RAIL_H
#define SNTV_RAIL_H
#include "input.h"

enum { RAIL_HOME, RAIL_EXPLORE, RAIL_SEARCH, RAIL_LIBRARY, RAIL_CALENDAR, RAIL_SETTINGS, RAIL_ACCOUNT, RAIL_COUNT };
#define RAIL_W   104.0f   /* the strip */
#define RAIL_LX  176.0f   /* where content starts on screens that show the rail */
#define PHONE_TABS_H 144.0f   /* upright on a phone the rail is a tab bar this tall along the bottom */
#define HIT_TOP 0x7EFFFFFF    /* Screen.tap: the open tab tapped again; 1 = the screen went back to its top */

/* focus: the highlighted item while the rail has the focus, else -1 */
void rail_draw(int active, int focus);
/* Keys while the rail has the focus. 1 = handled, 2 = the screen should take the focus back. */
int  rail_key(KeyAction a, int *focus, int active);
/* Switch to a section (pushes or replaces the top-level screen as needed). */
void rail_go(int active, int target);
/* Touch: an item of the rail drawn last was tapped. */
void rail_tap(int target);
#endif
