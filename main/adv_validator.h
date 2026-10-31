#pragma once

#include <stddef.h>
#include <stdint.h>

#include "adv_loader.h"

namespace Adv {

struct ValidationResult {
    bool valid;
    int mapCount;
    int mapIds[MAX_INTERNAL_MAPS];
    int errorMapId;
    char message[192];
};

// Validates the complete source through the production parser and game engine.
// Returns false only when validation could not run (for example, insufficient memory).
bool validateText(const char *text, size_t len, ValidationResult &result);

}  // namespace Adv
