// board_webserver.h - ESPAsyncWebServer on port 80 with the dashboard, settings and
// diagnostics pages plus a small JSON API.
//
// Pages are compiled into flash (gzip) from data/ by tools/embed_web.py -> web_assets.h,
// so no filesystem upload is needed.
//
//   GET  /                  index.html (dashboard)
//   GET  /settings          settings.html
//   GET  /diag              diagnostics.html
//   GET  /style.css
//   GET  /api/info          heap, PSRAM, uptime, Wi-Fi (status, RSSI, IPs), display, touch, I2C
//   GET  /api/config        current settings (passwords are never returned)
//   POST /api/config        update settings (form fields, see board_webserver.cpp)
//   POST /api/wifi          ssid, pass -> connect (saved to NVS on success); empty ssid = forget
//   GET  /api/scan          start/poll an async Wi-Fi scan
//   POST /api/reboot        restart the board
//   POST /api/reset         factory-reset the settings, then restart
#pragma once

namespace Webserver {
bool begin();
// Called by the web server after /api/config changed settings that the UI should apply
// (brightness, LED). Runs in the async_tcp task.
typedef void (*ConfigChangedCallback)();
void onConfigChanged(ConfigChangedCallback cb);
}
