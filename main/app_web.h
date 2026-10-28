// app_web.h - OnTheRoad web UI on top of my_web: static pages + JSON API.
//
// Pages: /, /settings, /diag, /update, /players (+ .html variants), /style.css
// API:   GET  /api/info              system (my_diag) + wifi, display, touch, clock, ota, storage, battery, i2c
//        GET  /api/config            settings (never passwords / PIN)
//        POST /api/config            brightness, led_*, default_variant, roll_anim_ms, maxi_saved_rolls, throw_mode,
//                                    tz, hostname, battery_scale_permille, battery_reference_mv, battery_full_mv,
//                                    admin_pin_new (needs pin)
//        POST /api/wifi              ssid, pass[, keep_pass] -> connection attempt
//        GET  /api/scan[?start]      Wi-Fi scan (202 while running)
//        GET/POST /api/nets          saved networks: add | remove | move | forget_all
//        POST /api/time              epoch=<UTC s>, manual clock
//        GET/POST /api/players       registry: add | rename | remove (needs pin)
//        POST /api/reset             factory reset (needs pin)
//        (my_web) /api/log, /api/events, /api/ota, /api/reboot
#pragma once

namespace AppWeb {
using ConfigChangedCallback = void (*)();
void onConfigChanged(ConfigChangedCallback cb);   // After POST /api/config saved
void registerDiag();                              // MyDiag providers (call once at boot)
bool begin();
}  // namespace AppWeb
