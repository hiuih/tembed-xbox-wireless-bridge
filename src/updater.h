#pragma once
#include "config.h"
#if HAS_SCREEN
#include "ui_state.h"

// Software update from GitHub releases, in a special boot mode ("update mode"): the Xbox radio and USB are off, the
// board joins your Wi-Fi, downloads the new firmware to the SD card (downloads/), checks it, and restarts into Launcher,
// which installs it from there. (The running app cannot overwrite its own slot, and the other slot may hold Bruce.)
bool updater_boot_flag();            // true if the last session asked for update mode (the flag is cleared when read)
void updater_request_and_restart();  // ask for update mode, then restart
bool updater_active();
void updater_begin();                // update mode: start
void updater_tick();                 // update mode: call from loop()
void updater_view(UpdView &out);     // copy of the state for the screen

// commands from the screen
void updater_cmd_menu();
void updater_cmd_scan();
void updater_cmd_connect(const char *ssid, const char *pass);
void updater_cmd_pass(const char *ssid);   // show the password screen for this network
void updater_cmd_wps();
void updater_cmd_check();
void updater_cmd_install();
void updater_cmd_cancel();           // stop what is running / go back to the menu
void updater_exit();                 // leave update mode (restart normally)
bool updater_is_open(const char *ssid);   // network without a password?
#endif
