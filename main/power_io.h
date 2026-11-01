#pragma once

#include <stdint.h>
#include <stddef.h>

namespace PowerIo {
struct Counts {
    uint32_t sdRead, sdWrite, flashRead, flashWrite;
};
void sdRead(size_t bytes);
void sdWrite(size_t bytes);
Counts counts();
}
