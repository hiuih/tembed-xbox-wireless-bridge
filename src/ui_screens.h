#pragma once
#include "ui_gfx.h"
#include "ui_state.h"

// Draws the whole 320x170 frame into the band that G covers.
void ui_render(G &g, const Snap &s, const Nav &n);

// shared by both screen files
void ui_chrome(G &g, const Snap &s, const Nav &n, const char *title, const char *hint, bool overlay = false);
void ui_footer(G &g, const char *hint, bool clear);
const char *ui_screen_title(Screen sc);
void ui_draw_toast(G &g, const Nav &n, uint32_t now);

// screens (ui_screens.cpp)
void scr_splash(G &g, const Snap &s, const Nav &n);
void scr_home(G &g, const Snap &s, const Nav &n);
void scr_pads(G &g, const Snap &s, const Nav &n);
void scr_pad(G &g, const Snap &s, const Nav &n);
void scr_pair(G &g, const Snap &s, const Nav &n);
// screens (ui_screens2.cpp)
void scr_radio(G &g, const Snap &s, const Nav &n);
void scr_spectrum(G &g, const Snap &s, const Nav &n);
void scr_power(G &g, const Snap &s, const Nav &n);
void scr_settings(G &g, const Snap &s, const Nav &n);
void scr_about(G &g, const Snap &s, const Nav &n);
// screens (ui_screens3.cpp)
void scr_lag(G &g, const Snap &s, const Nav &n);
void scr_log(G &g, const Snap &s, const Nav &n);

#define HOME_TILES 9
Screen ui_home_screen(int tile);      // screen opened by a home tile
int ui_home_index(Screen sc);         // tile that opens this screen
void settingsValue(const Snap &s, int row, char *out, size_t n);
const char *settingsHint(int row);
#define SETTINGS_ROWS 17
int settingsDanger(int row);          // 1 if the row needs a second click

#define SB_H 22
#define FOOT_Y 156
