#include "ui_multiplayer.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "cJSON.h"
#include "dice_cup.h"
#include "esp_err.h"
#include "esp_http_client.h"
#include "esp_netif_ip_addr.h"
#include "esp_log.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "game_yatzy.h"
#include "mdns.h"

#include "lang.h"
#include "multiplayer_protocol.h"
#include "my_theme.h"
#include "my_wifi.h"
#include "ui_metrics.h"
#include "ui_numpad.h"
#include "ui_yatzy.h"

namespace UiMultiplayer {
namespace {

constexpr int MAX_HOSTS = 8;
constexpr int MAX_SEATS = 8;
constexpr size_t NAME_CAP = 32;
constexpr size_t JSON_CAP = 8192;
constexpr size_t REMOTE_FRAME_CAP = 8192;
constexpr int REMOTE_WS_TASK_STACK = 8192;
constexpr int REMOTE_CATEGORY_CAP = 20;
constexpr uint32_t REMOTE_CHOOSE_DOUBLE_TAP_MS = 500;
constexpr char TAG[] = "multi";

struct Host {
    char name[NAME_CAP];
    char ip[16];
    char sessionId[17];
    uint16_t port;
    uint8_t seatCount;
    struct Seat {
        uint8_t player;
        char name[NAME_CAP];
    } seats[MAX_SEATS];
};

struct RemoteCategory {
    int id;
    int potential;
    bool available;
    char name[NAME_CAP];
    int playerScores[YATZY_MAX_PLAYERS];
    bool scoreSet[YATZY_MAX_PLAYERS];
};

struct RemotePlayer {
    char name[NAME_CAP];
    int upper;
    int bonus;
    int total;
    bool hasUpper;
    bool hasBonus;
};

struct RemoteGame {
    uint32_t version;
    uint32_t revision;
    bool connected;
    bool joined;
    bool needsJoin;
    bool terminal;
    bool active;
    bool finished;
    bool rolling;
    bool hasRolled;
    bool canRoll;
    int you;
    int current;
    int round;
    int rounds;
    int rollsLeft;
    int diceCount;
    uint8_t dice[YATZY_MAX_DICE];
    bool held[YATZY_MAX_DICE];
    bool rollMask[YATZY_MAX_DICE];
    int categoryCount;
    RemoteCategory categories[REMOTE_CATEGORY_CAP];
    int playerCount;
    RemotePlayer players[YATZY_MAX_PLAYERS];
    char gameName[NAME_CAP];
    char sessionId[17];
    char status[96];
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
static lv_obj_t *s_remoteStatus = nullptr;
static lv_obj_t *s_remoteRound = nullptr;
static lv_obj_t *s_remoteDice = nullptr;
static lv_obj_t *s_remoteNest = nullptr;
static lv_obj_t *s_remoteDie[YATZY_MAX_DICE] = {};
static int32_t s_remoteDieSize[YATZY_MAX_DICE] = {};
static lv_obj_t *s_remoteRoll = nullptr;
static lv_obj_t *s_remoteCategories = nullptr;
static lv_obj_t *s_remoteCupArea = nullptr;
static lv_obj_t *s_remoteCupRim = nullptr;
static lv_obj_t *s_remoteCupLip = nullptr;
static lv_obj_t *s_remoteCupBottom = nullptr;
static lv_obj_t *s_remoteCupDice = nullptr;
static lv_obj_t *s_remoteCupDie[YATZY_MAX_DICE] = {};
static lv_obj_t *s_remoteCupCount = nullptr;
static lv_obj_t *s_remoteCupHint = nullptr;
static lv_obj_t *s_remoteCupChoose = nullptr;
static uint32_t s_remoteRenderedRevision = UINT32_MAX;
static lv_timer_t *s_remoteDiceTimer = nullptr;
static int s_remoteLastChoiceId = -1;
static uint32_t s_remoteLastChoiceMs = 0;
static RemoteGame s_remote = {};
static portMUX_TYPE s_remoteMux = portMUX_INITIALIZER_UNLOCKED;
static esp_websocket_client_handle_t s_remoteClient = nullptr;
static Host s_remoteHost = {};
static int s_remoteSeat = -1;
static char s_remoteSession[17] = {};
static char s_remotePin[5] = {};
static char s_remoteUri[96] = {};
static char s_wsFrame[REMOTE_FRAME_CAP + 1] = {};
static size_t s_wsFrameUsed = 0;
static size_t s_wsFrameExpected = 0;
static lv_obj_t *s_remotePlayPanel = nullptr;
static lv_obj_t *s_remoteScorePanel = nullptr;
static lv_obj_t *s_remoteScoreTable = nullptr;
static lv_obj_t *s_remoteScoreButton = nullptr;
static bool s_remoteShowingScorecard = false;
static bool s_remoteCupActive = false;
static bool s_remoteCupPressed = false;
static DiceCup::ShakeTracker s_remoteCupShake;
static DiceCup::Body s_remoteCupBodies[YATZY_MAX_DICE] = {};
static int s_remoteCupBodyCount = 0;
static DiceCup::CupParams s_remoteCupParams = {};
static lv_timer_t *s_remoteCupTimer = nullptr;
static uint32_t s_remoteCupLastMs = 0;
static int32_t s_remoteCupBaseX = 0;
static int32_t s_remoteCupBaseY = 0;
static float s_remoteCupCenterX = 0;
static float s_remoteCupCenterY = 0;
static float s_remoteCupOffX = 0;
static float s_remoteCupOffY = 0;
static float s_remoteCupVx = 0;
static float s_remoteCupVy = 0;
static int s_selectedHost = -1;
static int s_selectedVisibleSeat = -1;
static Host s_selectedHostData = {};
static lv_obj_t *s_pinPad = nullptr;

static void selectSeatEvent(lv_event_t *event);
static void showRemoteGame();
static bool startRemoteGame(const char *pin);
static void renderRemoteDice(const RemoteGame &state);
static void closeRemoteCup();
static void remoteCupTick(lv_timer_t *timer);

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

static void remoteStatus(const char *text, bool terminal = false)
{
    portENTER_CRITICAL(&s_remoteMux);
    strlcpy(s_remote.status, text ? text : "", sizeof(s_remote.status));
    if (terminal) {
        s_remote.terminal = true;
        s_remote.joined = false;
        s_remote.needsJoin = false;
        s_remotePin[0] = '\0';
    }
    s_remote.revision++;
    portEXIT_CRITICAL(&s_remoteMux);
}

static bool sendRemoteJoin()
{
    esp_websocket_client_handle_t client;
    char pin[sizeof(s_remotePin)];
    char session[sizeof(s_remoteSession)];
    int seat;
    portENTER_CRITICAL(&s_remoteMux);
    client = s_remoteClient;
    memcpy(pin, s_remotePin, sizeof(pin));
    memcpy(session, s_remoteSession, sizeof(session));
    seat = s_remoteSeat;
    if (s_remote.terminal || !s_remote.needsJoin) {
        portEXIT_CRITICAL(&s_remoteMux);
        return false;
    }
    portEXIT_CRITICAL(&s_remoteMux);
    if (!client || seat < 0 || !MultiplayerProtocol::validSessionId(session) ||
        strlen(pin) != 4 || !esp_websocket_client_is_connected(client)) {
        memset(pin, 0, sizeof(pin));
        memset(session, 0, sizeof(session));
        return false;
    }
    char message[128];
    const int len = snprintf(message, sizeof(message),
                             "{\"cmd\":\"join\",\"player\":%d,\"pin\":\"%s\",\"session_id\":\"%s\"}",
                             seat, pin, session);
    if (len <= 0 || (size_t)len >= sizeof(message) ||
        esp_websocket_client_send_text(client, message, len, pdMS_TO_TICKS(1000)) != len) {
        remoteStatus(tr(Str::MULTI_REMOTE_DISCONNECTED));
        memset(pin, 0, sizeof(pin));
        memset(session, 0, sizeof(session));
        memset(message, 0, sizeof(message));
        return false;
    }
    portENTER_CRITICAL(&s_remoteMux);
    s_remote.needsJoin = false;
    s_remote.revision++;
    portEXIT_CRITICAL(&s_remoteMux);
    memset(pin, 0, sizeof(pin));
    memset(message, 0, sizeof(message));
    return true;
}

static void sendRemoteCommand(const char *cmd, int arg = -1)
{
    RemoteGame state;
    portENTER_CRITICAL(&s_remoteMux);
    state = s_remote;
    portEXIT_CRITICAL(&s_remoteMux);
    MultiplayerProtocol::RemoteActions actions = {
        state.joined, state.active, state.finished, state.rolling, state.hasRolled,
        state.canRoll, state.you, state.current
    };
    bool categoryAvailable = false;
    for (int i = 0; i < state.categoryCount; i++)
        categoryAvailable |= state.categories[i].id == arg && state.categories[i].available;
    const bool allowed = strcmp(cmd, "roll") == 0 ? MultiplayerProtocol::canRemoteRoll(actions) :
                         strcmp(cmd, "hold") == 0 ? MultiplayerProtocol::canRemoteHold(actions) &&
                                                       arg >= 0 && arg < state.diceCount :
                         strcmp(cmd, "choose") == 0 ? MultiplayerProtocol::canRemoteChoose(actions, categoryAvailable) :
                         false;
    if (!allowed || !state.connected) return;
    char message[48];
    int len;
    if (strcmp(cmd, "hold") == 0) len = snprintf(message, sizeof(message), "{\"cmd\":\"hold\",\"die\":%d}", arg);
    else if (strcmp(cmd, "choose") == 0) len = snprintf(message, sizeof(message), "{\"cmd\":\"choose\",\"cat\":%d}", arg);
    else len = snprintf(message, sizeof(message), "{\"cmd\":\"roll\"}");
    if (len > 0 && (size_t)len < sizeof(message)) {
        esp_websocket_client_handle_t client;
        portENTER_CRITICAL(&s_remoteMux);
        client = s_remoteClient;
        portEXIT_CRITICAL(&s_remoteMux);
        if (client) esp_websocket_client_send_text(client, message, len, pdMS_TO_TICKS(1000));
    }
}

static void copyJsonString(const cJSON *object, const char *key, char *out, size_t cap)
{
    const cJSON *value = cJSON_GetObjectItemCaseSensitive(object, key);
    if (cJSON_IsString(value)) strlcpy(out, value->valuestring, cap);
}

static const char *remoteErrorCode(const cJSON *code)
{
    static const char *const known[] = {
        "nogame", "seat", "seatbusy", "session", "pin", "nopin", "locked",
        "join", "full", "cmd", "turn"
    };
    if (!cJSON_IsString(code) || !code->valuestring) return "unknown";
    for (const char *candidate : known)
        if (strcmp(code->valuestring, candidate) == 0) return candidate;
    return "unknown";
}

static bool readRemoteState(const cJSON *json, RemoteGame &out,
                            const char *expectedSession, int expectedSeat)
{
    const cJSON *session = cJSON_GetObjectItemCaseSensitive(json, "session_id");
    const cJSON *type = cJSON_GetObjectItemCaseSensitive(json, "game_type");
    const cJSON *joined = cJSON_GetObjectItemCaseSensitive(json, "joined");
    const cJSON *you = cJSON_GetObjectItemCaseSensitive(json, "you");
    if (!cJSON_IsString(session) ||
        !MultiplayerProtocol::sameSession(expectedSession, session->valuestring) ||
        !cJSON_IsString(type) || strcmp(type->valuestring, "yatzy") != 0 ||
        !cJSON_IsTrue(joined) || !cJSON_IsNumber(you) || you->valueint != expectedSeat) return false;

    RemoteGame next = {};
    next.joined = true;
    next.you = you->valueint;
    next.active = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(json, "active"));
    next.finished = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(json, "finished"));
    next.rolling = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(json, "rolling"));
    next.hasRolled = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(json, "has_rolled"));
    next.canRoll = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(json, "can_roll"));
    const cJSON *current = cJSON_GetObjectItemCaseSensitive(json, "current");
    const cJSON *round = cJSON_GetObjectItemCaseSensitive(json, "round");
    const cJSON *rounds = cJSON_GetObjectItemCaseSensitive(json, "rounds");
    const cJSON *rollsLeft = cJSON_GetObjectItemCaseSensitive(json, "rolls_left");
    const cJSON *version = cJSON_GetObjectItemCaseSensitive(json, "version");
    const cJSON *dice = cJSON_GetObjectItemCaseSensitive(json, "dice");
    const cJSON *held = cJSON_GetObjectItemCaseSensitive(json, "held");
    if (!next.active || !cJSON_IsNumber(current) || !cJSON_IsNumber(round) ||
        !cJSON_IsNumber(rounds) || !cJSON_IsNumber(rollsLeft) ||
        !cJSON_IsNumber(version) || !cJSON_IsArray(dice) || !cJSON_IsArray(held)) return false;
    next.current = current->valueint;
    next.round = round->valueint;
    next.rounds = rounds->valueint;
    next.rollsLeft = rollsLeft->valueint;
    next.version = (uint32_t)version->valuedouble;
    copyJsonString(json, "name", next.gameName, sizeof(next.gameName));
    strlcpy(next.sessionId, session->valuestring, sizeof(next.sessionId));
    next.diceCount = cJSON_GetArraySize(dice);
    if (next.diceCount < 1 || next.diceCount > YATZY_MAX_DICE ||
        cJSON_GetArraySize(held) != next.diceCount) return false;
    for (int i = 0; i < next.diceCount; i++) {
        const cJSON *face = cJSON_GetArrayItem(dice, i);
        const cJSON *isHeld = cJSON_GetArrayItem(held, i);
        if (!cJSON_IsNumber(face) || face->valueint < 0 || face->valueint > 6 ||
            !cJSON_IsBool(isHeld)) return false;
        next.dice[i] = (uint8_t)face->valueint;
        next.held[i] = cJSON_IsTrue(isHeld);
    }
    if (next.rolling) {
        const cJSON *rollMask = cJSON_GetObjectItemCaseSensitive(json, "roll_mask");
        if (cJSON_IsArray(rollMask) && cJSON_GetArraySize(rollMask) == next.diceCount) {
            for (int i = 0; i < next.diceCount; i++) {
                const cJSON *rollingDie = cJSON_GetArrayItem(rollMask, i);
                if (!cJSON_IsBool(rollingDie)) return false;
                next.rollMask[i] = cJSON_IsTrue(rollingDie);
            }
        } else {
            for (int i = 0; i < next.diceCount; i++)
                next.rollMask[i] = !next.held[i];
        }
    }
    const cJSON *categories = cJSON_GetObjectItemCaseSensitive(json, "categories");
    if (cJSON_IsArray(categories)) {
        const cJSON *category;
        cJSON_ArrayForEach(category, categories) {
            if (next.categoryCount >= REMOTE_CATEGORY_CAP) break;
            const cJSON *id = cJSON_GetObjectItemCaseSensitive(category, "id");
            const cJSON *potential = cJSON_GetObjectItemCaseSensitive(category, "potential");
            const cJSON *name = cJSON_GetObjectItemCaseSensitive(category, "name");
            if (!cJSON_IsNumber(id) || id->valueint < 0 || id->valueint > UINT8_MAX ||
                !cJSON_IsString(name)) continue;
            RemoteCategory &item = next.categories[next.categoryCount++];
            item.id = id->valueint;
            item.available = cJSON_IsNumber(potential);
            item.potential = item.available ? potential->valueint : 0;
            strlcpy(item.name, name->valuestring, sizeof(item.name));
        }
    }
    const cJSON *players = cJSON_GetObjectItemCaseSensitive(json, "players");
    if (!cJSON_IsArray(players)) return false;
    const cJSON *player;
    cJSON_ArrayForEach(player, players) {
        if (next.playerCount >= YATZY_MAX_PLAYERS) break;
        RemotePlayer &item = next.players[next.playerCount++];
        copyJsonString(player, "name", item.name, sizeof(item.name));
        const cJSON *upper = cJSON_GetObjectItemCaseSensitive(player, "upper");
        const cJSON *bonus = cJSON_GetObjectItemCaseSensitive(player, "bonus");
        const cJSON *total = cJSON_GetObjectItemCaseSensitive(player, "total");
        const cJSON *scores = cJSON_GetObjectItemCaseSensitive(player, "scores");
        if (!cJSON_IsNumber(total) || !cJSON_IsArray(scores) ||
            cJSON_GetArraySize(scores) != next.categoryCount) return false;
        item.hasUpper = cJSON_IsNumber(upper);
        item.hasBonus = cJSON_IsNumber(bonus);
        item.upper = item.hasUpper ? upper->valueint : 0;
        item.bonus = item.hasBonus ? bonus->valueint : 0;
        item.total = total->valueint;
        for (int i = 0; i < next.categoryCount; i++) {
            const cJSON *score = cJSON_GetArrayItem(scores, i);
            if (cJSON_IsNumber(score)) {
                next.categories[i].playerScores[next.playerCount - 1] = score->valueint;
                next.categories[i].scoreSet[next.playerCount - 1] = true;
            } else if (!cJSON_IsNull(score)) {
                return false;
            }
        }
    }
    if (next.you < 0 || next.you >= next.playerCount || next.current < 0 ||
        next.current >= next.playerCount) return false;
    out = next;
    return true;
}

static void handleRemoteMessage(const char *text)
{
    cJSON *json = cJSON_Parse(text);
    if (!json) return;
    char expectedSession[sizeof(s_remoteSession)];
    int expectedSeat;
    portENTER_CRITICAL(&s_remoteMux);
    memcpy(expectedSession, s_remoteSession, sizeof(expectedSession));
    expectedSeat = s_remoteSeat;
    portEXIT_CRITICAL(&s_remoteMux);
    const cJSON *type = cJSON_GetObjectItemCaseSensitive(json, "type");
    if (cJSON_IsString(type) && strcmp(type->valuestring, "joined") == 0) {
        const cJSON *you = cJSON_GetObjectItemCaseSensitive(json, "you");
        const cJSON *session = cJSON_GetObjectItemCaseSensitive(json, "session_id");
        const bool ok = cJSON_IsNumber(you) && you->valueint == expectedSeat &&
                        cJSON_IsString(session) &&
                        MultiplayerProtocol::sameSession(expectedSession, session->valuestring);
        if (ok) {
            ESP_LOGI(TAG, "remote join accepted seat=%d", expectedSeat);
            portENTER_CRITICAL(&s_remoteMux);
            s_remote.joined = true;
            s_remote.needsJoin = false;
            s_remote.status[0] = '\0';
            s_remote.revision++;
            portEXIT_CRITICAL(&s_remoteMux);
        } else {
            ESP_LOGW(TAG, "remote join response rejected seat=%d", expectedSeat);
            remoteStatus(tr(Str::MULTI_SESSION_CHANGED), true);
        }
    } else if (cJSON_IsString(type) && strcmp(type->valuestring, "state") == 0) {
        const cJSON *session = cJSON_GetObjectItemCaseSensitive(json, "session_id");
        const cJSON *joined = cJSON_GetObjectItemCaseSensitive(json, "joined");
        const auto decision = MultiplayerProtocol::remoteStateDecision(
            expectedSession, cJSON_IsString(session) ? session->valuestring : nullptr,
            cJSON_IsTrue(joined));
        if (decision == MultiplayerProtocol::RemoteStateDecision::RequireFreshJoin) {
            remoteStatus(tr(Str::MULTI_SESSION_CHANGED), true);
        } else if (decision == MultiplayerProtocol::RemoteStateDecision::ApplySnapshot) {
            RemoteGame next = {};
            if (readRemoteState(json, next, expectedSession, expectedSeat)) {
                portENTER_CRITICAL(&s_remoteMux);
                next.connected = s_remote.connected;
                next.revision = s_remote.revision + 1;
                s_remote = next;
                portEXIT_CRITICAL(&s_remoteMux);
            }
        }
    } else if (cJSON_IsString(type) && strcmp(type->valuestring, "error") == 0) {
        const cJSON *code = cJSON_GetObjectItemCaseSensitive(json, "error");
        char message[96] = {};
        const bool seatTaken = cJSON_IsString(code) && strcmp(code->valuestring, "seatbusy") == 0;
        const bool sessionChanged = cJSON_IsString(code) && strcmp(code->valuestring, "session") == 0;
        if (seatTaken || sessionChanged)
            strlcpy(message, tr(seatTaken ? Str::MULTI_SEAT_TAKEN : Str::MULTI_SESSION_CHANGED),
                    sizeof(message));
        else
            copyJsonString(json, "msg", message, sizeof(message));
        if (!message[0]) strlcpy(message, tr(Str::MULTI_REMOTE_DISCONNECTED), sizeof(message));
        const bool retryable = cJSON_IsString(code) &&
                               (strcmp(code->valuestring, "turn") == 0 ||
                                strcmp(code->valuestring, "cmd") == 0);
        ESP_LOGW(TAG, "remote server rejected request code=%s", remoteErrorCode(code));
        remoteStatus(message, !retryable);
    }
    cJSON_Delete(json);
}

static void remoteWsEvent(void *, esp_event_base_t, int32_t eventId, void *eventData)
{
    if (eventId == WEBSOCKET_EVENT_CONNECTED) {
        ESP_LOGI(TAG, "remote WebSocket connected");
        portENTER_CRITICAL(&s_remoteMux);
        s_remote.connected = true;
        s_remote.joined = false;
        s_remote.needsJoin = !s_remote.terminal;
        if (!s_remote.terminal) s_remote.status[0] = '\0';
        s_remote.revision++;
        portEXIT_CRITICAL(&s_remoteMux);
    } else if (eventId == WEBSOCKET_EVENT_DISCONNECTED) {
        ESP_LOGW(TAG, "remote WebSocket disconnected");
        portENTER_CRITICAL(&s_remoteMux);
        s_remote.connected = false;
        s_remote.joined = false;
        s_remote.revision++;
        portEXIT_CRITICAL(&s_remoteMux);
    } else if (eventId == WEBSOCKET_EVENT_ERROR) {
        const auto *event = (const esp_websocket_event_data_t *)eventData;
        if (event) {
            ESP_LOGE(TAG, "remote WebSocket error type=%d handshake=%d errno=%d tls=%s",
                     event->error_handle.error_type,
                     event->error_handle.esp_ws_handshake_status_code,
                     event->error_handle.esp_transport_sock_errno,
                     esp_err_to_name(event->error_handle.esp_tls_last_esp_err));
        } else {
            ESP_LOGE(TAG, "remote WebSocket error without details");
        }
        remoteStatus(tr(Str::MULTI_REMOTE_DISCONNECTED));
    } else if (eventId == WEBSOCKET_EVENT_DATA) {
        const auto *event = (const esp_websocket_event_data_t *)eventData;
        if (!event || !event->data_ptr || event->op_code == 0x2 ||
            event->payload_len <= 0 || (size_t)event->payload_len > REMOTE_FRAME_CAP ||
            event->data_len < 0 || event->payload_offset < 0) {
            s_wsFrameUsed = s_wsFrameExpected = 0;
            return;
        }
        if (event->payload_offset == 0) {
            if (event->op_code != 0x1) {
                s_wsFrameUsed = s_wsFrameExpected = 0;
                return;
            }
            s_wsFrameUsed = 0;
            s_wsFrameExpected = (size_t)event->payload_len;
        } else if (event->op_code != 0x0 && event->op_code != 0x1) {
            return;
        }
        if (s_wsFrameExpected != (size_t)event->payload_len ||
            (size_t)event->payload_offset != s_wsFrameUsed ||
            (size_t)event->data_len > s_wsFrameExpected - s_wsFrameUsed) {
            s_wsFrameUsed = s_wsFrameExpected = 0;
            return;
        }
        memcpy(s_wsFrame + s_wsFrameUsed, event->data_ptr, (size_t)event->data_len);
        s_wsFrameUsed += (size_t)event->data_len;
        if (s_wsFrameUsed == s_wsFrameExpected) {
            s_wsFrame[s_wsFrameUsed] = '\0';
            handleRemoteMessage(s_wsFrame);
            s_wsFrameUsed = s_wsFrameExpected = 0;
        }
    }
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
    const cJSON *session = cJSON_GetObjectItemCaseSensitive(root, "session_id");
    const bool activeGame = cJSON_IsTrue(active);
    const bool yatzy = cJSON_IsString(game) && strcmp(game->valuestring, "yatzy") == 0;
    const bool isFinished = cJSON_IsTrue(finished);
    if (cJSON_IsString(session) && MultiplayerProtocol::validSessionId(session->valuestring))
        strlcpy(host.sessionId, session->valuestring, sizeof(host.sessionId));
    if (activeGame && yatzy && !isFinished && cJSON_IsArray(players)) {
        const cJSON *seat;
        cJSON_ArrayForEach(seat, players) {
            const cJSON *web = cJSON_GetObjectItemCaseSensitive(seat, "web");
            const cJSON *pin = cJSON_GetObjectItemCaseSensitive(seat, "has_pin");
            const cJSON *connected = cJSON_GetObjectItemCaseSensitive(seat, "connected");
            const cJSON *name = cJSON_GetObjectItemCaseSensitive(seat, "name");
            const cJSON *player = cJSON_GetObjectItemCaseSensitive(seat, "player");
            if (!MultiplayerProtocol::joinableSeat(cJSON_IsTrue(web), cJSON_IsTrue(pin),
                                                    cJSON_IsTrue(connected)) ||
                !cJSON_IsNumber(player) || player->valueint < 0 ||
                player->valueint >= YATZY_MAX_PLAYERS ||
                !cJSON_IsString(name) || host.seatCount >= MAX_SEATS) continue;
            Host::Seat &target = host.seats[host.seatCount++];
            target.player = (uint8_t)player->valueint;
            strlcpy(target.name, name->valuestring, sizeof(target.name));
        }
    }
    cJSON_Delete(root);
    return MultiplayerProtocol::joinableGame(activeGame, yatzy, isFinished, host.seatCount) &&
           MultiplayerProtocol::validSessionId(host.sessionId);
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

        for (int seat = 0; seat < host.seatCount; seat++) {
            char seatText[96];
            snprintf(seatText, sizeof(seatText), tr(Str::MULTI_JOIN_SEAT_FMT),
                     host.seats[seat].name);
            lv_obj_t *button = Theme::button(card, seatText, lv_obj_get_content_width(card),
                                             UiMetrics::buttonH(), Theme::TEAL);
            if (!button) continue;
            lv_obj_set_width(button, LV_PCT(100));
            lv_obj_add_event_cb(button, selectSeatEvent, LV_EVENT_CLICKED,
                                (void *)(intptr_t)(i * MAX_SEATS + seat));
        }
    }
}

static void pollScan(lv_timer_t *)
{
    renderResults();
}

static void remoteHoldEvent(lv_event_t *event)
{
    sendRemoteCommand("hold", (int)(intptr_t)lv_event_get_user_data(event));
}

static void remoteChooseEvent(lv_event_t *event)
{
    const int categoryId = (int)(intptr_t)lv_event_get_user_data(event);
    const uint32_t now = lv_tick_get();
    if (s_remoteLastChoiceId == categoryId &&
        lv_tick_elaps(s_remoteLastChoiceMs) <= REMOTE_CHOOSE_DOUBLE_TAP_MS) {
        s_remoteLastChoiceId = -1;
        s_remoteLastChoiceMs = 0;
        sendRemoteCommand("choose", categoryId);
    } else {
        s_remoteLastChoiceId = categoryId;
        s_remoteLastChoiceMs = now;
    }
}

static void remoteScoreToggleEvent(lv_event_t *)
{
    s_remoteShowingScorecard = !s_remoteShowingScorecard;
    lv_obj_add_flag(s_remotePlayPanel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_remoteScorePanel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_remoteShowingScorecard ? s_remoteScorePanel : s_remotePlayPanel,
                      LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(lv_obj_get_child(s_remoteScoreButton, 0),
                      tr(s_remoteShowingScorecard ? Str::MULTI_REMOTE_PLAY :
                                                   Str::MULTI_REMOTE_SCORECARD));
}

static void stopRemoteClient()
{
    if (s_remoteDiceTimer) {
        lv_timer_delete(s_remoteDiceTimer);
        s_remoteDiceTimer = nullptr;
    }
    esp_websocket_client_handle_t client;
    portENTER_CRITICAL(&s_remoteMux);
    client = s_remoteClient;
    s_remoteClient = nullptr;
    portEXIT_CRITICAL(&s_remoteMux);
    if (client) {
        esp_websocket_client_stop(client);
        esp_websocket_client_destroy(client);
    }
    portENTER_CRITICAL(&s_remoteMux);
    memset(&s_remote, 0, sizeof(s_remote));
    memset(s_remotePin, 0, sizeof(s_remotePin));
    memset(s_remoteSession, 0, sizeof(s_remoteSession));
    memset(s_remoteUri, 0, sizeof(s_remoteUri));
    memset(s_wsFrame, 0, sizeof(s_wsFrame));
    s_wsFrameUsed = s_wsFrameExpected = 0;
    memset(&s_remoteHost, 0, sizeof(s_remoteHost));
    s_remoteSeat = -1;
    portEXIT_CRITICAL(&s_remoteMux);
}

static void leaveRemoteEvent(lv_event_t *)
{
    if (s_timer) {
        lv_timer_delete(s_timer);
        s_timer = nullptr;
    }
    stopRemoteClient();
    show();
}

static void placeRemoteCup()
{
    const int32_t diameter = lv_obj_get_width(s_remoteCupRim);
    const int32_t lipDiameter = lv_obj_get_width(s_remoteCupLip);
    const int32_t bottomDiameter = lv_obj_get_width(s_remoteCupBottom);
    lv_obj_set_pos(s_remoteCupRim, s_remoteCupBaseX + (int32_t)s_remoteCupOffX,
                   s_remoteCupBaseY + (int32_t)s_remoteCupOffY);
    lv_obj_set_pos(s_remoteCupLip, (diameter - lipDiameter) / 2 -
                   (int32_t)s_remoteCupOffX / 6,
                   (diameter - lipDiameter) / 2 - (int32_t)s_remoteCupOffY / 6);
    lv_obj_set_pos(s_remoteCupBottom, (lipDiameter - bottomDiameter) / 2 -
                   (int32_t)s_remoteCupOffX / 3,
                   (lipDiameter - bottomDiameter) / 2 -
                   (int32_t)s_remoteCupOffY / 3);
    for (int body = 0; body < s_remoteCupBodyCount; body++) {
        lv_obj_t *die = s_remoteCupDie[body];
        if (!die) continue;
        const int32_t size = lv_obj_get_width(die);
        const DiceCup::Body &physics = s_remoteCupBodies[body];
        lv_obj_set_pos(die, bottomDiameter / 2 + (int32_t)physics.x - size / 2,
                       bottomDiameter / 2 + (int32_t)physics.y - size / 2);
        lv_image_set_rotation(die, (int32_t)(physics.ang * 10.0f));
    }
}

static void remoteCupTick(lv_timer_t *timer)
{
    if (!s_remoteCupActive) {
        if (s_remoteCupTimer == timer) s_remoteCupTimer = nullptr;
        lv_timer_delete(timer);
        return;
    }
    const uint32_t now = lv_tick_get();
    float dt = (float)(now - s_remoteCupLastMs) / 1000.0f;
    s_remoteCupLastMs = now;
    if (dt <= 0.0f) return;
    if (dt > 0.05f) dt = 0.05f;

    float targetX = 0.0f;
    float targetY = 0.0f;
    if (s_remoteCupPressed) {
        targetX = (s_remoteCupShake.lastX() - s_remoteCupCenterX) * 0.35f;
        targetY = (s_remoteCupShake.lastY() - s_remoteCupCenterY) * 0.35f;
        targetX = targetX < -18.0f ? -18.0f : (targetX > 18.0f ? 18.0f : targetX);
        targetY = targetY < -18.0f ? -18.0f : (targetY > 18.0f ? 18.0f : targetY);
    }
    const float k = dt * 16.0f > 1.0f ? 1.0f : dt * 16.0f;
    const float oldX = s_remoteCupOffX;
    const float oldY = s_remoteCupOffY;
    const float nextX = s_remoteCupOffX + (targetX - s_remoteCupOffX) * k;
    const float nextY = s_remoteCupOffY + (targetY - s_remoteCupOffY) * k;
    const float vx = (nextX - oldX) / dt;
    const float vy = (nextY - oldY) / dt;
    const float dvx = vx - s_remoteCupVx;
    const float dvy = vy - s_remoteCupVy;
    s_remoteCupOffX = nextX;
    s_remoteCupOffY = nextY;
    s_remoteCupVx = vx;
    s_remoteCupVy = vy;
    DiceCup::step(s_remoteCupBodies, s_remoteCupBodyCount, dt,
                  -dvx * 1.6f, -dvy * 1.6f, s_remoteCupParams);
    placeRemoteCup();
}

static void remoteCupEvent(lv_event_t *event)
{
    if (!s_remoteCupActive || !s_remoteCupRim) return;
    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;
    lv_point_t point;
    lv_indev_get_point(indev, &point);
    const uint32_t now = lv_tick_get();
    const lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_PRESSED) {
        lv_area_t coords;
        lv_obj_get_coords(s_remoteCupRim, &coords);
        const float cx = ((float)coords.x1 + (float)coords.x2) / 2.0f;
        const float cy = ((float)coords.y1 + (float)coords.y2) / 2.0f;
        const float dx = (float)point.x - cx;
        const float dy = (float)point.y - cy;
        const int32_t radius = lv_obj_get_width(s_remoteCupRim) / 2 + 14;
        if (dx * dx + dy * dy > (float)radius * (float)radius) return;
        RemoteGame state;
        portENTER_CRITICAL(&s_remoteMux);
        state = s_remote;
        portEXIT_CRITICAL(&s_remoteMux);
        MultiplayerProtocol::RemoteActions actions = {
            state.joined, state.active, state.finished, state.rolling, state.hasRolled,
            state.canRoll, state.you, state.current
        };
        if (!MultiplayerProtocol::canRemoteRoll(actions)) return;
        s_remoteCupPressed = true;
        s_remoteCupShake.begin((float)point.x, (float)point.y, now);
    } else if (code == LV_EVENT_PRESSING) {
        if (s_remoteCupPressed)
            s_remoteCupShake.move((float)point.x, (float)point.y, now);
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        if (!s_remoteCupPressed) return;
        s_remoteCupPressed = false;
        lv_area_t coords;
        lv_obj_get_coords(s_remoteCupRim, &coords);
        const DiceCup::ReleaseInfo release =
            DiceCup::decideRelease(s_remoteCupShake, s_remoteCupShake.lastY(),
                                   (float)coords.y1);
        s_remoteCupShake.end();
        if (release.kind != DiceCup::Release::NONE) {
            sendRemoteCommand("roll");
            closeRemoteCup();
        }
    }
}

static void remoteOpenCupEvent(lv_event_t *)
{
    RemoteGame state;
    portENTER_CRITICAL(&s_remoteMux);
    state = s_remote;
    portEXIT_CRITICAL(&s_remoteMux);
    MultiplayerProtocol::RemoteActions actions = {
        state.joined, state.active, state.finished, state.rolling, state.hasRolled,
        state.canRoll, state.you, state.current
    };
    if (!MultiplayerProtocol::canRemoteRoll(actions)) return;
    s_remoteCupActive = true;
    lv_obj_set_hidden(s_remoteCupArea, false);
    lv_obj_set_hidden(s_remoteRoll, true);
    lv_obj_set_hidden(s_remoteCategories, true);
    lv_obj_set_hidden(s_remoteCupChoose, !state.hasRolled);
    lv_label_set_text_fmt(s_remoteCupCount, tr(Str::GAME_ROLLS_LEFT), state.rollsLeft);
    renderRemoteDice(state);
    s_remoteCupLastMs = lv_tick_get();
    if (s_remoteCupTimer) lv_timer_delete(s_remoteCupTimer);
    s_remoteCupTimer = lv_timer_create(remoteCupTick, 16, nullptr);
}

static void remoteCupRollEvent(lv_event_t *)
{
    RemoteGame state;
    portENTER_CRITICAL(&s_remoteMux);
    state = s_remote;
    portEXIT_CRITICAL(&s_remoteMux);
    MultiplayerProtocol::RemoteActions actions = {
        state.joined, state.active, state.finished, state.rolling, state.hasRolled,
        state.canRoll, state.you, state.current
    };
    if (MultiplayerProtocol::canRemoteRoll(actions)) sendRemoteCommand("roll");
    closeRemoteCup();
}

static void remoteCupCloseEvent(lv_event_t *)
{
    closeRemoteCup();
}

static void closeRemoteCup()
{
    if (!s_remoteCupActive) return;
    s_remoteCupActive = false;
    s_remoteCupPressed = false;
    s_remoteCupShake.end();
    if (s_remoteCupTimer) {
        lv_timer_delete(s_remoteCupTimer);
        s_remoteCupTimer = nullptr;
    }
    s_remoteCupOffX = s_remoteCupOffY = s_remoteCupVx = s_remoteCupVy = 0.0f;
    placeRemoteCup();
    lv_obj_set_hidden(s_remoteCupArea, true);
    lv_obj_set_hidden(s_remoteRoll, false);
    lv_obj_set_hidden(s_remoteCategories, false);
    RemoteGame state;
    portENTER_CRITICAL(&s_remoteMux);
    state = s_remote;
    portEXIT_CRITICAL(&s_remoteMux);
    renderRemoteDice(state);
}

static void renderRemoteScorecard(const RemoteGame &state)
{
    const int rowCount = state.categoryCount + 4;
    lv_table_set_row_count(s_remoteScoreTable, rowCount);
    lv_table_set_column_count(s_remoteScoreTable, state.playerCount + 1);
    const int32_t width = lv_obj_get_content_width(s_remoteScorePanel);
    const int32_t categoryWidth = LV_MIN(112, LV_MAX(78, width / 3));
    const int32_t playerWidth = LV_MAX(48, (width - categoryWidth) /
                                           LV_MAX(1, state.playerCount));
    lv_table_set_column_width(s_remoteScoreTable, 0, categoryWidth);
    for (int player = 0; player < state.playerCount; player++)
        lv_table_set_column_width(s_remoteScoreTable, player + 1, playerWidth);

    lv_table_set_cell_value(s_remoteScoreTable, 0, 0, "");
    lv_table_set_cell_ctrl(s_remoteScoreTable, 0, 0, LV_TABLE_CELL_CTRL_TEXT_CROP);
    for (int player = 0; player < state.playerCount; player++) {
        lv_table_set_cell_value(s_remoteScoreTable, 0, player + 1, state.players[player].name);
        lv_table_set_cell_ctrl(s_remoteScoreTable, 0, player + 1, LV_TABLE_CELL_CTRL_TEXT_CROP);
    }
    for (int categoryIndex = 0; categoryIndex < state.categoryCount; categoryIndex++) {
        const RemoteCategory &category = state.categories[categoryIndex];
        const int row = categoryIndex + 1;
        lv_table_set_cell_value(s_remoteScoreTable, row, 0, category.name);
        for (int player = 0; player < state.playerCount; player++) {
            char value[12] = {};
            if (category.scoreSet[player]) {
                snprintf(value, sizeof(value), "%d", category.playerScores[player]);
            } else if (player == state.current && category.available) {
                snprintf(value, sizeof(value), "%d", category.potential);
            }
            lv_table_set_cell_value(s_remoteScoreTable, row, player + 1, value);
        }
    }

    const int sumRow = state.categoryCount + 1;
    const int bonusRow = state.categoryCount + 2;
    const int totalRow = state.categoryCount + 3;
    lv_table_set_cell_value(s_remoteScoreTable, sumRow, 0, tr(Str::GAME_SUM));
    lv_table_set_cell_value(s_remoteScoreTable, bonusRow, 0, tr(Str::GAME_BONUS));
    lv_table_set_cell_value(s_remoteScoreTable, totalRow, 0, tr(Str::GAME_TOTAL));
    for (int player = 0; player < state.playerCount; player++) {
        char value[16];
        snprintf(value, sizeof(value), "%d", state.players[player].upper);
        lv_table_set_cell_value(s_remoteScoreTable, sumRow, player + 1,
                                state.players[player].hasUpper ? value : "");
        snprintf(value, sizeof(value), "%d", state.players[player].bonus);
        lv_table_set_cell_value(s_remoteScoreTable, bonusRow, player + 1,
                                state.players[player].hasBonus ? value : "");
        snprintf(value, sizeof(value), "%d", state.players[player].total);
        lv_table_set_cell_value(s_remoteScoreTable, totalRow, player + 1, value);
    }
    lv_obj_invalidate(s_remoteScoreTable);
}

static void remoteScoreTableDrawEvent(lv_event_t *event)
{
    lv_draw_task_t *task = lv_event_get_draw_task(event);
    lv_draw_dsc_base_t *base = task ? (lv_draw_dsc_base_t *)lv_draw_task_get_draw_dsc(task) : nullptr;
    if (!base || base->part != LV_PART_ITEMS) return;
    lv_draw_fill_dsc_t *fill = lv_draw_task_get_fill_dsc(task);
    lv_draw_label_dsc_t *text = lv_draw_task_get_label_dsc(task);
    if (!fill && !text) return;

    RemoteGame state;
    portENTER_CRITICAL(&s_remoteMux);
    state = s_remote;
    portEXIT_CRITICAL(&s_remoteMux);
    const uint32_t row = base->id1;
    const uint32_t col = base->id2;
    const bool currentColumn = col > 0 && (int)col - 1 == state.current;
    uint32_t background = currentColumn ? 0x1A2C44 : Theme::PANEL;
    uint32_t foreground = col == 0 ? Theme::MUTED : Theme::TEXT;
    if (row == 0) {
        background = Theme::PANEL_RAISED;
        foreground = currentColumn ? Theme::ACCENT : Theme::MUTED;
    } else if (row >= (uint32_t)state.categoryCount + 1) {
        background = Theme::PANEL_RAISED;
        foreground = row == (uint32_t)state.categoryCount + 3 ? Theme::GOLD : Theme::TEAL;
    } else if (currentColumn && row > 0 && row <= (uint32_t)state.categoryCount) {
        const RemoteCategory &category = state.categories[row - 1];
        if (category.available && !category.scoreSet[state.current]) {
            background = 0x4D3F10;
            foreground = Theme::GOLD;
        }
    }
    if (fill) {
        fill->color = Theme::c(background);
        fill->opa = LV_OPA_COVER;
    }
    if (text) text->color = Theme::c(foreground);
}

static lv_obj_t *createRemoteDie(lv_obj_t *parent, const RemoteGame &state, int index,
                                 int32_t size, bool inCup, int faceOverride = 0)
{
    lv_obj_t *die = lv_image_create(parent);
    if (!die) return nullptr;
    const int faceValue = faceOverride >= 1 && faceOverride <= 6 ?
                          faceOverride : state.dice[index];
    const void *face = UiYatzy::faceSprite(size, faceValue);
    if (!face) {
        lv_obj_delete(die);
        return nullptr;
    }
    lv_obj_remove_style_all(die);
    lv_obj_set_size(die, size, size);
    lv_obj_set_style_radius(die, size / 5, 0);
    lv_obj_set_style_shadow_spread(die, 0, 0);
    lv_image_set_src(die, face);
    lv_image_set_pivot(die, size / 2, size / 2);
    lv_image_set_antialias(die, true);
    const bool held = state.held[index] && !inCup;
    lv_obj_set_style_border_width(die, held ? 3 : 0, 0);
    lv_obj_set_style_border_color(die, Theme::c(Theme::GOLD), 0);
    lv_obj_set_style_shadow_width(die, inCup ? 0 : (held ? 6 : (state.finished ? 10 : 16)), 0);
    lv_obj_set_style_shadow_offset_y(die, held ? 1 : 3, 0);
    lv_obj_set_style_shadow_color(die, Theme::c(held || state.finished ? 0x000000 : 0x05343A), 0);
    lv_obj_set_style_shadow_opa(die, inCup ? LV_OPA_0 :
                                (state.hasRolled && !state.rolling ?
                                 (held ? LV_OPA_60 : LV_OPA_70) : LV_OPA_0), 0);
    lv_obj_set_style_image_opa(die, inCup ? LV_OPA_COVER :
                               (state.hasRolled ? (held ? 225 : LV_OPA_COVER) : LV_OPA_30), 0);
    if (!inCup) {
        lv_obj_set_clickable(die, true);
        lv_obj_set_scrollable(die, false);
        lv_obj_set_scroll_chain(die, false);
        lv_obj_set_gesture_bubble(die, false);
        lv_obj_set_ext_click_area(die, 4);
    }
    return die;
}

static void renderRemoteDice(const RemoteGame &state)
{
    if (!s_remoteDice || !s_remoteNest) return;
    MultiplayerProtocol::RemoteActions actions = {
        state.joined, state.active, state.finished, state.rolling, state.hasRolled,
        state.canRoll, state.you, state.current
    };
    const int32_t diceAvailable = LV_MAX(1, lv_obj_get_content_width(s_remoteDice));
    const int32_t nestAvailable = LV_MAX(1, lv_obj_get_content_width(s_remoteNest));
    const int32_t gap = 2;
    const int32_t preferredSize = state.diceCount == 6 ? 46 : 52;
    const int32_t diceSize = LV_MIN(preferredSize,
                                    (diceAvailable - LV_MAX(0, state.diceCount - 1) * gap) /
                                        LV_MAX(1, state.diceCount));
    const int32_t nestDieSize = LV_MIN(preferredSize,
                                       (nestAvailable - LV_MAX(0, state.diceCount - 1) * gap) /
                                           LV_MAX(1, state.diceCount));
    lv_obj_clean(s_remoteDice);
    lv_obj_clean(s_remoteNest);
    memset(s_remoteDie, 0, sizeof(s_remoteDie));
    memset(s_remoteDieSize, 0, sizeof(s_remoteDieSize));
    for (int i = 0; i < state.diceCount; i++) {
        const bool held = state.held[i];
        if (!held && s_remoteCupActive) continue;
        lv_obj_t *row = held ? s_remoteNest : s_remoteDice;
        const int32_t dieSize = held ? nestDieSize : diceSize;
        const int animatedFace = state.rolling && state.rollMask[i] ?
                                 (int)((lv_tick_get() / 70u + (uint32_t)i * 3u) % 6u) + 1 : 0;
        lv_obj_t *die = createRemoteDie(row, state, i, dieSize, false, animatedFace);
        if (!die) continue;
        lv_obj_set_size(die, dieSize, dieSize);
        lv_image_set_rotation(die, 0);
        s_remoteDie[i] = die;
        s_remoteDieSize[i] = dieSize;
        if (MultiplayerProtocol::canRemoteHold(actions))
            lv_obj_add_event_cb(die, remoteHoldEvent, LV_EVENT_CLICKED,
                                (void *)(intptr_t)i);
        else
            lv_obj_add_state(die, LV_STATE_DISABLED);
    }

    if (!s_remoteCupDice) return;
    lv_obj_clean(s_remoteCupDice);
    memset(s_remoteCupDie, 0, sizeof(s_remoteCupDie));
    s_remoteCupBodyCount = 0;
    if (!s_remoteCupActive) return;

    int remoteDice[YATZY_MAX_DICE];
    for (int i = 0; i < state.diceCount; i++)
        if (!state.held[i]) remoteDice[s_remoteCupBodyCount++] = i;
    const int32_t cupDieSize = state.diceCount == 6 ? 46 : 52;
    const float scaledDieSize = (float)cupDieSize * 180.0f / 256.0f;
    s_remoteCupParams.wallR = (float)lv_obj_get_width(s_remoteCupLip) / 2.0f -
                              scaledDieSize * 0.55f;
    s_remoteCupParams.dieR = scaledDieSize * 0.48f;
    DiceCup::layout(s_remoteCupBodies, s_remoteCupBodyCount, s_remoteCupParams, 0.73f);
    for (int i = 0; i < s_remoteCupBodyCount; i++) {
        lv_obj_t *die = createRemoteDie(s_remoteCupDice, state, remoteDice[i],
                                        cupDieSize, true);
        if (!die) continue;
        lv_image_set_scale(die, 180);
        s_remoteCupDie[i] = die;
        s_remoteCupBodies[i].vx = (float)((i % 3) - 1) * 90.0f;
        s_remoteCupBodies[i].vy = (i % 2 ? -70.0f : 80.0f);
        s_remoteCupBodies[i].av = (i % 2 ? -160.0f : 140.0f);
    }
    placeRemoteCup();
}

static void remoteDiceAnimTick(lv_timer_t *)
{
    RemoteGame state;
    portENTER_CRITICAL(&s_remoteMux);
    state = s_remote;
    portEXIT_CRITICAL(&s_remoteMux);
    if (!state.rolling) return;

    const uint32_t tick = lv_tick_get();
    for (int i = 0; i < state.diceCount; i++) {
        if (!state.rollMask[i] || !s_remoteDie[i]) continue;
        const int face = (int)((tick / 70u + (uint32_t)i * 3u) % 6u) + 1;
        const void *sprite = UiYatzy::faceSprite(s_remoteDieSize[i], face);
        if (sprite) lv_image_set_src(s_remoteDie[i], sprite);
        lv_image_set_rotation(s_remoteDie[i],
                              (int32_t)((tick * 7u + (uint32_t)i * 900u) % 3600u));
    }
}

static void renderRemote()
{
    if (!s_remoteStatus || !s_remoteRound || !s_remoteDice || !s_remoteNest ||
        !s_remoteCategories || !s_remoteScoreTable || !s_remoteRoll) return;
    RemoteGame state;
    portENTER_CRITICAL(&s_remoteMux);
    state = s_remote;
    portEXIT_CRITICAL(&s_remoteMux);
    if (s_remoteRenderedRevision == state.revision) return;
    s_remoteRenderedRevision = state.revision;

    char status[128];
    if (state.terminal && state.status[0]) {
        strlcpy(status, state.status, sizeof(status));
    } else if (!state.connected) {
        strlcpy(status, tr(Str::MULTI_REMOTE_DISCONNECTED), sizeof(status));
    } else if (!state.joined) {
        strlcpy(status, state.status[0] ? state.status : tr(Str::MULTI_REMOTE_CONNECTING),
                sizeof(status));
    } else if (state.finished) {
        strlcpy(status, tr(Str::MULTI_REMOTE_FINISHED), sizeof(status));
    } else if (state.you == state.current && state.active) {
        snprintf(status, sizeof(status), tr(Str::MULTI_REMOTE_TURN_FMT),
                 state.you >= 0 && state.you < state.playerCount ? state.players[state.you].name : "");
    } else {
        snprintf(status, sizeof(status), tr(Str::MULTI_REMOTE_WAIT_FMT),
                 state.current >= 0 && state.current < state.playerCount ? state.players[state.current].name : "");
    }
    lv_label_set_text(s_remoteStatus, status);

    char round[96];
    snprintf(round, sizeof(round), tr(Str::MULTI_REMOTE_ROUND_FMT),
             state.gameName[0] ? state.gameName : s_remoteHost.name,
             state.round, state.rounds, state.rollsLeft);
    lv_label_set_text(s_remoteRound, round);

    MultiplayerProtocol::RemoteActions actions = {
        state.joined, state.active, state.finished, state.rolling, state.hasRolled,
        state.canRoll, state.you, state.current
    };
    if (s_remoteCupActive && !MultiplayerProtocol::canRemoteRoll(actions))
        closeRemoteCup();
    else if (s_remoteCupActive) {
        lv_obj_set_hidden(s_remoteCupChoose, !state.hasRolled);
        lv_label_set_text_fmt(s_remoteCupCount, tr(Str::GAME_ROLLS_LEFT), state.rollsLeft);
    }
    renderRemoteDice(state);

    if (MultiplayerProtocol::canRemoteRoll(actions))
        lv_obj_clear_state(s_remoteRoll, LV_STATE_DISABLED);
    else
        lv_obj_add_state(s_remoteRoll, LV_STATE_DISABLED);

    lv_obj_clean(s_remoteCategories);
    if (MultiplayerProtocol::canRemoteChoose(actions, true)) {
        lv_obj_t *hint = Theme::label(s_remoteCategories, tr(Str::MULTI_REMOTE_CHOOSE_HINT),
                                      &my_font_14, Theme::MUTED);
        if (hint) lv_obj_set_width(hint, LV_PCT(100));
    }
    for (int i = 0; i < state.categoryCount; i++) {
        const RemoteCategory &category = state.categories[i];
        if (!category.available || !MultiplayerProtocol::canRemoteChoose(actions, true)) continue;
        char text[96];
        snprintf(text, sizeof(text), "%s   %d", category.name, category.potential);
        lv_obj_t *button = Theme::button(s_remoteCategories, text,
                                         lv_obj_get_content_width(s_remoteCategories),
                                         UiMetrics::buttonH(), Theme::TEAL);
        if (!button) continue;
        lv_obj_set_width(button, LV_PCT(100));
        lv_obj_add_event_cb(button, remoteChooseEvent, LV_EVENT_CLICKED,
                            (void *)(intptr_t)category.id);
    }
    renderRemoteScorecard(state);
}

static void pollRemote(lv_timer_t *)
{
    sendRemoteJoin();
    renderRemote();
}

static void showRemoteGame()
{
    if (s_timer) {
        lv_timer_delete(s_timer);
        s_timer = nullptr;
    }
    s_remoteRenderedRevision = UINT32_MAX;
    s_remoteShowingScorecard = false;
    s_remoteCupActive = false;
    s_remoteCupPressed = false;
    s_remoteLastChoiceId = -1;
    s_remoteLastChoiceMs = 0;
    ESP_LOGI(TAG, "opening remote Yatzy view");
    lv_obj_t *screen = Theme::screen();
    Theme::header(screen, tr(Str::MULTI_REMOTE_TITLE));

    const int32_t margin = UiMetrics::marginNormal();
    const int32_t width = UiMetrics::screenW() - 2 * margin;
    const int32_t top = UiMetrics::headerH();
    const int32_t line = lv_font_get_line_height(&my_font_14);
    s_remoteStatus = Theme::label(screen, tr(Str::MULTI_REMOTE_CONNECTING), &my_font_14, Theme::GOLD);
    lv_obj_set_style_bg_color(s_remoteStatus, Theme::c(Theme::PANEL_RAISED), 0);
    lv_obj_set_style_bg_opa(s_remoteStatus, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_remoteStatus, 10, 0);
    lv_obj_set_style_pad_hor(s_remoteStatus, margin, 0);
    lv_obj_set_style_pad_ver(s_remoteStatus, UiMetrics::marginSmall(), 0);
    lv_label_set_long_mode(s_remoteStatus, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_pos(s_remoteStatus, margin, top + 2);
    lv_obj_set_size(s_remoteStatus, width, line + 2 * UiMetrics::marginSmall());

    s_remoteRound = Theme::label(screen, "", &my_font_14, Theme::MUTED);
    lv_obj_set_pos(s_remoteRound, margin, top + line + 2 * UiMetrics::marginSmall() + 8);
    lv_obj_set_width(s_remoteRound, width);

    const int32_t buttonH = UiMetrics::buttonH();
    const int32_t footerY = UiMetrics::screenH() - UiMetrics::marginSmall() - buttonH;
    const int32_t bodyTop = top + 2 * line + 3 * UiMetrics::marginSmall() + 18;
    const int32_t bodyHeight = LV_MAX(buttonH * 3, footerY - bodyTop - UiMetrics::marginSmall());

    s_remotePlayPanel = lv_obj_create(screen);
    lv_obj_remove_style_all(s_remotePlayPanel);
    lv_obj_set_pos(s_remotePlayPanel, margin, bodyTop);
    lv_obj_set_size(s_remotePlayPanel, width, bodyHeight);
    lv_obj_set_scrollable(s_remotePlayPanel, false);

    const int32_t feltH = LV_MIN(150, LV_MAX(118, bodyHeight / 3));
    lv_obj_t *felt = lv_obj_create(s_remotePlayPanel);
    lv_obj_remove_style_all(felt);
    lv_obj_set_pos(felt, 0, 0);
    lv_obj_set_size(felt, width, feltH);
    lv_obj_set_style_radius(felt, 14, 0);
    lv_obj_set_style_bg_color(felt, Theme::c(0x123A3F), 0);
    lv_obj_set_style_bg_grad_color(felt, Theme::c(0x0E2730), 0);
    lv_obj_set_style_bg_grad_dir(felt, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(felt, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(felt, 1, 0);
    lv_obj_set_style_border_color(felt, Theme::c(0x416267), 0);
    lv_obj_set_style_pad_hor(felt, 4, 0);
    lv_obj_set_style_pad_ver(felt, UiMetrics::marginSmall(), 0);

    s_remoteDice = lv_obj_create(felt);
    lv_obj_remove_style_all(s_remoteDice);
    const int32_t nestHeight = 60;
    lv_obj_set_pos(s_remoteDice, 0, 0);
    lv_obj_set_size(s_remoteDice, LV_PCT(100), feltH - nestHeight - 4);
    lv_obj_set_flex_flow(s_remoteDice, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_remoteDice, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(s_remoteDice, 2, 0);
    lv_obj_set_scrollable(s_remoteDice, false);

    s_remoteNest = lv_obj_create(felt);
    lv_obj_remove_style_all(s_remoteNest);
    lv_obj_set_pos(s_remoteNest, 4, feltH - nestHeight - 4);
    lv_obj_set_size(s_remoteNest, width - 8, nestHeight);
    lv_obj_set_style_radius(s_remoteNest, 12, 0);
    lv_obj_set_style_bg_color(s_remoteNest, Theme::c(0x000000), 0);
    lv_obj_set_style_bg_opa(s_remoteNest, LV_OPA_30, 0);
    lv_obj_set_style_border_width(s_remoteNest, 1, 0);
    lv_obj_set_style_border_color(s_remoteNest, Theme::c(0x6B5418), 0);
    lv_obj_set_style_border_opa(s_remoteNest, LV_OPA_70, 0);
    lv_obj_set_flex_flow(s_remoteNest, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_remoteNest, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(s_remoteNest, 2, 0);
    lv_obj_set_scrollable(s_remoteNest, false);

    const int32_t rollY = feltH + UiMetrics::marginSmall();
    s_remoteRoll = Theme::button(s_remotePlayPanel, tr(Str::GAME_ROLL), width, buttonH,
                                 Theme::ACCENT, true);
    lv_obj_set_pos(s_remoteRoll, 0, rollY);
    lv_obj_add_state(s_remoteRoll, LV_STATE_DISABLED);
    lv_obj_add_event_cb(s_remoteRoll, remoteOpenCupEvent, LV_EVENT_CLICKED, nullptr);

    const int32_t categoryY = rollY + buttonH + UiMetrics::marginSmall();
    s_remoteCategories = lv_obj_create(s_remotePlayPanel);
    lv_obj_remove_style_all(s_remoteCategories);
    lv_obj_set_pos(s_remoteCategories, 0, categoryY);
    lv_obj_set_size(s_remoteCategories, width, bodyHeight - categoryY);
    lv_obj_set_flex_flow(s_remoteCategories, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_remoteCategories, UiMetrics::marginSmall(), 0);
    lv_obj_set_scroll_dir(s_remoteCategories, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_remoteCategories, LV_SCROLLBAR_MODE_AUTO);

    const int32_t cupAreaHeight = bodyHeight - rollY;
    s_remoteCupArea = lv_obj_create(s_remotePlayPanel);
    lv_obj_remove_style_all(s_remoteCupArea);
    lv_obj_set_pos(s_remoteCupArea, 0, rollY);
    lv_obj_set_size(s_remoteCupArea, width, cupAreaHeight);
    lv_obj_set_scrollable(s_remoteCupArea, false);
    lv_obj_set_scroll_chain(s_remoteCupArea, false);
    lv_obj_set_gesture_bubble(s_remoteCupArea, false);
    lv_obj_set_clickable(s_remoteCupArea, true);
    lv_obj_add_event_cb(s_remoteCupArea, remoteCupEvent, LV_EVENT_PRESSED, nullptr);
    lv_obj_add_event_cb(s_remoteCupArea, remoteCupEvent, LV_EVENT_PRESSING, nullptr);
    lv_obj_add_event_cb(s_remoteCupArea, remoteCupEvent, LV_EVENT_RELEASED, nullptr);
    lv_obj_add_event_cb(s_remoteCupArea, remoteCupEvent, LV_EVENT_PRESS_LOST, nullptr);

    const int32_t cupDiameter = LV_MAX(84, LV_MIN(146, cupAreaHeight - 56));
    const UiYatzy::CupVisual cup = UiYatzy::createCupVisual(s_remoteCupArea, cupDiameter);
    s_remoteCupRim = cup.rim;
    s_remoteCupLip = cup.lip;
    s_remoteCupBottom = cup.bottom;
    s_remoteCupBaseX = (width - cupDiameter) / 2;
    s_remoteCupBaseY = (cupAreaHeight - cupDiameter) / 2;
    lv_obj_set_pos(s_remoteCupRim, s_remoteCupBaseX, s_remoteCupBaseY);
    lv_area_t cupCoords;
    lv_obj_get_coords(s_remoteCupRim, &cupCoords);
    s_remoteCupCenterX = ((float)cupCoords.x1 + (float)cupCoords.x2) / 2.0f;
    s_remoteCupCenterY = ((float)cupCoords.y1 + (float)cupCoords.y2) / 2.0f;
    s_remoteCupDice = lv_obj_create(s_remoteCupBottom);
    lv_obj_remove_style_all(s_remoteCupDice);
    lv_obj_set_size(s_remoteCupDice, LV_PCT(100), LV_PCT(100));
    lv_obj_set_clickable(s_remoteCupDice, false);
    lv_obj_set_scrollable(s_remoteCupDice, false);

    s_remoteCupCount = Theme::label(s_remoteCupArea, "", &my_font_18, Theme::GOLD);
    lv_obj_set_width(s_remoteCupCount, 80);
    lv_obj_set_pos(s_remoteCupCount, 6, 8);
    s_remoteCupHint = Theme::label(s_remoteCupArea, tr(Str::CUP_HINT),
                                   &my_font_14, Theme::MUTED);
    lv_obj_set_width(s_remoteCupHint, 80);
    lv_obj_set_pos(s_remoteCupHint, 6, 56);
    lv_obj_t *cupRoll = Theme::button(s_remoteCupArea,
                                      trp(LV_SYMBOL_REFRESH " ", Str::GAME_ROLL),
                                      78, 38, Theme::ACCENT, true);
    lv_obj_set_pos(cupRoll, width - 82, 8);
    lv_obj_set_style_text_font(lv_obj_get_child(cupRoll, 0), &my_font_14, 0);
    lv_obj_add_event_cb(cupRoll, remoteCupRollEvent, LV_EVENT_CLICKED, nullptr);
    s_remoteCupChoose = Theme::button(s_remoteCupArea,
                                      trp("", Str::GAME_CHOOSE, " " LV_SYMBOL_RIGHT),
                                      78, 38, Theme::TEAL, false);
    lv_obj_set_pos(s_remoteCupChoose, width - 82, cupAreaHeight - 44);
    lv_obj_set_style_text_font(lv_obj_get_child(s_remoteCupChoose, 0), &my_font_14, 0);
    lv_obj_add_event_cb(s_remoteCupChoose, remoteCupCloseEvent, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *cupHide = Theme::button(s_remoteCupArea, trp(LV_SYMBOL_DOWN " ", Str::GAME_TIDY),
                                      78, 38, Theme::MUTED, false);
    lv_obj_set_pos(cupHide, 4, cupAreaHeight - 44);
    lv_obj_set_style_text_font(lv_obj_get_child(cupHide, 0), &my_font_14, 0);
    lv_obj_add_event_cb(cupHide, remoteCupCloseEvent, LV_EVENT_CLICKED, nullptr);
    lv_obj_set_hidden(s_remoteCupArea, true);

    s_remoteScorePanel = lv_obj_create(screen);
    lv_obj_remove_style_all(s_remoteScorePanel);
    lv_obj_set_pos(s_remoteScorePanel, margin, bodyTop);
    lv_obj_set_size(s_remoteScorePanel, width, bodyHeight);
    lv_obj_set_hidden(s_remoteScorePanel, true);

    s_remoteScoreTable = lv_table_create(s_remoteScorePanel);
    lv_obj_set_size(s_remoteScoreTable, LV_PCT(100), LV_PCT(100));
    lv_obj_set_scroll_dir(s_remoteScoreTable,
                          static_cast<lv_dir_t>(LV_DIR_VER | LV_DIR_HOR));
    lv_obj_set_scrollbar_mode(s_remoteScoreTable, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_bg_color(s_remoteScoreTable, Theme::c(Theme::BG), 0);
    lv_obj_set_style_border_width(s_remoteScoreTable, 2, 0);
    lv_obj_set_style_border_side(s_remoteScoreTable, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_color(s_remoteScoreTable, Theme::c(Theme::ACCENT), 0);
    lv_obj_set_style_pad_all(s_remoteScoreTable, 0, 0);
    lv_obj_set_style_text_font(s_remoteScoreTable, &my_font_14, LV_PART_ITEMS);
    lv_obj_set_style_pad_top(s_remoteScoreTable, 4, LV_PART_ITEMS);
    lv_obj_set_style_pad_bottom(s_remoteScoreTable, 4, LV_PART_ITEMS);
    lv_obj_set_style_pad_left(s_remoteScoreTable, 5, LV_PART_ITEMS);
    lv_obj_set_style_pad_right(s_remoteScoreTable, 3, LV_PART_ITEMS);
    lv_obj_set_style_border_color(s_remoteScoreTable, Theme::c(Theme::LINE), LV_PART_ITEMS);
    lv_obj_set_style_border_width(s_remoteScoreTable, 1, LV_PART_ITEMS);
    lv_obj_set_style_border_side(s_remoteScoreTable, LV_BORDER_SIDE_BOTTOM, LV_PART_ITEMS);
    lv_obj_add_event_cb(s_remoteScoreTable, remoteScoreTableDrawEvent,
                        LV_EVENT_DRAW_TASK_ADDED, nullptr);
    lv_obj_set_send_draw_task_events(s_remoteScoreTable, true);

    const int32_t footerWidth = (width - UiMetrics::marginSmall()) / 2;
    s_remoteScoreButton = Theme::button(screen, tr(Str::MULTI_REMOTE_SCORECARD),
                                        footerWidth, buttonH, Theme::TEAL);
    lv_obj_set_pos(s_remoteScoreButton, margin, footerY);
    lv_obj_add_event_cb(s_remoteScoreButton, remoteScoreToggleEvent, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *back = Theme::button(screen, trp(LV_SYMBOL_LEFT " ", Str::BTN_BACK),
                                   footerWidth, buttonH, Theme::MUTED);
    lv_obj_set_pos(back, margin + footerWidth + UiMetrics::marginSmall(), footerY);
    lv_obj_add_event_cb(back, leaveRemoteEvent, LV_EVENT_CLICKED, nullptr);

    s_timer = lv_timer_create(pollRemote, 200, nullptr);
    s_remoteDiceTimer = lv_timer_create(remoteDiceAnimTick, 70, nullptr);
    Theme::loadScreen(screen);
    ESP_LOGI(TAG, "remote Yatzy view ready");
}

static bool remotePinDone(const char *pin, bool ok, void *)
{
    if (!ok) {
        ESP_LOGI(TAG, "remote join PIN entry cancelled");
        s_pinPad = nullptr;
        return true;
    }
    ESP_LOGI(TAG, "remote join PIN submitted");
    if (!pin || strlen(pin) != 4 || !startRemoteGame(pin)) {
        if (s_pinPad) UiNumpad::setHint(s_pinPad, tr(Str::MULTI_REMOTE_START_FAILED));
        return false;
    }
    s_pinPad = nullptr;
    return true;
}

static void selectSeatEvent(lv_event_t *event)
{
    const int key = (int)(intptr_t)lv_event_get_user_data(event);
    const int hostIndex = key / MAX_SEATS;
    const int visibleSeat = key % MAX_SEATS;
    Host host = {};
    portENTER_CRITICAL(&s_mux);
    if (hostIndex >= 0 && hostIndex < s_state.count) host = s_state.hosts[hostIndex];
    portEXIT_CRITICAL(&s_mux);
    if (hostIndex < 0 || hostIndex >= MAX_HOSTS || visibleSeat < 0 ||
        visibleSeat >= host.seatCount ||
        !MultiplayerProtocol::validSessionId(host.sessionId)) return;
    ESP_LOGI(TAG, "join seat selected ip=%s port=%u seat=%u stack-watermark=%u",
             host.ip, host.port, host.seats[visibleSeat].player,
             (unsigned)uxTaskGetStackHighWaterMark(nullptr));
    s_selectedHost = hostIndex;
    s_selectedVisibleSeat = visibleSeat;
    s_selectedHostData = host;
    char title[96];
    snprintf(title, sizeof(title), tr(Str::MULTI_PIN_TITLE_FMT),
             host.seats[visibleSeat].name);
    s_pinPad = UiNumpad::open(title, tr(Str::MULTI_PIN_INFO), remotePinDone, nullptr);
    if (s_pinPad)
        ESP_LOGI(TAG, "remote join PIN keypad opened");
    else
        ESP_LOGE(TAG, "remote join PIN keypad allocation failed");
}

static bool startRemoteGame(const char *pin)
{
    if (!pin || strlen(pin) != 4 || s_selectedHost < 0 ||
        s_selectedVisibleSeat < 0) return false;
    const Host host = s_selectedHostData;
    if (!MultiplayerProtocol::validSessionId(host.sessionId) ||
        s_selectedVisibleSeat >= host.seatCount) return false;

    ESP_LOGI(TAG, "creating remote WebSocket host=%s:%u seat=%u stack-watermark=%u",
             host.ip, host.port, host.seats[s_selectedVisibleSeat].player,
             (unsigned)uxTaskGetStackHighWaterMark(nullptr));
    snprintf(s_remoteUri, sizeof(s_remoteUri), "ws://%s:%u/ws/game", host.ip, host.port);
    esp_websocket_client_config_t config = {};
    config.uri = s_remoteUri;
    config.reconnect_timeout_ms = 5000;
    config.network_timeout_ms = 5000;
    config.task_stack = REMOTE_WS_TASK_STACK;
    esp_websocket_client_handle_t client = esp_websocket_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "remote WebSocket client initialization failed");
        return false;
    }

    portENTER_CRITICAL(&s_remoteMux);
    memset(&s_remote, 0, sizeof(s_remote));
    s_remote.needsJoin = true;
    s_remoteClient = client;
    s_remoteHost = host;
    s_remoteSeat = host.seats[s_selectedVisibleSeat].player;
    strlcpy(s_remoteSession, host.sessionId, sizeof(s_remoteSession));
    strlcpy(s_remotePin, pin, sizeof(s_remotePin));
    portEXIT_CRITICAL(&s_remoteMux);

    const esp_err_t registerResult =
        esp_websocket_register_events(client, WEBSOCKET_EVENT_ANY, remoteWsEvent, nullptr);
    if (registerResult != ESP_OK) {
        ESP_LOGE(TAG, "remote WebSocket event registration failed: %s",
                 esp_err_to_name(registerResult));
        stopRemoteClient();
        return false;
    }
    const esp_err_t startResult = esp_websocket_client_start(client);
    if (startResult != ESP_OK) {
        ESP_LOGE(TAG, "remote WebSocket start failed: %s", esp_err_to_name(startResult));
        stopRemoteClient();
        return false;
    }
    ESP_LOGI(TAG, "remote WebSocket client started");
    showRemoteGame();
    return true;
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
