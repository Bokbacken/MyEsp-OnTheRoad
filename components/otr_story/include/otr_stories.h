// otr_stories.h - the built-in story scripts (constant tables in flash).
#pragma once

#include "otr_story.h"

namespace Otr {
namespace Story {

// "Byn" / "The Village" (world WORLD_VILLAGE): dialogue demo without Bible text. Shows variants
// (first / later visits), flags, a counter, items, a quest, a locked choice with a hint, hidden
// choices, a narrator line, a reference line, world effects (move a person, give a key) and sound.
extern const Script VILLAGE_STORY;

// "Josef" (world WORLD_JOSEPH), Bible adventure chapter 1 (Genesis 37 / 1 Mosebok 37). Two maps:
// Jakob's camp (robe puzzle) and the road to Dothan (dream riddle + cistern + sale). Bilingual
// modern prose with per-node Bible references; see otr_story_joseph.cpp.
extern const Script JOSEPH_STORY;

}  // namespace Story
}  // namespace Otr
