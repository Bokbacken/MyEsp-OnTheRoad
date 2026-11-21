#pragma once

#include <stdint.h>

namespace Touch {

struct Contact {
    int16_t x = 0;
    int16_t y = 0;
};

struct Contacts {
    Contact points[2];
    uint8_t count = 0;
    uint32_t sampledAt = 0;
};

}  // namespace Touch
