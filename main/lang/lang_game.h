// lang_game.h - localized names for game_yatzy / player_db values. The game core stays
// language-free (host-tested without the lang module); the UI and the web API use these.
#pragma once

#include "game_yatzy.h"
#include "lang.h"
#include "player_db.h"

static_assert((int)Str::CAT_YATZY - (int)Str::CAT_ONES == (int)YatzyCategory::YATZY,
              "CAT_* ids must follow the YatzyCategory order");

inline const char *trCategory(YatzyCategory c, YatzyVariant v)
{
    if (c == YatzyCategory::YATZY && v == YatzyVariant::MAXI) return tr(Str::CAT_MAXI_YATZY);
    if ((int)c >= YATZY_CATEGORY_COUNT) return "?";
    return tr((Str)((int)Str::CAT_ONES + (int)c));
}

inline const char *trVariant(YatzyVariant v, YatzyMode m)
{
    bool ord = m == YatzyMode::ORDERED;
    if (v == YatzyVariant::MAXI) return tr(ord ? Str::VAR_MAXI_ORDER : Str::VAR_MAXI);
    return tr(ord ? Str::VAR_YATZY_ORDER : Str::VAR_YATZY);
}

inline const char *trPlayerErr(int e)
{
    switch ((PlayerErr)e) {
    case PlayerErr::INVALID: return tr(Str::PERR_INVALID);
    case PlayerErr::DUPLICATE: return tr(Str::PERR_DUPLICATE);
    case PlayerErr::FULL: return tr(Str::PERR_FULL);
    case PlayerErr::NOT_FOUND: return tr(Str::PERR_NOT_FOUND);
    default: return "";
    }
}
