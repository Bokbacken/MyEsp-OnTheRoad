#include "board_config.h"

#include <ctype.h>

#include <Arduino.h>
#include <Preferences.h>
#include <string.h>

namespace {
constexpr const char *NVS_NS = "otr";
constexpr uint8_t CONFIG_SCHEMA = 1;
AppConfig g_cfg;

void copyStr(char *dst, size_t cap, const String &src)
{
    strncpy(dst, src.c_str(), cap - 1);
    dst[cap - 1] = '\0';
}
}  // namespace

namespace Config {

void defaults(AppConfig &c)
{
    memset(&c, 0, sizeof(c));
    strcpy(c.hostname, "ontheroad");
    c.brightness = 80;
    c.ledBrightness = 40;
    c.ledEnabled = true;
    c.defaultVariant = 0;
    c.rollAnimMs = 800;
    c.maxiSavedRolls = true;
}

void begin()
{
    defaults(g_cfg);
    Preferences p;
    if (!p.begin(NVS_NS, true)) {   // Namespace does not exist yet: first boot
        Serial.println("[cfg] no stored settings, using defaults");
        return;
    }
    if (p.getUChar("schema", 0) == CONFIG_SCHEMA) {
        copyStr(g_cfg.staSsid, sizeof(g_cfg.staSsid), p.getString("sta_ssid", ""));
        copyStr(g_cfg.staPass, sizeof(g_cfg.staPass), p.getString("sta_pass", ""));
        copyStr(g_cfg.hostname, sizeof(g_cfg.hostname), p.getString("host", g_cfg.hostname));
        g_cfg.brightness = constrain(p.getUChar("bl", g_cfg.brightness), 5, 100);
        g_cfg.ledBrightness = p.getUChar("led_bri", g_cfg.ledBrightness);
        g_cfg.ledEnabled = p.getBool("led_on", g_cfg.ledEnabled);
        g_cfg.defaultVariant = p.getUChar("variant", 0) % 4;
        g_cfg.rollAnimMs = constrain(p.getUShort("roll_ms", g_cfg.rollAnimMs), 300, 2000);
        g_cfg.maxiSavedRolls = p.getBool("maxi_save", true);
    }
    p.end();
    if (!validHostname(g_cfg.hostname)) strcpy(g_cfg.hostname, "ontheroad");
    Serial.printf("[cfg] loaded: sta='%s' host=%s bl=%u%%\n", g_cfg.staSsid, g_cfg.hostname, g_cfg.brightness);
}

AppConfig &get() { return g_cfg; }

bool save()
{
    Preferences p;
    if (!p.begin(NVS_NS, false)) return false;
    p.putUChar("schema", CONFIG_SCHEMA);
    p.remove("ap_pass");   // Soft-AP removed in 0.2.0
    p.putString("sta_ssid", g_cfg.staSsid);
    p.putString("sta_pass", g_cfg.staPass);
    p.putString("host", g_cfg.hostname);
    p.putUChar("bl", g_cfg.brightness);
    p.putUChar("led_bri", g_cfg.ledBrightness);
    p.putBool("led_on", g_cfg.ledEnabled);
    p.putUChar("variant", g_cfg.defaultVariant);
    p.putUShort("roll_ms", g_cfg.rollAnimMs);
    p.putBool("maxi_save", g_cfg.maxiSavedRolls);
    p.end();
    return true;
}

void resetToDefaults()
{
    Preferences p;
    if (p.begin(NVS_NS, false)) {
        p.clear();
        p.end();
    }
    defaults(g_cfg);
}

bool validWifiPass(const char *pass, bool allowEmpty)
{
    if (!pass) return false;
    size_t n = strlen(pass);
    if ((allowEmpty && n == 0) || (n >= 8 && n <= 63)) return true;
    if (n != 64) return false;
    for (size_t i = 0; i < n; i++)   // 64 hex digits = raw WPA PSK
        if (!isxdigit((unsigned char)pass[i])) return false;
    return true;
}

bool validHostname(const char *name)
{
    if (!name) return false;
    size_t n = strlen(name);
    if (n == 0 || n > 31 || name[0] == '-' || name[n - 1] == '-') return false;
    for (size_t i = 0; i < n; i++) {
        char ch = name[i];
        if (!(isalnum((unsigned char)ch) || ch == '-')) return false;
    }
    return true;
}

}  // namespace Config
