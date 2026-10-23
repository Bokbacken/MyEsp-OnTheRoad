#include "board_config.h"

#include <ctype.h>

#include <Arduino.h>
#include <Preferences.h>
#include <string.h>

namespace {
constexpr const char *NVS_NS = "otr";
constexpr uint8_t CONFIG_SCHEMA = 1;
AppConfig g_cfg;
NetList g_nets;
constexpr uint8_t NETS_SCHEMA = 1;
constexpr const char *DEFAULT_TZ = "CET-1CEST,M3.5.0,M10.5.0/3";

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
    strcpy(c.tz, DEFAULT_TZ);
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
        copyStr(g_cfg.hostname, sizeof(g_cfg.hostname), p.getString("host", g_cfg.hostname));
        g_cfg.brightness = constrain(p.getUChar("bl", g_cfg.brightness), 5, 100);
        g_cfg.ledBrightness = p.getUChar("led_bri", g_cfg.ledBrightness);
        g_cfg.ledEnabled = p.getBool("led_on", g_cfg.ledEnabled);
        g_cfg.defaultVariant = p.getUChar("variant", 0) % 4;
        g_cfg.rollAnimMs = constrain(p.getUShort("roll_ms", g_cfg.rollAnimMs), 300, 2000);
        g_cfg.maxiSavedRolls = p.getBool("maxi_save", true);
        copyStr(g_cfg.adminPin, sizeof(g_cfg.adminPin), p.getString("admin_pin", ""));
        copyStr(g_cfg.tz, sizeof(g_cfg.tz), p.getString("tz", DEFAULT_TZ));
    }
    // Saved networks: one blob. 0.2.0 stored a single sta_ssid/sta_pass -> migrate it.
    g_nets.clear();
    bool migrate = false;
    if (p.getUChar("nets_v", 0) == NETS_SCHEMA && p.getBytesLength("nets") == sizeof(NetList)) {
        p.getBytes("nets", &g_nets, sizeof(NetList));
        if (g_nets.count > NET_LIST_MAX) g_nets.clear();
        for (int i = 0; i < g_nets.count; i++) {   // Defensive termination
            g_nets.items[i].ssid[32] = 0;
            g_nets.items[i].pass[64] = 0;
        }
    } else {
        String ssid = p.getString("sta_ssid", "");
        if (ssid.length()) {
            g_nets.upsert(ssid.c_str(), p.getString("sta_pass", "").c_str());
            migrate = true;
        }
    }
    p.end();
    if (migrate) {
        saveNets();
        Preferences w;
        if (w.begin(NVS_NS, false)) {
            w.remove("sta_ssid");
            w.remove("sta_pass");
            w.end();
        }
        Serial.println("[cfg] migrated the 0.2.0 network to the saved-network list");
    }
    if (!validHostname(g_cfg.hostname)) strcpy(g_cfg.hostname, "ontheroad");
    if (!validAdminPin(g_cfg.adminPin, true)) g_cfg.adminPin[0] = 0;
    if (!validTz(g_cfg.tz)) strcpy(g_cfg.tz, DEFAULT_TZ);
    Serial.printf("[cfg] loaded: %u saved network(s) host=%s bl=%u%%\n", g_nets.count, g_cfg.hostname, g_cfg.brightness);
}

AppConfig &get() { return g_cfg; }

bool save()
{
    Preferences p;
    if (!p.begin(NVS_NS, false)) return false;
    p.putUChar("schema", CONFIG_SCHEMA);
    p.remove("ap_pass");   // Soft-AP removed in 0.2.0
    p.putString("host", g_cfg.hostname);
    p.putUChar("bl", g_cfg.brightness);
    p.putUChar("led_bri", g_cfg.ledBrightness);
    p.putBool("led_on", g_cfg.ledEnabled);
    p.putUChar("variant", g_cfg.defaultVariant);
    p.putUShort("roll_ms", g_cfg.rollAnimMs);
    p.putBool("maxi_save", g_cfg.maxiSavedRolls);
    p.putString("admin_pin", g_cfg.adminPin);
    p.putString("tz", g_cfg.tz);
    p.end();
    return true;
}

NetList &nets() { return g_nets; }

bool saveNets()
{
    Preferences p;
    if (!p.begin(NVS_NS, false)) return false;
    bool ok = p.putBytes("nets", &g_nets, sizeof(NetList)) == sizeof(NetList);
    p.putUChar("nets_v", NETS_SCHEMA);
    p.end();
    return ok;
}

void resetToDefaults()
{
    Preferences p;
    if (p.begin(NVS_NS, false)) {
        p.clear();
        p.end();
    }
    defaults(g_cfg);
    g_nets.clear();
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

bool validAdminPin(const char *pin, bool allowEmpty)
{
    if (!pin) return false;
    size_t n = strlen(pin);
    if (n == 0) return allowEmpty;
    if (n < 4 || n > 8) return false;
    for (size_t i = 0; i < n; i++)
        if (!isdigit((unsigned char)pin[i])) return false;
    return true;
}

bool adminOk(const char *pin)
{
    const char *want = g_cfg.adminPin;
    if (!want[0]) return true;
    if (!pin) return false;
    size_t a = strlen(want), b = strlen(pin);
    uint8_t diff = (uint8_t)(a != b);
    for (size_t i = 0; i < a; i++) diff |= (uint8_t)(want[i] ^ (i < b ? pin[i] : 0));
    return diff == 0;
}

bool validTz(const char *tz)
{
    if (!tz) return false;
    size_t n = strlen(tz);
    if (n < 3 || n >= sizeof(AppConfig::tz)) return false;
    for (size_t i = 0; i < n; i++)
        if (tz[i] < 0x21 || tz[i] > 0x7E) return false;
    return true;
}

}  // namespace Config
