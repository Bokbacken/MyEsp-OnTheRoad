// board_pins.h - selects the GPIO map of the active board profile (Kconfig OTR_HW_PROFILE).
#pragma once

#include "sdkconfig.h"

#if defined(CONFIG_OTR_HW_PROFILE_P4_43)
#include "board_pins_p4_43.h"
#elif defined(CONFIG_OTR_HW_PROFILE_S3_35B)
#include "board_pins_s3_35b.h"
#else
#include "board_pins_es3c35p.h"
#endif
