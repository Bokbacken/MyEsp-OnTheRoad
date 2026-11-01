#pragma once

#include <stdint.h>

namespace PowerHistory {
struct Status {
    bool running;
    uint32_t written, dropped;
    char path[64];
};
bool begin();
Status status();
}
