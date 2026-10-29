#pragma once

#include "esp_http_server.h"

namespace HostOta {
struct VersionInfo {
    bool known;
    bool match;
    char host[16];
    char coprocessor[36];
};
void cacheVersionInfo();
const VersionInfo &versionInfo();
bool registerRoute();
}
