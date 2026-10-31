#include "adv_validator.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <new>

#if defined(ESP_PLATFORM)
#include "esp_heap_caps.h"
#endif

namespace Adv {

namespace {

void *allocPsram(size_t size)
{
#if defined(ESP_PLATFORM)
    return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    return malloc(size);
#endif
}

void setError(ValidationResult &result, int mapId, const char *message)
{
    result.valid = false;
    result.errorMapId = mapId;
    snprintf(result.message, sizeof(result.message), "%s", message ? message : "validation failed");
}

}  // namespace

bool validateText(const char *text, size_t len, ValidationResult &result)
{
    memset(&result, 0, sizeof(result));
    if (!text || len > MAX_FILE || memchr(text, 0, len)) {
        setError(result, 0, "invalid source text or file exceeds 64 KiB");
        return true;
    }

    void *loadedMemory = allocPsram(sizeof(Loaded));
    void *worldMemory = allocPsram(sizeof(Otr::World));
    if (!loadedMemory || !worldMemory) {
        free(loadedMemory);
        free(worldMemory);
        snprintf(result.message, sizeof(result.message), "not enough PSRAM to validate adventure");
        return false;
    }
    auto *loaded = new (loadedMemory) Loaded{};
    auto *world = new (worldMemory) Otr::World;

    char error[sizeof(result.message)] = {};
    if (!parse(text, len, *loaded, error, sizeof(error))) {
        setError(result, 0, error);
    } else {
        result.mapCount = loaded->internalMapCount;
        memcpy(result.mapIds, loaded->internalMapIds, sizeof(result.mapIds));
        result.valid = true;

        for (int i = 0; i < result.mapCount && result.valid; ++i) {
            const int mapId = result.mapIds[i];
            if (i > 0 && !parseMap(text, len, mapId, *loaded, error, sizeof(error))) {
                setError(result, mapId, error);
                break;
            }
            if (loaded->script.validate(error, sizeof(error))) {
                setError(result, mapId, error);
                break;
            }
            if (result.mapCount > 1 && loaded->map.rules.mineLevel) {
                setError(result, mapId, "mine_level is not supported in internal-map adventures");
                break;
            }
            if (!world->load(loaded->map)) {
                snprintf(error, sizeof(error), "engine rejected internal map %d", mapId);
                setError(result, mapId, error);
                break;
            }
            for (int n = 0; n < loaded->destinationCount; ++n) {
                const Destination &destination = loaded->destinations[n];
                if (destination.mapId == mapId && !world->safeArrivalAt(destination.x, destination.y)) {
                    snprintf(error, sizeof(error), "destination '%s' is not safe and walkable on map %d",
                             destination.name, mapId);
                    setError(result, mapId, error);
                    break;
                }
            }
            for (int n = 0; n < loaded->portalCount && result.valid; ++n) {
                const Portal &portal = loaded->portals[n];
                if (portal.mapId == mapId &&
                    (world->tile(portal.x, portal.y) == Otr::Tile::EXIT ||
                     !world->safeArrivalAt(portal.x, portal.y))) {
                    snprintf(error, sizeof(error), "portal source is not safe and walkable on map %d", mapId);
                    setError(result, mapId, error);
                }
            }
        }
    }

    world->~World();
    loaded->~Loaded();
    free(worldMemory);
    free(loadedMemory);
    return true;
}

}  // namespace Adv
