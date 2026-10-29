// hw_profile.cpp - board profile table and compile-time selection (see header).
#include "hw_profile.h"
#include "sdkconfig.h"

namespace HwProfile {

static const Info PROFILES[] = {
    {Id::ES3C35P, "ES3C35P", 320, 480, true, true, true, true, false},
    {Id::P4_43, "P4_43", 480, 800, true, true, true, false, true},
};

Id activeId()
{
#if defined(CONFIG_OTR_HW_PROFILE_P4_43)
    return Id::P4_43;
#else
    return Id::ES3C35P;
#endif
}

const Info &get(Id id) { return PROFILES[(int)id]; }

const Info &active() { return get(activeId()); }

}  // namespace HwProfile
