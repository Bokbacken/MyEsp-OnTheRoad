#pragma once
#include <stdint.h>

namespace MyOta {
struct BoardIdentity {
    uint32_t magic;
    char name[12];
};
constexpr uint32_t BOARD_MAGIC = 0x4252544f;
constexpr const char *boardName()
{
#if defined(CONFIG_OTR_HW_PROFILE_S3_35B)
    return "S3_35B";
#elif defined(CONFIG_OTR_HW_PROFILE_P4_43)
    return "P4_43";
#else
    return "ES3C35P";
#endif
}
}
