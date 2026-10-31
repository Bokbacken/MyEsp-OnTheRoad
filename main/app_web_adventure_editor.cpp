#include "app_web_adventure_editor.h"

#include <atomic>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "adv_validator.h"
#include "esp_heap_caps.h"
#include "my_web.h"

namespace AppWebAdventureEditor {

namespace {

constexpr size_t MAX_BODY = Adv::MAX_FILE;
constexpr size_t RECV_CHUNK = 4096;
std::atomic<bool> s_validating{false};

void *psAlloc(size_t size)
{
    void *memory = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return memory ? memory : malloc(size);
}

esp_err_t handleValidate(httpd_req_t *request)
{
    char contentType[64] = {};
    if (httpd_req_get_hdr_value_str(request, "Content-Type", contentType, sizeof(contentType)) != ESP_OK ||
        strncasecmp(contentType, "text/plain", 10) != 0 ||
        (contentType[10] && contentType[10] != ';'))
        return MyWeb::sendError(request, 415, "Valideringsbegäran måste vara text/plain");
    if (request->content_len > MAX_BODY) return MyWeb::sendError(request, 413, "Äventyrsfilen får vara högst 64 KiB");
    if (request->content_len == 0) return MyWeb::sendError(request, 400, "Ingen äventyrstext mottogs");
    MyWeb::Req query(request);
    if (!query.adminAllowed()) return MyWeb::sendError(request, 403, "Fel admin-PIN");

    bool expected = false;
    if (!s_validating.compare_exchange_strong(expected, true))
        return MyWeb::sendError(request, 503, "En äventyrsvalidering pågår redan");
    struct ValidationGuard {
        ~ValidationGuard() { s_validating.store(false); }
    } guard;

    char *source = static_cast<char *>(psAlloc(request->content_len + 1));
    if (!source) return MyWeb::sendError(request, 503, "Minnet räcker inte för äventyrsfilen");
    size_t received = 0;
    int timeouts = 0;
    while (received < request->content_len) {
        const size_t want = request->content_len - received < RECV_CHUNK
                                ? request->content_len - received
                                : RECV_CHUNK;
        const int count = httpd_req_recv(request, source + received, want);
        if (count == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts < 5) continue;
        if (count <= 0) {
            free(source);
            return MyWeb::sendError(request, 400, "Uppladdningen avbröts innan hela filen mottogs");
        }
        received += static_cast<size_t>(count);
        timeouts = 0;
    }
    source[received] = 0;

    Adv::ValidationResult result{};
    const bool ran = Adv::validateText(source, received, result);
    free(source);
    if (!ran) return MyWeb::sendError(request, 503, result.message);

    cJSON *response = cJSON_CreateObject();
    if (!response) return MyWeb::sendError(request, 503, "Minnet räcker inte för valideringssvaret");
    cJSON_AddBoolToObject(response, "ok", true);
    cJSON_AddBoolToObject(response, "valid", result.valid);
    cJSON *maps = cJSON_AddArrayToObject(response, "maps");
    if (!maps) {
        cJSON_Delete(response);
        return MyWeb::sendError(request, 503, "Minnet räcker inte för valideringssvaret");
    }
    for (int i = 0; i < result.mapCount; ++i) cJSON_AddItemToArray(maps, cJSON_CreateNumber(result.mapIds[i]));
    if (!result.valid) {
        cJSON_AddNumberToObject(response, "error_map", result.errorMapId);
        cJSON_AddStringToObject(response, "error", result.message);
    }
    return MyWeb::sendJson(request, 200, response);
}

}  // namespace

bool registerRoutes()
{
    return MyWeb::on("/api/adventure/validate", HTTP_POST, handleValidate);
}

}  // namespace AppWebAdventureEditor
