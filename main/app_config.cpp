#include "app_config.h"
#include "lang.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "my_storage.h"
#include "my_sys.h"
#include "my_wifi.h"

static const char *TAG = "cfg";

namespace {
constexpr uint8_t CONFIG_SCHEMA = 1;
constexpr uint8_t NETS_SCHEMA = 1;
constexpr const char *DEFAULT_TZ = "CET-1CEST,M3.5.0,M10.5.0/3";
AppConfig g_cfg;
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
    c.throwMode = 0;
    strcpy(c.tz, DEFAULT_TZ);
    strcpy(c.lang, "sv");
}

void begin()
{
    defaults(g_cfg);
    {
        MyNvs::Handle p(CFG_NVS_NS, true);
        if (!p) {
            ESP_LOGI(TAG, "no stored settings, using defaults");
            return;
        }
        if (p.getU8("schema", 0) == CONFIG_SCHEMA) {
            p.getStr("host", g_cfg.hostname, sizeof(g_cfg.hostname));
            g_cfg.brightness = MySys::clamp<uint8_t>(p.getU8("bl", g_cfg.brightness), 5, 100);
            g_cfg.ledBrightness = p.getU8("led_bri", g_cfg.ledBrightness);
            g_cfg.ledEnabled = p.getBool("led_on", g_cfg.ledEnabled);
            g_cfg.defaultVariant = p.getU8("variant", 0) % 4;
            g_cfg.rollAnimMs = MySys::clamp<uint16_t>(p.getU16("roll_ms", g_cfg.rollAnimMs), 300, 2000);
            g_cfg.maxiSavedRolls = p.getBool("maxi_save", true);
            g_cfg.throwMode = p.getU8("throw_mode", 0) > 1 ? 0 : p.getU8("throw_mode", 0);
            p.getStr("admin_pin", g_cfg.adminPin, sizeof(g_cfg.adminPin));
            p.getStr("tz", g_cfg.tz, sizeof(g_cfg.tz));
            p.getStr("lang", g_cfg.lang, sizeof(g_cfg.lang));
            g_cfg.rotation = p.getU16("rotation", 0) == 180 ? 180 : 0;
        }
    }
    if (!validHostname(g_cfg.hostname)) strcpy(g_cfg.hostname, "ontheroad");
    if (!validAdminPin(g_cfg.adminPin, true)) g_cfg.adminPin[0] = 0;
    if (!validTz(g_cfg.tz)) strcpy(g_cfg.tz, DEFAULT_TZ);
    if (!Lang::valid(g_cfg.lang)) strcpy(g_cfg.lang, "sv");
    ESP_LOGI(TAG, "loaded: host=%s bl=%u%% pin=%s", g_cfg.hostname, g_cfg.brightness, g_cfg.adminPin[0] ? "set" : "none");
}

AppConfig &get() { return g_cfg; }
const char *hostname() { return g_cfg.hostname; }

bool save()
{
    MyNvs::Handle p(CFG_NVS_NS, false);
    if (!p) return false;
    bool ok = p.setU8("schema", CONFIG_SCHEMA);
    p.erase("ap_pass");   // Soft-AP removed in 0.2.0
    ok &= p.setStr("host", g_cfg.hostname);
    ok &= p.setU8("bl", g_cfg.brightness);
    ok &= p.setU8("led_bri", g_cfg.ledBrightness);
    ok &= p.setBool("led_on", g_cfg.ledEnabled);
    ok &= p.setU8("variant", g_cfg.defaultVariant);
    ok &= p.setU16("roll_ms", g_cfg.rollAnimMs);
    ok &= p.setBool("maxi_save", g_cfg.maxiSavedRolls);
    ok &= p.setU8("throw_mode", g_cfg.throwMode);
    ok &= p.setStr("admin_pin", g_cfg.adminPin);
    ok &= p.setStr("tz", g_cfg.tz);
    ok &= p.setStr("lang", g_cfg.lang);
    ok &= p.setU16("rotation", g_cfg.rotation == 180 ? 180 : 0);
    return p.commit() && ok;
}

bool loadNets(NetList &out)
{
    out.clear();
    bool migrate = false;
    {
        MyNvs::Handle p(CFG_NVS_NS, true);
        if (!p) return true;
        if (p.getU8("nets_v", 0) == NETS_SCHEMA && p.blobSize("nets") == sizeof(NetList)) {
            if (!p.getBlob("nets", &out, sizeof(NetList)) || out.count > NET_LIST_MAX) out.clear();
            for (int i = 0; i < out.count; i++) {   // Defensive termination
                out.items[i].ssid[32] = 0;
                out.items[i].pass[64] = 0;
            }
        } else {
            char ssid[33] = "", pass[65] = "";
            if (p.getStr("sta_ssid", ssid, sizeof(ssid)) && ssid[0]) {
                p.getStr("sta_pass", pass, sizeof(pass));
                out.upsert(ssid, pass);
                migrate = true;
            }
            memset(pass, 0, sizeof(pass));
        }
    }
    if (migrate && saveNets(out)) {
        MyNvs::Handle w(CFG_NVS_NS, false);
        if (w) {
            w.erase("sta_ssid");
            w.erase("sta_pass");
        }
        ESP_LOGI(TAG, "migrated the 0.2.0 network to the saved-network list");
    }
    ESP_LOGI(TAG, "%u saved network(s)", out.count);
    return true;
}

bool saveNets(const NetList &nets)
{
    MyNvs::Handle p(CFG_NVS_NS, false);
    if (!p) return false;
    bool ok = p.setBlob("nets", &nets, sizeof(NetList));
    ok &= p.setU8("nets_v", NETS_SCHEMA);
    return p.commit() && ok;
}

void resetToDefaults()
{
    {
        MyNvs::Handle p(CFG_NVS_NS, false);
        if (p) p.eraseAll();
    }
    defaults(g_cfg);
    MyWifi::forgetAll();
}

bool validWifiPass(const char *pass, bool allowEmpty) { return MyWifi::validPass(pass, allowEmpty); }

bool validHostname(const char *name)
{
    if (!name) return false;
    size_t n = strlen(name);
    if (n == 0 || n > 31 || name[0] == '-' || name[n - 1] == '-') return false;
    for (size_t i = 0; i < n; i++)
        if (!(isalnum((unsigned char)name[i]) || name[i] == '-')) return false;
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
