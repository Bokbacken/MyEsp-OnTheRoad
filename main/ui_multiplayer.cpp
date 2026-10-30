#include "ui_multiplayer.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "cJSON.h"
#include "esp_http_client.h"
#include "esp_netif_ip_addr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mdns.h"

#include "lang.h"
#include "multiplayer_protocol.h"
#include "my_theme.h"
#include "my_wifi.h"
#include "ui_metrics.h"
#include "ui_yatzy.h"

namespace UiMultiplayer {
namespace {

constexpr int MAX_HOSTS = 8;
constexpr int MAX_SEATS = 8;
constexpr size_t NAME_CAP = 32;
constexpr size_t JSON_CAP = 8192;

struct Host {
    char name[NAME_CAP];
    char ip[16];
    uint16_t port;
    uint8_t seatCount;
    char seats[MAX_SEATS][NAME_CAP];
};

struct ScanState {
    uint32_t generation;
    bool running;
    bool done;
    bool connected;
    int count;
    Host hosts[MAX_HOSTS];
};

struct HttpBody {
    char *data;
    size_t len;
    size_t cap;
    bool overflow;
};

static ScanState s_state = {};
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static lv_obj_t *s_list = nullptr;
static lv_obj_t *s_status = nullptr;
static lv_timer_t *s_timer = nullptr;
static uint32_t s_renderedGeneration = 0;

static esp_err_t receiveHttp(esp_http_client_event_t *event)
{
    if (event->event_id != HTTP_EVENT_ON_DATA || !event->data_len) return ESP_OK;
    HttpBody *body = (HttpBody *)event->user_data;
    if ((size_t)event->data_len > body->cap - body->len) {
        body->overflow = true;
        return ESP_FAIL;
    }
    memcpy(body->data + body->len, event->data, event->data_len);
    body->len += event->data_len;
    body->data[body->len] = '\0';
    return ESP_OK;
}

static const char *txtValue(const mdns_result_t *result, const char *key)
{
    if (!result || !result->txt) return nullptr;
    for (size_t i = 0; i < result->txt_count; i++)
        if (result->txt[i].key && strcmp(result->txt[i].key, key) == 0) return result->txt[i].value;
    return nullptr;
}

static bool addJoinableSeats(const char *json, Host &host)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) return false;
    const cJSON *active = cJSON_GetObjectItemCaseSensitive(root, "active");
    const cJSON *game = cJSON_GetObjectItemCaseSensitive(root, "game_type");
    const cJSON *finished = cJSON_GetObjectItemCaseSensitive(root, "finished");
    const cJSON *players = cJSON_GetObjectItemCaseSensitive(root, "web_players");
    const bool activeGame = cJSON_IsTrue(active);
    const bool yatzy = cJSON_IsString(game) && strcmp(game->valuestring, "yatzy") == 0;
    const bool isFinished = cJSON_IsTrue(finished);
    if (activeGame && yatzy && !isFinished && cJSON_IsArray(players)) {
        const cJSON *seat;
        cJSON_ArrayForEach(seat, players) {
            const cJSON *web = cJSON_GetObjectItemCaseSensitive(seat, "web");
            const cJSON *pin = cJSON_GetObjectItemCaseSensitive(seat, "has_pin");
            const cJSON *connected = cJSON_GetObjectItemCaseSensitive(seat, "connected");
            const cJSON *name = cJSON_GetObjectItemCaseSensitive(seat, "name");
            if (!MultiplayerProtocol::joinableSeat(cJSON_IsTrue(web), cJSON_IsTrue(pin),
                                                    cJSON_IsTrue(connected)) ||
                !cJSON_IsString(name) || host.seatCount >= MAX_SEATS) continue;
            strlcpy(host.seats[host.seatCount++], name->valuestring, NAME_CAP);
        }
    }
    cJSON_Delete(root);
    return MultiplayerProtocol::joinableGame(activeGame, yatzy, isFinished, host.seatCount);
}

static bool queryGame(Host &host)
{
    char url[96];
    snprintf(url, sizeof(url), "http://%s:%u/api/game", host.ip, host.port);
    HttpBody body = {};
    body.cap = JSON_CAP;
    body.data = (char *)malloc(body.cap + 1);
    if (!body.data) return false;
    body.data[0] = '\0';

    esp_http_client_config_t config = {};
    config.url = url;
    config.timeout_ms = 1500;
    config.buffer_size = 1024;
    config.event_handler = receiveHttp;
    config.user_data = &body;
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        free(body.data);
        return false;
    }
    const esp_err_t result = esp_http_client_perform(client);
    const bool ok = result == ESP_OK && !body.overflow &&
                    esp_http_client_get_status_code(client) == 200 &&
                    addJoinableSeats(body.data, host);
    esp_http_client_cleanup(client);
    free(body.data);
    return ok;
}

static bool discoverHost(const mdns_result_t *result, const char *ownIp, Host &host)
{
    if (!result->instance_name) return false;
    mdns_result_t *srv = nullptr;
    mdns_result_t *txt = nullptr;
    const mdns_result_t *records = result;
    if (!result->hostname || !result->port) {
        if (mdns_query_srv(result->instance_name, "_ontheroad", "_tcp", 1200, &srv) != ESP_OK || !srv) return false;
    }
    if (!txtValue(result, "proto") || !txtValue(result, "games")) {
        if (mdns_query_txt(result->instance_name, "_ontheroad", "_tcp", 1200, &txt) != ESP_OK || !txt) {
            if (srv) mdns_query_results_free(srv);
            return false;
        }
        records = txt;
    }
    if (!MultiplayerProtocol::supports(txtValue(records, "proto"), txtValue(records, "games"))) {
        if (srv) mdns_query_results_free(srv);
        if (txt) mdns_query_results_free(txt);
        return false;
    }

    const char *hostname = result->hostname ? result->hostname : srv->hostname;
    host.port = result->port ? result->port : srv->port;
    if (!hostname || !host.port) {
        if (srv) mdns_query_results_free(srv);
        if (txt) mdns_query_results_free(txt);
        return false;
    }
    const mdns_ip_addr_t *address = result->addr;
    while (address && address->addr.type != ESP_IPADDR_TYPE_V4) address = address->next;
    esp_ip4_addr_t resolved = {};
    if (!address && mdns_query_a(hostname, 1200, &resolved) != ESP_OK) {
        if (srv) mdns_query_results_free(srv);
        if (txt) mdns_query_results_free(txt);
        return false;
    }
    const esp_ip4_addr_t *ip = address ? &address->addr.u_addr.ip4 : &resolved;
    snprintf(host.ip, sizeof(host.ip), IPSTR, IP2STR(ip));
    if (ownIp[0] && strcmp(host.ip, ownIp) == 0) {
        if (srv) mdns_query_results_free(srv);
        if (txt) mdns_query_results_free(txt);
        return false;
    }
    const char *device = txtValue(records, "device");
    strlcpy(host.name, device && device[0] ? device :
            result->instance_name, sizeof(host.name));
    const bool found = queryGame(host);
    if (srv) mdns_query_results_free(srv);
    if (txt) mdns_query_results_free(txt);
    return found;
}

static void publishResult(uint32_t generation, const Host *hosts, int count, bool connected)
{
    portENTER_CRITICAL(&s_mux);
    const bool current = generation == s_state.generation;
    portEXIT_CRITICAL(&s_mux);
    if (!current) return;
    for (int i = 0; i < count; i++) s_state.hosts[i] = hosts[i];
    portENTER_CRITICAL(&s_mux);
    if (generation == s_state.generation) {
        s_state.count = count;
        s_state.connected = connected;
        s_state.running = false;
        s_state.done = true;
    }
    portEXIT_CRITICAL(&s_mux);
}

static void scanTask(void *arg)
{
    const uint32_t generation = (uint32_t)(uintptr_t)arg;
    Host found[MAX_HOSTS] = {};
    int count = 0;
    mdns_result_t *results = nullptr;
    char ownIp[16] = {};
    const MyWifi::Info wifi = MyWifi::info();
    strlcpy(ownIp, wifi.ip, sizeof(ownIp));
    if (wifi.connected && mdns_query_ptr("_ontheroad", "_tcp", 1800, MAX_HOSTS, &results) == ESP_OK) {
        for (const mdns_result_t *result = results; result && count < MAX_HOSTS; result = result->next) {
            Host candidate = {};
            if (!discoverHost(result, ownIp, candidate)) continue;
            bool duplicate = false;
            for (int i = 0; i < count; i++) duplicate |= strcmp(found[i].ip, candidate.ip) == 0;
            if (!duplicate) found[count++] = candidate;
        }
    }
    if (results) mdns_query_results_free(results);
    publishResult(generation, found, count, wifi.connected);
    vTaskDelete(nullptr);
}

static uint32_t startScan()
{
    portENTER_CRITICAL(&s_mux);
    if (s_state.running) {
        const uint32_t current = s_state.generation;
        portEXIT_CRITICAL(&s_mux);
        return current;
    }
    s_state.generation++;
    s_state.running = true;
    s_state.done = false;
    s_state.connected = false;
    s_state.count = 0;
    const uint32_t generation = s_state.generation;
    portEXIT_CRITICAL(&s_mux);
    if (xTaskCreatePinnedToCore(scanTask, "multi_scan", 8192, (void *)(uintptr_t)generation, 3, nullptr, 0) != pdPASS) {
        portENTER_CRITICAL(&s_mux);
        if (s_state.generation == generation) {
            s_state.running = false;
            s_state.done = true;
        }
        portEXIT_CRITICAL(&s_mux);
    }
    return generation;
}

static void renderResults()
{
    uint32_t generation;
    int count;
    bool done;
    bool connected;
    portENTER_CRITICAL(&s_mux);
    generation = s_state.generation;
    count = s_state.count;
    done = s_state.done;
    connected = s_state.connected;
    portEXIT_CRITICAL(&s_mux);
    if (!s_status || !s_list || s_renderedGeneration == generation || !done) return;
    s_renderedGeneration = generation;
    lv_obj_clean(s_list);
    if (count == 0) {
        lv_label_set_text(s_status, tr(connected ? Str::MULTI_NO_GAMES : Str::MULTI_WIFI_REQUIRED));
        return;
    }

    lv_label_set_text(s_status, "");
    for (int i = 0; i < count; i++) {
        Host host;
        portENTER_CRITICAL(&s_mux);
        host = s_state.hosts[i];
        portEXIT_CRITICAL(&s_mux);
        lv_obj_t *card = lv_obj_create(s_list);
        lv_obj_remove_style_all(card);
        lv_obj_set_width(card, LV_PCT(100));
        lv_obj_set_height(card, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(card, Theme::c(Theme::PANEL), 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(card, 1, 0);
        lv_obj_set_style_border_color(card, Theme::c(Theme::LINE), 0);
        lv_obj_set_style_radius(card, 10, 0);
        lv_obj_set_style_pad_all(card, UiMetrics::marginSmall(), 0);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(card, UiMetrics::marginSmall(), 0);

        char titleLine[96];
        snprintf(titleLine, sizeof(titleLine), tr(Str::MULTI_HOST_FMT), host.name, host.ip);
        lv_obj_t *title = Theme::label(card, titleLine, &my_font_18);
        lv_label_set_long_mode(title, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_width(title, LV_PCT(100));

        char names[MAX_SEATS * (NAME_CAP + 2)] = {};
        size_t used = 0;
        for (int seat = 0; seat < host.seatCount; seat++) {
            const int wrote = snprintf(names + used, sizeof(names) - used, "%s%s",
                                       seat ? ", " : "", host.seats[seat]);
            if (wrote < 0 || (size_t)wrote >= sizeof(names) - used) break;
            used += (size_t)wrote;
        }
        char seatsLine[sizeof(names) + 32];
        snprintf(seatsLine, sizeof(seatsLine), tr(Str::MULTI_SEATS_FMT), names);
        lv_obj_t *seats = Theme::label(card, seatsLine, &my_font_14, Theme::TEAL);
        lv_label_set_long_mode(seats, LV_LABEL_LONG_MODE_WRAP);
        lv_obj_set_width(seats, LV_PCT(100));
    }
}

static void pollScan(lv_timer_t *)
{
    renderResults();
}

static void closeScreen()
{
    if (s_timer) {
        lv_timer_delete(s_timer);
        s_timer = nullptr;
    }
    s_status = s_list = nullptr;
    UiYatzy::showMenu();
}

static void refreshEvent(lv_event_t *)
{
    s_renderedGeneration = 0;
    lv_label_set_text(s_status, tr(Str::MULTI_SEARCHING));
    lv_obj_clean(s_list);
    startScan();
}

}  // namespace

void show()
{
    if (s_timer) lv_timer_delete(s_timer);
    s_timer = nullptr;
    s_status = s_list = nullptr;
    s_renderedGeneration = 0;
    startScan();

    lv_obj_t *screen = Theme::screen();
    Theme::header(screen, tr(Str::MULTI_TITLE));
    const int32_t margin = UiMetrics::marginNormal();
    const int32_t top = UiMetrics::headerH();
    lv_obj_t *hint = Theme::label(screen, tr(Str::MULTI_HINT), &my_font_14, Theme::MUTED);
    lv_obj_set_pos(hint, margin, top + UiMetrics::marginSmall());
    lv_obj_set_width(hint, UiMetrics::screenW() - 2 * margin);

    s_status = Theme::label(screen, tr(Str::MULTI_SEARCHING), &my_font_14, Theme::MUTED);
    lv_obj_set_pos(s_status, margin, top + UiMetrics::marginSmall() + 26);
    lv_obj_set_width(s_status, UiMetrics::screenW() - 2 * margin);
    lv_label_set_long_mode(s_status, LV_LABEL_LONG_MODE_WRAP);

    const int32_t buttonH = UiMetrics::buttonH();
    const int32_t buttonW = (UiMetrics::screenW() - 3 * margin) / 2;
    const int32_t noteH = 2 * lv_font_get_line_height(&my_font_14);
    const int32_t buttonTop = UiMetrics::screenH() - UiMetrics::marginSmall() - buttonH;
    const int32_t noteBottom = buttonTop - UiMetrics::marginSmall();
    const int32_t listTop = top + 2 * UiMetrics::marginNormal() + 42;
    const int32_t noteTop = noteBottom - noteH;

    s_list = lv_obj_create(screen);
    lv_obj_remove_style_all(s_list);
    lv_obj_set_pos(s_list, margin, listTop);
    lv_obj_set_size(s_list, UiMetrics::screenW() - 2 * margin, noteTop - listTop - UiMetrics::marginSmall());
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_list, UiMetrics::cardSpacing(), 0);
    lv_obj_set_scroll_dir(s_list, LV_DIR_VER);

    lv_obj_t *notice = Theme::label(screen, tr(Str::MULTI_NOT_READY), &my_font_14, Theme::GOLD);
    lv_label_set_long_mode(notice, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(notice, UiMetrics::screenW() - 2 * margin);
    lv_obj_set_height(notice, noteH);
    lv_obj_set_pos(notice, margin, noteTop);

    lv_obj_t *back = Theme::button(screen, trp(LV_SYMBOL_LEFT " ", Str::BTN_BACK),
                                   buttonW, buttonH, Theme::MUTED);
    lv_obj_align(back, LV_ALIGN_BOTTOM_LEFT, margin, -UiMetrics::marginSmall());
    lv_obj_add_event_cb(back, [](lv_event_t *) { closeScreen(); }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *refresh = Theme::button(screen, tr(Str::MULTI_REFRESH), buttonW, buttonH, Theme::TEAL);
    lv_obj_align(refresh, LV_ALIGN_BOTTOM_RIGHT, -margin, -UiMetrics::marginSmall());
    lv_obj_add_event_cb(refresh, refreshEvent, LV_EVENT_CLICKED, nullptr);

    s_timer = lv_timer_create(pollScan, 200, nullptr);
    Theme::loadScreen(screen);
}

}  // namespace UiMultiplayer
