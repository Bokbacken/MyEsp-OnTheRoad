// hw_profile.cpp - board profile table and compile-time selection (see header).
#include "hw_profile.h"
#include "sdkconfig.h"

namespace HwProfile {

static const Info PROFILES[] = {
    {Id::ES3C35P, "ES3C35P", 320, 480, true, true, true, true, false,
     16u * 1024 * 1024, 8u * 1024 * 1024, 7u * 1024 * 1024, 512u * 1024, true, 96,
     49312, 73968},
    {Id::P4_43, "P4_43", 480, 800, true, true, true, false, true,
     32u * 1024 * 1024, 32u * 1024 * 1024, 32u * 1024 * 1024, 2u * 1024 * 1024, false, 192,
     56208, 93680},
    // Nominal active area derived from 3.5-inch diagonal and 2:3 aspect; verify on the unit.
    {Id::S3_35B, "S3_35B", 320, 480, true, true, true, false, false,
     16u * 1024 * 1024, 8u * 1024 * 1024, 7u * 1024 * 1024, 512u * 1024, true, 96,
     49312, 73968},
};

Id activeId()
{
#if defined(CONFIG_OTR_HW_PROFILE_P4_43)
    return Id::P4_43;
#elif defined(CONFIG_OTR_HW_PROFILE_S3_35B)
    return Id::S3_35B;
#else
    return Id::ES3C35P;
#endif
}

const Info &get(Id id) { return PROFILES[(int)id]; }

const Info &active() { return get(activeId()); }

}  // namespace HwProfile
