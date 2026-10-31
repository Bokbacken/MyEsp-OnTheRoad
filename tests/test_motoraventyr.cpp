#include "adv_sd_session.h"

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <queue>
#include <string>
#include <vector>

using namespace Otr;

namespace {

char err[256];
int checks;
#define CHECK(c) do { ++checks; if (!(c)) { std::fprintf(stderr, "FAIL %d: %s (%s)\n", __LINE__, #c, err); std::abort(); } } while (0)

Adv::CatalogEntry campaign(const char *folder, const char *file)
{
    Adv::CatalogEntry entry{};
    std::snprintf(entry.folder, sizeof(entry.folder), "%s", folder);
    entry.mapCount = 1;
    std::snprintf(entry.maps[0], Adv::MAP_NAME_CAP, "%s", file);
    return entry;
}

uint32_t move(Adv::SdSession &session, Adv::Loaded &loaded, World &world,
              Story::StoryState &story, const Story::Script *&script, Dir dir)
{
    const int oldX = world.playerX(), oldY = world.playerY();
    const uint32_t events = world.move(dir);
    const auto portal = session.afterAction(oldX, oldY, events, loaded, world, story, script,
                                            err, sizeof(err));
    if (portal == Adv::SdSession::PORTAL_FAILED)
        std::fprintf(stderr, "portal failed map=%d old=%d,%d now=%d,%d dir=%d err=%s\\n",
                     session.currentMapId(), oldX, oldY, world.playerX(), world.playerY(),
                     (int)dir, err);
    CHECK(portal != Adv::SdSession::PORTAL_FAILED);
    return events;
}

bool walkMineSafe(Adv::SdSession &session, Adv::Loaded &loaded, World &world,
                  Story::StoryState &story, const Story::Script *&script, int tx, int ty)
{
    const int width = world.width(), height = world.height();
    const int start = world.playerY() * width + world.playerX();
    const int target = ty * width + tx;
    const size_t cellCount = static_cast<size_t>(width) * static_cast<size_t>(height);
    std::vector<int> previous(cellCount, -1), direction(cellCount, -1);
    std::queue<int> queue;
    previous[start] = start;
    queue.push(start);
    while (!queue.empty() && previous[target] < 0) {
        const int cell = queue.front();
        queue.pop();
        const int x = cell % width, y = cell / width;
        for (int d = 0; d < 4; ++d) {
            const Dir dir = (Dir)d;
            const int nx = x + dirX(dir), ny = y + dirY(dir);
            if (!world.inside(nx, ny) || !world.playerCanEnter(nx, ny) ||
                world.rawObj(nx, ny) == Obj::MINE) continue;
            const int next = ny * width + nx;
            if (previous[next] >= 0) continue;
            previous[next] = cell;
            direction[next] = d;
            queue.push(next);
        }
    }
    if (previous[target] < 0) return false;
    std::vector<int> path;
    for (int cell = target; cell != start; cell = previous[cell]) path.push_back(direction[cell]);
    for (auto it = path.rbegin(); it != path.rend(); ++it) {
        const uint32_t events = move(session, loaded, world, story, script, (Dir)*it);
        if (!(events & EV_MOVED) && !(events & EV_EXIT)) return false;
    }
    return true;
}

void productionLoad()
{
    const char *folders[] = {
        "01_motvind", "02_stenporten", "03_fyrarnas_sang",
        "04_vindverkstaden", "05_hemresan", "06_resurskedjan"
    };
    const char *files[] = {
        "01_motvind.adv", "01_stenporten.adv", "01_fyrarnas_sang.adv",
        "01_vindverkstaden.adv", "01_hemresan.adv", "01_lamna.adv",
        "02_fyll_pa.adv", "03_nollstall.adv"
    };
    auto loaded = std::make_unique<Adv::Loaded>();
    auto world = std::make_unique<World>();
    for (int i = 0; i < 8; ++i) {
        char path[192];
        const char *folder = i < 5 ? folders[i] : folders[5];
        std::snprintf(path, sizeof(path), "sdcard_bundle/otr/adventure/motoraventyr/%s/%s",
                      folder, files[i]);
        CHECK(Adv::parseFile(path, *loaded, err, sizeof(err)));
        CHECK(loaded->script.validate(err, sizeof(err)) == 0);
        CHECK(loaded->internalMapCount >= 1 && loaded->internalMapCount <= Adv::MAX_INTERNAL_MAPS);
        for (int i = 0; i < loaded->internalMapCount; ++i) {
            CHECK(Adv::parseMapFile(path, loaded->internalMapIds[i], *loaded, err, sizeof(err)));
            CHECK(world->load(loaded->map));
            CHECK(world->width() <= Otr::MAX_W && world->height() <= Otr::MAX_H);
        }
    }
}

struct TestHost : Story::Host {
    Adv::SdSession *session = nullptr;
    Adv::Loaded *loaded = nullptr;
    World *world = nullptr;
    Story::StoryState *state = nullptr;
    const Story::Script **script = nullptr;
    char pendingDestination[32] = {};
    void worldAction(const char *, int, const int *) override {}
    bool worldActionOnMap(const char *verb, int mapId, int argc, const int *argv) override
    {
        return session->applyWorldActionOnMap(verb, mapId, argc, argv, *loaded, *world, *state,
                                              *script, err, sizeof(err));
    }
    void worldTravel(const char *destination) override
    {
        std::snprintf(pendingDestination, sizeof(pendingDestination), "%s", destination);
    }
    void sound(const char *) override {}
};

int choiceWithText(const Story::Dialog &dialog, const char *text)
{
    for (int i = 0; i < dialog.choiceCount(); ++i)
        if (!std::strcmp(dialog.choice(i).text.get(0), text)) return i;
    return -1;
}

void play()
{
    const char *root = "sdcard_bundle/otr/adventure/motoraventyr";
    auto loaded = std::make_unique<Adv::Loaded>();
    auto world = std::make_unique<World>();
    Story::StoryState story{};
    const Story::Script *script = nullptr;
    Adv::SdSession session;
    CHECK(session.start(root, campaign("01_motvind", "01_motvind.adv"), *loaded, *world,
                        story, script, err, sizeof(err)));

    CHECK(world->mineLevel() == 1 && world->safeLeft() > 0);
    int guard = 0;
    while (world->safeLeft() && guard++ < world->width() * world->height()) {
        int tx = -1, ty = -1;
        for (int y = 1; y < world->height() - 1 && ty < 0; ++y)
            for (int x = 1; x < world->width() - 1; ++x)
                if (world->rawObj(x, y) != Obj::MINE && !world->revealed(x, y)) {
                    tx = x; ty = y; break;
                }
        CHECK(tx >= 0 && ty >= 0);
        CHECK(walkMineSafe(session, *loaded, *world, story, script, tx, ty));
        CHECK(!world->gameOver());
    }
    CHECK(world->safeLeft() == 0 && world->exitReached());
    CHECK(!session.hasNext());
    CHECK(session.start(root, campaign("02_stenporten", "01_stenporten.adv"), *loaded, *world,
                        story, script, err, sizeof(err)));

    move(session, *loaded, *world, story, script, Dir::DOWN);
    move(session, *loaded, *world, story, script, Dir::RIGHT);
    CHECK(move(session, *loaded, *world, story, script, Dir::RIGHT) & EV_PUSHED);
    CHECK(world->rawObj(4, 2) == Obj::ROCK && world->gateOpenAt(5, 1));
    move(session, *loaded, *world, story, script, Dir::LEFT);
    move(session, *loaded, *world, story, script, Dir::UP);
    for (int i = 0; i < 6; ++i) move(session, *loaded, *world, story, script, Dir::RIGHT);
    CHECK(session.currentMapId() == 2);

    move(session, *loaded, *world, story, script, Dir::DOWN);
    move(session, *loaded, *world, story, script, Dir::RIGHT);
    CHECK(move(session, *loaded, *world, story, script, Dir::RIGHT) & EV_PUSHED);
    CHECK(world->rawObj(4, 2) == Obj::BOX_GREEN && world->gateOpenAt(5, 1));
    move(session, *loaded, *world, story, script, Dir::LEFT);
    move(session, *loaded, *world, story, script, Dir::UP);
    for (int i = 0; i < 5; ++i) move(session, *loaded, *world, story, script, Dir::RIGHT);
    CHECK(session.currentMapId() == 3);

    move(session, *loaded, *world, story, script, Dir::RIGHT);
    CHECK(move(session, *loaded, *world, story, script, Dir::RIGHT) & EV_PUSHED);
    CHECK(move(session, *loaded, *world, story, script, Dir::RIGHT) & EV_PUSHED);
    CHECK(world->rawObj(5, 1) == Obj::BOX_RED);
    move(session, *loaded, *world, story, script, Dir::DOWN);
    move(session, *loaded, *world, story, script, Dir::RIGHT);
    move(session, *loaded, *world, story, script, Dir::RIGHT);
    move(session, *loaded, *world, story, script, Dir::UP);
    CHECK(move(session, *loaded, *world, story, script, Dir::RIGHT) & EV_PUSHED);
    CHECK(world->rawObj(5, 1) == Obj::BOX_RED && world->rawObj(8, 1) == Obj::BOX_BLUE);
    move(session, *loaded, *world, story, script, Dir::DOWN);
    move(session, *loaded, *world, story, script, Dir::RIGHT);
    move(session, *loaded, *world, story, script, Dir::RIGHT);
    CHECK(!world->gateOpenAt(10, 1));
    const uint32_t leverBlock = move(session, *loaded, *world, story, script, Dir::DOWN);
    CHECK(leverBlock & EV_BLOCKED);
    CHECK(world->activate() & EV_LEVER);
    CHECK(world->gateOpenAt(10, 1));
    move(session, *loaded, *world, story, script, Dir::UP);
    move(session, *loaded, *world, story, script, Dir::RIGHT);
    move(session, *loaded, *world, story, script, Dir::RIGHT);
    CHECK(world->keys(KeyColor::RED) == 1);
    CHECK(move(session, *loaded, *world, story, script, Dir::RIGHT) & EV_DOOR);
    CHECK(world->keys(KeyColor::RED) == 0);
    move(session, *loaded, *world, story, script, Dir::RIGHT);
    CHECK(move(session, *loaded, *world, story, script, Dir::RIGHT) & EV_TREASURE);
    CHECK(world->pogo() == 1 && world->canVault());
    CHECK(move(session, *loaded, *world, story, script, Dir::RIGHT) & EV_BLOCKED);
    const int oldX = world->playerX(), oldY = world->playerY();
    const uint32_t vault = world->vault();
    CHECK(vault & EV_MOVED);
    CHECK(session.afterAction(oldX, oldY, vault, *loaded, *world, story, script, err, sizeof(err)) ==
          Adv::SdSession::PORTAL_NONE);
    CHECK(world->playerX() == 15 && world->hasSuction());
    CHECK(move(session, *loaded, *world, story, script, Dir::RIGHT) & EV_MOVED);
    CHECK(session.currentMapId() == 4 && world->playerX() == 7 && world->playerY() == 3);
    CHECK(world->hasSuction() && world->pogo() == 1);
    CHECK(!(move(session, *loaded, *world, story, script, Dir::RIGHT) & EV_MOVED));
    CHECK(!world->canVault() && !world->exitReached());
    CHECK(move(session, *loaded, *world, story, script, Dir::LEFT) & EV_MOVED);
    CHECK(world->canPull());
    CHECK(world->pull() & EV_PUSHED);
    CHECK(world->rawObj(6, 3) == Obj::ROCK && world->gateOpenForGroup(1));
    move(session, *loaded, *world, story, script, Dir::RIGHT);
    move(session, *loaded, *world, story, script, Dir::RIGHT);
    CHECK(move(session, *loaded, *world, story, script, Dir::RIGHT) & EV_EXIT);
    CHECK(!session.hasNext());
    CHECK(session.restart(*loaded, *world, story, script, err, sizeof(err)));
    CHECK(session.currentMapId() == 1 && world->rawObj(3, 2) == Obj::ROCK);
    CHECK(world->pogo() == 0 && !world->hasSuction());
    CHECK(session.start(root, campaign("03_fyrarnas_sang", "01_fyrarnas_sang.adv"),
                        *loaded, *world, story, script, err, sizeof(err)));

    move(session, *loaded, *world, story, script, Dir::RIGHT);
    CHECK(world->tile(6, 1) == Tile::WATER && world->playerCanEnter(6, 1) == false);
    move(session, *loaded, *world, story, script, Dir::RIGHT);
    move(session, *loaded, *world, story, script, Dir::RIGHT);
    CHECK(story.items[script->itemId("plank", 5)] == 1);
    move(session, *loaded, *world, story, script, Dir::RIGHT);
    CHECK(move(session, *loaded, *world, story, script, Dir::RIGHT) & EV_TALK);
    TestHost host;
    host.session = &session;
    host.loaded = loaded.get();
    host.world = world.get();
    host.state = &story;
    host.script = &script;
    Story::Dialog dialog;
    CHECK(dialog.start(*script, story, "byggare", &host));
    int choice = choiceWithText(dialog, "Bygg bron.");
    CHECK(choice >= 0 && dialog.enabled(choice));
    CHECK(dialog.choose(choice));
    CHECK(world->tile(6, 1) == Tile::BRIDGE && story.items[script->itemId("plank", 5)] == 0);
    CHECK(Story::evalCond(*script, story, "bridge_built & quest:crossing=active"));

    move(session, *loaded, *world, story, script, Dir::DOWN);
    move(session, *loaded, *world, story, script, Dir::RIGHT);
    move(session, *loaded, *world, story, script, Dir::RIGHT);
    CHECK(move(session, *loaded, *world, story, script, Dir::UP) & EV_TALK);
    CHECK(world->tile(6, 1) == Tile::BRIDGE);
    CHECK(dialog.start(*script, story, "budbarare", &host));
    choice = choiceWithText(dialog, "Skicka fyrlyktan.");
    CHECK(choice >= 0 && dialog.enabled(choice));
    CHECK(dialog.choose(choice));
    CHECK(story.items[script->itemId("beacon", 6)] == 0);
    CHECK(Story::evalCond(*script, story, "archive_lit"));
    CHECK(session.currentMapId() == 1);

    CHECK(!Story::applyEffects(*script, story, "give plank; world object map=2 7 3 3", &host));
    CHECK(story.items[script->itemId("plank", 5)] == 0);
    CHECK(!Story::applyEffects(*script, story, "world object map=2 7 3 3", &host));
    CHECK(!Story::applyEffects(*script, story, "world tile map=99 6 1 8", &host));
    CHECK(!Story::applyEffects(*script, story, "world tile map=2 1 1 8", &host));
    CHECK(!Story::applyEffects(*script, story, "world object map=2 4 2 13", &host));
    CHECK(!Story::applyEffects(*script, story, "world object map=2 5 2 6", &host));
    CHECK(!Story::applyEffects(*script, story, "world tile map=2 -1 1 8", &host));
    CHECK(!Story::applyEffects(*script, story, "world tile map=2 6 2 255", &host));
    CHECK(!Story::applyEffects(*script, story, "world tile map=2 6 2 25", &host));
    CHECK(!Story::applyEffects(*script, story, "world object map=2 5 4 3", &host));
    CHECK(!Story::applyEffects(*script, story, "world object map=2 8 2 3", &host));
    CHECK(!Story::applyEffects(*script, story, "world object map=2 1 1 3", &host));
    CHECK(!Story::applyEffects(*script, story, "world object map=1 9 1 3", &host));
    const int tileArgs[] = {world->playerX(), world->playerY(), (int)Tile::BRIDGE};
    CHECK(!session.applyWorldActionOnMap("tile", 1, 3, tileArgs, *loaded, *world, story, script,
                                        err, sizeof(err)));
    const int objectArgs[] = {world->playerX(), world->playerY(), (int)Obj::KEY};
    CHECK(!session.applyWorldActionOnMap("object", 1, 3, objectArgs, *loaded, *world, story,
                                        script, err, sizeof(err)));
    CHECK(world->tile(tileArgs[0], tileArgs[1]) == Tile::GRASS);
    CHECK(world->rawObj(objectArgs[0], objectArgs[1]) == Obj::NONE);
    CHECK(dialog.start(*script, story, "budbarare", &host));
    CHECK(choiceWithText(dialog, "Skicka fyrlyktan.") < 0);

    move(session, *loaded, *world, story, script, Dir::RIGHT);
    move(session, *loaded, *world, story, script, Dir::UP);
    move(session, *loaded, *world, story, script, Dir::RIGHT);
    move(session, *loaded, *world, story, script, Dir::RIGHT);
    CHECK(move(session, *loaded, *world, story, script, Dir::RIGHT) & EV_MOVED);
    CHECK(session.currentMapId() == 2 && world->rawObj(6, 2) == Obj::KEY);
    CHECK(world->rawObj(7, 3) == Obj::ROCK && world->hidden(7, 3) == Obj::GEM);
    std::vector<uint8_t> save(Adv::SdSession::SAVE_CAP);
    const size_t saveSize = session.save(*world, story, save.data(), save.size());
    CHECK(saveSize > 0);
    move(session, *loaded, *world, story, script, Dir::RIGHT);
    CHECK(session.restore(save.data(), saveSize, *loaded, *world, story, script, err, sizeof(err)));
    CHECK(world->playerX() == 1 && world->rawObj(6, 2) == Obj::KEY);
    CHECK(move(session, *loaded, *world, story, script, Dir::DOWN) & EV_MOVED);
    CHECK(move(session, *loaded, *world, story, script, Dir::UP) & EV_MOVED);
    CHECK(session.currentMapId() == 1 && world->tile(6, 1) == Tile::BRIDGE);
    move(session, *loaded, *world, story, script, Dir::UP);
    CHECK(move(session, *loaded, *world, story, script, Dir::RIGHT) & EV_MOVED);
    CHECK(session.currentMapId() == 2);
    move(session, *loaded, *world, story, script, Dir::RIGHT);
    move(session, *loaded, *world, story, script, Dir::RIGHT);
    move(session, *loaded, *world, story, script, Dir::DOWN);
    move(session, *loaded, *world, story, script, Dir::DOWN);
    const uint32_t talkEvents = move(session, *loaded, *world, story, script, Dir::DOWN);
    CHECK(talkEvents & EV_TALK);
    CHECK(dialog.start(*script, story, "arkivarie", &host));
    choice = choiceWithText(dialog, "Ta med en lykta.");
    CHECK(choice >= 0 && dialog.enabled(choice));
    CHECK(dialog.choose(choice));
    const int beacon = script->itemId("beacon", 6);
    CHECK(story.items[beacon] == 1 && Story::evalCond(*script, story, "archive_rewarded"));
    CHECK(dialog.start(*script, story, "arkivarie", &host));
    CHECK(choiceWithText(dialog, "Ta med en lykta.") < 0);
    choice = choiceWithText(dialog, "Tillbaka till fyren.");
    CHECK(choice >= 0 && dialog.enabled(choice) && dialog.choose(choice));
    CHECK(!std::strcmp(host.pendingDestination, "home"));
    CHECK(session.travelToDestination(host.pendingDestination, *loaded, *world, story, script,
                                      err, sizeof(err)));
    CHECK(session.currentMapId() == 1 && world->tile(6, 1) == Tile::BRIDGE);
    CHECK(walkMineSafe(session, *loaded, *world, story, script, 3, 2));
    CHECK(dialog.start(*script, story, "byggare", &host));
    CHECK(choiceWithText(dialog, "Bygg bron.") < 0);

    for (int i = 0; i < 5; ++i) move(session, *loaded, *world, story, script, Dir::RIGHT);
    move(session, *loaded, *world, story, script, Dir::UP);
    CHECK(move(session, *loaded, *world, story, script, Dir::RIGHT) & EV_MOVED);
    CHECK(session.currentMapId() == 2);
    for (int i = 0; i < 8; ++i) move(session, *loaded, *world, story, script, Dir::RIGHT);
    move(session, *loaded, *world, story, script, Dir::DOWN);
    move(session, *loaded, *world, story, script, Dir::DOWN);
    move(session, *loaded, *world, story, script, Dir::DOWN);
    CHECK(move(session, *loaded, *world, story, script, Dir::DOWN) & EV_CHECKPOINT);
    CHECK(move(session, *loaded, *world, story, script, Dir::DOWN) & EV_EXIT);
    CHECK(!session.hasNext());
    CHECK(session.restart(*loaded, *world, story, script, err, sizeof(err)));
    CHECK(!Story::evalCond(*script, story, "bridge_built"));
    CHECK(world->tile(6, 1) == Tile::WATER && world->obj(6, 2) == Obj::NONE);
    CHECK(session.start(root, campaign("04_vindverkstaden", "01_vindverkstaden.adv"),
                        *loaded, *world, story, script, err, sizeof(err)));

    move(session, *loaded, *world, story, script, Dir::RIGHT);
    CHECK(move(session, *loaded, *world, story, script, Dir::RIGHT) & EV_PUSHED);
    move(session, *loaded, *world, story, script, Dir::DOWN);
    move(session, *loaded, *world, story, script, Dir::DOWN);
    move(session, *loaded, *world, story, script, Dir::RIGHT);
    CHECK(move(session, *loaded, *world, story, script, Dir::UP) == EV_BLOCKED);
    CHECK(world->activate() & EV_LEVER);
    for (int i = 0; i < 4; ++i) move(session, *loaded, *world, story, script, Dir::RIGHT);
    move(session, *loaded, *world, story, script, Dir::UP);
    move(session, *loaded, *world, story, script, Dir::UP);
    CHECK(session.currentMapId() == 2);
    CHECK(world->tile(3, 3) == Tile::ICE && world->tile(4, 3) == Tile::BELT_RIGHT);
    move(session, *loaded, *world, story, script, Dir::DOWN);
    CHECK(move(session, *loaded, *world, story, script, Dir::RIGHT) & EV_TREASURE);
    CHECK(world->hasSkates());
    move(session, *loaded, *world, story, script, Dir::UP);
    CHECK(move(session, *loaded, *world, story, script, Dir::RIGHT) & EV_PUSHED);
    CHECK(world->timerRemaining(2) > 0);
    move(session, *loaded, *world, story, script, Dir::DOWN);
    move(session, *loaded, *world, story, script, Dir::RIGHT);
    move(session, *loaded, *world, story, script, Dir::RIGHT);
    move(session, *loaded, *world, story, script, Dir::UP);
    for (int i = 0; i < 4; ++i) move(session, *loaded, *world, story, script, Dir::RIGHT);
    CHECK(session.currentMapId() == 3);
    move(session, *loaded, *world, story, script, Dir::RIGHT);
    move(session, *loaded, *world, story, script, Dir::RIGHT);
    move(session, *loaded, *world, story, script, Dir::RIGHT);
    CHECK(world->hasBoat() && world->suitCharges() > 0);
    move(session, *loaded, *world, story, script, Dir::RIGHT);
    CHECK(world->sailing());
    move(session, *loaded, *world, story, script, Dir::DOWN);
    CHECK(move(session, *loaded, *world, story, script, Dir::LEFT) & EV_CHECKPOINT);
    move(session, *loaded, *world, story, script, Dir::RIGHT);
    move(session, *loaded, *world, story, script, Dir::UP);
    for (int i = 0; i < 5; ++i) move(session, *loaded, *world, story, script, Dir::RIGHT);
    CHECK(session.currentMapId() == 4 && world->hasBoat());
    move(session, *loaded, *world, story, script, Dir::DOWN);
    move(session, *loaded, *world, story, script, Dir::RIGHT);
    CHECK(move(session, *loaded, *world, story, script, Dir::RIGHT) & EV_GEM);
    CHECK(move(session, *loaded, *world, story, script, Dir::RIGHT) & EV_SIZZLE);
    CHECK(world->tile(5, 2) == Tile::BASALT);
    move(session, *loaded, *world, story, script, Dir::UP);
    uint32_t exitEvents = 0;
    for (int i = 0; i < 4; ++i) exitEvents = move(session, *loaded, *world, story, script, Dir::RIGHT);
    CHECK(exitEvents & EV_EXIT);
    CHECK(!session.hasNext());
    CHECK(session.start(root, campaign("05_hemresan", "01_hemresan.adv"),
                        *loaded, *world, story, script, err, sizeof(err)));
    move(session, *loaded, *world, story, script, Dir::RIGHT);
    CHECK(move(session, *loaded, *world, story, script, Dir::RIGHT) & EV_TALK);
    CHECK(dialog.start(*script, story, "fyrvakt", &host));
    choice = choiceWithText(dialog, "Till hamnen.");
    CHECK(choice >= 0 && dialog.enabled(choice));
    CHECK(dialog.choose(choice));
    CHECK(!std::strcmp(host.pendingDestination, "harbor"));
    CHECK(session.travelToDestination(host.pendingDestination, *loaded, *world, story, script,
                                      err, sizeof(err)));
    CHECK(session.currentMapId() == 2 && story.quests[script->questId("crossing", 8)] == (uint8_t)Story::Quest::DONE);
    for (int i = 0; i < 5; ++i) move(session, *loaded, *world, story, script, Dir::RIGHT);
    CHECK(world->exitReached() && !session.hasNext());
}

void carryReset()
{
    const char *root = "sdcard_bundle/otr/adventure/motoraventyr";
    Adv::CatalogEntry entry = campaign("06_resurskedjan", "01_lamna.adv");
    entry.mapCount = 3;
    std::snprintf(entry.maps[1], Adv::MAP_NAME_CAP, "02_fyll_pa.adv");
    std::snprintf(entry.maps[2], Adv::MAP_NAME_CAP, "03_nollstall.adv");
    auto loaded = std::make_unique<Adv::Loaded>();
    auto world = std::make_unique<World>();
    Story::StoryState story{};
    const Story::Script *script = nullptr;
    Adv::SdSession session;
    CHECK(session.start(root, entry, *loaded, *world, story, script, err, sizeof(err)));
    const int stone = script->itemId("stone", 5);
    CHECK(stone >= 0);
    CHECK(move(session, *loaded, *world, story, script, Dir::RIGHT) & EV_TREASURE);
    CHECK(story.items[stone] == 1);
    CHECK(move(session, *loaded, *world, story, script, Dir::RIGHT) & EV_EXIT);
    CHECK(session.travel(*loaded, *world, story, script, err, sizeof(err)));
    CHECK(session.mapIndex() == 1 && story.items[script->itemId("stone", 5)] == 1);
    CHECK(move(session, *loaded, *world, story, script, Dir::RIGHT) & EV_TREASURE);
    CHECK(story.items[script->itemId("stone", 5)] == 2);
    move(session, *loaded, *world, story, script, Dir::RIGHT);
    CHECK(move(session, *loaded, *world, story, script, Dir::RIGHT) & EV_EXIT);
    CHECK(session.travel(*loaded, *world, story, script, err, sizeof(err)));
    CHECK(session.mapIndex() == 2 && story.items[script->itemId("stone", 5)] == 0);
}

}  // namespace

int main()
{
    productionLoad();
    play();
    carryReset();
    std::printf("Motoräventyret: %d checks passed\n", checks);
}
