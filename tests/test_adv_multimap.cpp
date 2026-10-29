#include "adv_sd_session.h"
#include "adv_live.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

using namespace Otr;

namespace {

char err[256];
int checks;
#define CHECK(c) do { ++checks; if (!(c)) { std::printf("FAIL %d: %s (%s)\n", __LINE__, #c, err); std::abort(); } } while (0)

std::string read(const char *path)
{
    std::ifstream file(path, std::ios::binary);
    CHECK(file.good());
    return {std::istreambuf_iterator<char>(file), {}};
}

Adv::CatalogEntry entry()
{
    Adv::CatalogEntry e{};
    std::snprintf(e.folder, sizeof(e.folder), "multimap_portals");
    e.mapCount = 1;
    std::snprintf(e.maps[0], Adv::MAP_NAME_CAP, "01_multimap.adv");
    return e;
}

struct TestHost : Story::Host {
    Adv::SdSession *session = nullptr;
    Adv::Loaded *loaded = nullptr;
    World *world = nullptr;
    Story::StoryState *state = nullptr;
    const Story::Script **script = nullptr;
    char *error = nullptr;
    size_t errorCap = 0;
    bool actionCalled = false;
    bool travelled = false;
    void worldAction(const char *, int, const int *) override {}
    void worldActionOnMap(const char *verb, int mapId, int argc, const int *argv) override
    {
        actionCalled = true;
        travelled = session->applyWorldActionOnMap(verb, mapId, argc, argv, *loaded, *world,
                                                   *state, *script, error, errorCap);
    }
    void worldTravel(const char *destination) override
    {
        travelled = session->travelToDestination(destination, *loaded, *world, *state, *script,
                                                 error, errorCap);
    }
    void sound(const char *) override {}
};

void parser()
{
    const std::string source = read("docs/adventures/multimap_portals/01_multimap.adv");
    auto loaded = std::make_unique<Adv::Loaded>();
    CHECK(Adv::parseMap(source.data(), source.size(), 1, *loaded, err, sizeof(err)));
    CHECK(loaded->internalMapCount == 2 && loaded->internalMapId == 1);
    CHECK(loaded->internalMapIds[0] == 1 && loaded->internalMapIds[1] == 2);
    CHECK(loaded->rowCount == 5 && loaded->rows[1][2] == '.');
    CHECK(loaded->resourceCount == 1 && loaded->resources[0].glyph == 's');
    CHECK(loaded->itemCount == 1 && !std::strcmp(loaded->items[0].id, "stone"));
    CHECK(loaded->npcCount == 2 && loaded->script.npcByNum(1, 1));
    CHECK(loaded->script.npcByNum(1, 2));
    CHECK(loaded->destinationCount == 3 && loaded->portalCount == 4);
    CHECK(loaded->controlCount == 0 &&
          (uint8_t)loaded->rows[3][3] == Otr::GROUPED_PLATE_GLYPH + 1 &&
          (uint8_t)loaded->rows[3][4] == Otr::GROUPED_GATE_GLYPH + 1);
    CHECK(loaded->enemyCount == 0 && loaded->darkCount == 0);

    CHECK(Adv::parseMap(source.data(), source.size(), 2, *loaded, err, sizeof(err)));
    CHECK(loaded->internalMapId == 2 && loaded->rowCount == 6);
    CHECK(loaded->resources[0].glyph == 't' && loaded->itemCount == 1);
    CHECK(loaded->controlCount == 1 && loaded->controls[0].group == 2);
    CHECK(loaded->controls[0].steps == 1 && loaded->enemyCount == 1 && loaded->darkCount == 1);
    CHECK(loaded->map.rules.startEnergy == 8 && loaded->map.rules.energyCap == 20);
    CHECK(loaded->npcCount == 2 && loaded->script.npcByNum(1, 2));
    CHECK(loaded->map.id == 2);

    for (const char *bad : {
        "@map 2\nS.\n",
        "@map\nS.\n@map 2\nS.\n@map 2\nS.\n",
        "@map\nS.\n@map 2\nS.\n@resources map=3\ns : stone\n",
        "@map\nS.\n@map 2\nS.\n@destinations\nmap=2: home 0,0\n@portals\nmap=2: 1,0 -> missing\n",
        "@map\nS.\n@map 2\nS.\n@destinations\nhome 0,0\n@portals\n1,0 -> missing\n",
        "@map\nS.\n@map 2\nS.\n@destinations\nhome 0,0\nhome 1,0\n"}) {
        CHECK(!Adv::parse(bad, std::strlen(bad), *loaded, err, sizeof(err)));
        CHECK(err[0]);
    }
    const std::string tooManyMaps = "@map\nS.\n@map 2\nS.\n@map 3\nS.\n@map 4\nS.\n@map 5\nS.\n";
    CHECK(!Adv::parse(tooManyMaps.data(), tooManyMaps.size(), *loaded, err, sizeof(err)));
    CHECK(std::strstr(err, "maximum 4"));
    std::string tooManyDestinations = "@destinations\n";
    for (int i = 0; i < Adv::MAX_DESTINATIONS + 1; ++i)
        tooManyDestinations += "dest" + std::to_string(i) + " 0,0\n";
    tooManyDestinations += "@map\nS.\n";
    CHECK(!Adv::parse(tooManyDestinations.data(), tooManyDestinations.size(), *loaded, err, sizeof(err)));
    CHECK(std::strstr(err, "maximum 32"));
    std::string tooManyPortals = "@destinations\nhome 0,0\n@portals\n";
    for (int i = 0; i < Adv::MAX_PORTALS + 1; ++i)
        tooManyPortals += std::to_string(i) + ",0 -> home\n";
    tooManyPortals += "@map\nS.\n";
    CHECK(!Adv::parse(tooManyPortals.data(), tooManyPortals.size(), *loaded, err, sizeof(err)));
    CHECK(std::strstr(err, "maximum 32"));
    const char *conflictingResources =
        "@resources\ns : stone : Stone : Stone : 2 :\n"
        "@resources map=2\nt : stone : Sten : Stone : 2 :\n"
        "@map\nS.\n@map 2\nS.\n";
    CHECK(!Adv::parse(conflictingResources, std::strlen(conflictingResources), *loaded, err, sizeof(err)));
    CHECK(std::strstr(err, "conflicting"));
    const char *badRules = "@rules map=2\nenergy_cap: 10\n@map\nS.\n@map 2\nS.\n";
    CHECK(!Adv::parseMap(badRules, std::strlen(badRules), 2, *loaded, err, sizeof(err)));
}

void runtime(const char *root)
{
    auto loaded = std::make_unique<Adv::Loaded>();
    auto world = std::make_unique<World>();
    Story::StoryState story{};
    const Story::Script *script = nullptr;
    Adv::SdSession session;
    CHECK(session.start(root, entry(), *loaded, *world, story, script, err, sizeof(err)));
    CHECK(session.hasInternalMaps() && session.currentMapId() == 1 && session.internalMapCount() == 2);
    CHECK(world->energy() == 9 && world->livesLeft() == 4);
    CHECK(loaded->resourceCount == 1 && loaded->items[0].cap == 2);

    const int oldX = world->playerX(), oldY = world->playerY();
    const uint32_t events = world->move(Dir::RIGHT);
    CHECK(events & EV_TREASURE);
    CHECK(story.items[0] == 1);
    CHECK(session.afterAction(oldX, oldY, events, *loaded, *world, story, script, err, sizeof(err)) ==
          Adv::SdSession::PORTAL_TRAVELLED);
    CHECK(session.currentMapId() == 2 && world->playerX() == 2 && world->playerY() == 1);
    CHECK(world->energy() == 9 && world->livesLeft() == 4);
    {
        TestHost host;
        host.session = &session;
        host.loaded = loaded.get();
        host.world = world.get();
        host.state = &story;
        host.script = &script;
        host.error = err;
        host.errorCap = sizeof(err);
        int guardStart = -1;
        for (int i = 0; i < loaded->nodeCount; ++i)
            if (!std::strcmp(loaded->nodes[i].id, "guard.start")) guardStart = i;
        CHECK(guardStart >= 0);
        const char *effects = "world npc_move map=1 1 8 2; take stone; give stone";
        loaded->nodes[guardStart].effects = loaded->intern(effects, std::strlen(effects));
        const Story::Script *activeScript = script;
        const Story::Node *activeNode = &loaded->nodes[guardStart];
        Story::Dialog dialog;
        CHECK(dialog.start(*script, story, "guard", &host));
        if (!host.actionCalled) std::printf("no mapped callback: node=%s effects=%s\n",
            dialog.node() ? dialog.node()->id : "(null)", dialog.node() && dialog.node()->effects ? dialog.node()->effects : "(null)");
        if (host.actionCalled && !host.travelled) std::printf("mapped dialog action failed: %s\n", err);
        CHECK(host.actionCalled);
        CHECK(host.travelled);
        CHECK(dialog.node() == activeNode);
        CHECK(dialog.node()->effects == loaded->nodes[guardStart].effects &&
              !std::strcmp(dialog.node()->effects, effects));
        CHECK(script == activeScript && script->validate(err, sizeof(err)) == 0);
        CHECK(story.items[0] == 1);
    }

    std::vector<uint8_t> image(Adv::SdSession::SAVE_CAP);
    const size_t size = session.save(*world, story, image.data(), image.size());
    CHECK(size > 0 && size <= Adv::SdSession::SAVE_CAP);
    CHECK(session.restore(image.data(), size, *loaded, *world, story, script, err, sizeof(err)));
    CHECK(session.currentMapId() == 2 && world->playerX() == 2 && story.items[0] == 1);
    auto live = std::make_unique<AdvLive::Snapshot>();
    AdvLive::Source source{world.get(), "Cave", script, &story, nullptr, 1, "%s", "OK", "Bye", 0};
    source.adventure = loaded.get();
    source.folder = session.folder();
    source.activeMapId = session.currentMapId();
    AdvLive::fillMap(*live, source);
    AdvLive::fillState(*live, source);
    CHECK(live->activeMapId == 2 && live->portalCount == 2);
    CHECK(live->portals[0].x == 2 && live->portals[0].y == 1);
    CHECK(live->portals[1].x == 7 && live->portals[1].y == 1);
    CHECK(live->resourceTypeCount == 1 && live->resourceTypes[0].item == 0);
    bool foundGuard = false;
    for (int i = 0; i < live->npcCount; ++i)
        if (live->npcs[i].num == 1) foundGuard = !std::strcmp(live->npcs[i].name, "Guard");
    CHECK(foundGuard);
    std::vector<char> liveJson(AdvLive::JSON_CAP);
    CHECK(AdvLive::toJson(*live, 0, liveJson.data(), liveJson.size()) > 0);
    CHECK(std::strstr(liveJson.data(), "\"active_map_id\":2"));
    CHECK(std::strstr(liveJson.data(), "\"portals\":[{\"x\":2,\"y\":1},{\"x\":7,\"y\":1}]"));
    CHECK(session.afterAction(1, 1, EV_MOVED, *loaded, *world, story, script, err, sizeof(err)) ==
          Adv::SdSession::PORTAL_NONE);

    const uint32_t pickup = world->move(Dir::DOWN);
    CHECK(pickup & EV_TREASURE);
    CHECK(story.items[0] == 2);

    // Visit the cave checkpoint; it becomes the adventure-wide respawn point.
    CHECK(world->setPlayerPosition(4, 4));
    const uint32_t checkpointEvent = world->move(Dir::RIGHT);
    CHECK(checkpointEvent & EV_CHECKPOINT);
    CHECK(session.afterAction(5, 4, checkpointEvent, *loaded, *world, story, script, err, sizeof(err)) ==
          Adv::SdSession::PORTAL_NONE);
    CHECK(session.checkpointMapId() == 2);

    // Push the crate onto the cave arrival so the cross-map portal has a dynamic blocker.
    CHECK(world->setPlayerPosition(6, 1));
    uint32_t pushEvents = world->move(Dir::LEFT);
    CHECK(pushEvents & EV_PUSHED);
    CHECK(world->move(Dir::LEFT) & EV_PUSHED);
    CHECK(world->move(Dir::LEFT) & EV_PUSHED);
    CHECK(world->obj(2, 1) == Obj::ROCK);

    // The portal at (7,1) returns home, preserving the cave's moved crate.
    CHECK(world->setPlayerPosition(6, 1));
    const uint32_t returnEvents = world->move(Dir::RIGHT);
    CHECK(session.afterAction(6, 1, returnEvents, *loaded, *world, story, script, err, sizeof(err)) ==
          Adv::SdSession::PORTAL_TRAVELLED);
    CHECK(session.currentMapId() == 1 && world->playerX() == 1);
    CHECK(story.items[0] == 2);

    // The main portal now targets an occupied landing; rejection is atomic.
    const int beforeX = world->playerX(), beforeY = world->playerY();
    const uint32_t blockedMove = world->move(Dir::RIGHT);
    CHECK(session.afterAction(beforeX, beforeY, blockedMove, *loaded, *world, story, script, err, sizeof(err)) ==
          Adv::SdSession::PORTAL_BLOCKED);
    CHECK(session.currentMapId() == 1 && world->playerX() == 2 && world->playerY() == 1);
    CHECK(world->obj(2, 1) == Obj::NONE);
    CHECK(std::strstr(err, "blocked"));

    // The alternate portal avoids the blocked landing. Push the rock off the arrival,
    // entering the cave return portal and preserving the moved rock.
    CHECK(world->move(Dir::DOWN) & EV_MOVED);
    for (int i = 0; i < 5; ++i) CHECK(world->move(Dir::RIGHT) & EV_MOVED);
    const uint32_t sidePortal = world->move(Dir::DOWN);
    CHECK(session.afterAction(7, 2, sidePortal, *loaded, *world, story, script, err, sizeof(err)) ==
          Adv::SdSession::PORTAL_TRAVELLED);
    CHECK(session.currentMapId() == 2 && world->playerX() == 1 && world->playerY() == 4);
    CHECK(world->move(Dir::UP) & EV_MOVED);
    CHECK(world->move(Dir::UP) & EV_MOVED);
    CHECK(world->move(Dir::UP) & EV_MOVED);
    const uint32_t clearLanding = world->move(Dir::RIGHT);
    CHECK(clearLanding & EV_PUSHED);
    CHECK(session.afterAction(1, 1, clearLanding, *loaded, *world, story, script, err, sizeof(err)) ==
          Adv::SdSession::PORTAL_TRAVELLED);
    CHECK(session.currentMapId() == 1);
    const uint32_t openArrival = world->move(Dir::RIGHT);
    CHECK(session.afterAction(1, 1, openArrival, *loaded, *world, story, script, err, sizeof(err)) ==
          Adv::SdSession::PORTAL_TRAVELLED);
    CHECK(session.currentMapId() == 2 && world->obj(2, 1) == Obj::NONE &&
          world->obj(3, 1) == Obj::ROCK);

    // Cross-map checkpoint respawn keeps the shared life count and returns to map 2.
    CHECK(session.travelToDestination("home", *loaded, *world, story, script, err, sizeof(err)));
    CHECK(session.currentMapId() == 1);
    CHECK(world->setPlayerPosition(5, 2));
    world->setEnergy(1);
    const uint32_t deadly = world->move(Dir::DOWN);
    CHECK(deadly & EV_DIED);
    CHECK(session.afterAction(5, 2, deadly, *loaded, *world, story, script, err, sizeof(err)) ==
          Adv::SdSession::PORTAL_CHECKPOINT_RESPAWNED);
    CHECK(session.currentMapId() == 2 && world->playerX() == 5 && world->playerY() == 4);

    // Story travel uses the same named destination registry as portals.
    TestHost host;
    host.session = &session;
    host.loaded = loaded.get();
    host.world = world.get();
    host.state = &story;
    host.script = &script;
    host.error = err;
    host.errorCap = sizeof(err);

    CHECK(Story::applyEffects(*script, story, "world travel home", &host));
    CHECK(host.travelled && session.currentMapId() == 1);
    int npcX = -1, npcY = -1;
    CHECK(world->findNpc(1, npcX, npcY) && npcX == 8 && npcY == 2);
    CHECK(loaded->script.validate(err, sizeof(err)) == 0);

    // Saving and restoring preserves both maps, portal suppression, story and checkpoint.
    const size_t finalSize = session.save(*world, story, image.data(), image.size());
    CHECK(finalSize > 0);
    const int finalX = world->playerX(), finalY = world->playerY();
    CHECK(session.restore(image.data(), finalSize, *loaded, *world, story, script, err, sizeof(err)));
    CHECK(session.currentMapId() == 1 && world->playerX() == finalX && world->playerY() == finalY);
    CHECK(session.checkpointMapId() == 2 && story.items[0] == 2);

    // Restore is transactional for truncation, corrupted images, and changed file signature.
    const int priorX = world->playerX();
    CHECK(!session.restore(image.data(), finalSize - 1, *loaded, *world, story, script, err, sizeof(err)));
    CHECK(world->playerX() == priorX);
    image[56 + 8] ^= 0x80;
    CHECK(!session.restore(image.data(), finalSize, *loaded, *world, story, script, err, sizeof(err)));
    CHECK(world->playerX() == priorX);
    CHECK(session.restart(*loaded, *world, story, script, err, sizeof(err)));
    CHECK(session.currentMapId() == 1 && story.items[0] == 0);
    CHECK(world->obj(2, 1) == Obj::RESOURCE && session.checkpointMapId() == 1);
}

void legacy(const char *root)
{
    auto loaded = std::make_unique<Adv::Loaded>();
    auto world = std::make_unique<World>();
    Story::StoryState story{};
    const Story::Script *script = nullptr;
    Adv::SdSession session;
    auto e = entry();
    std::snprintf(e.folder, sizeof(e.folder), "z_resource_test");
    e.mapCount = 3;
    std::snprintf(e.maps[0], Adv::MAP_NAME_CAP, "01_collect.adv");
    std::snprintf(e.maps[1], Adv::MAP_NAME_CAP, "02_carry.adv");
    std::snprintf(e.maps[2], Adv::MAP_NAME_CAP, "03_reset.adv");
    CHECK(session.start(root, e, *loaded, *world, story, script, err, sizeof(err)));
    CHECK(!session.hasInternalMaps() && session.currentMapId() == 1);
    world->move(Dir::RIGHT);
    std::vector<uint8_t> oldSave(Adv::SdSession::SAVE_CAP);
    const size_t size = session.save(*world, story, oldSave.data(), oldSave.size());
    CHECK(size > 0 && oldSave[4] == 1 && oldSave[5] == 0);
    CHECK(session.restore(oldSave.data(), size, *loaded, *world, story, script, err, sizeof(err)));
    CHECK(session.travel(*loaded, *world, story, script, err, sizeof(err)));
    CHECK(session.mapIndex() == 1);
}

void editedFileRejectsSave()
{
    const std::string source = read("docs/adventures/multimap_portals/01_multimap.adv");
    const std::filesystem::path folder = "build_host_multimap/signature/multimap_portals";
    std::filesystem::create_directories(folder);
    {
        std::ofstream file(folder / "01_multimap.adv", std::ios::binary);
        file << source;
    }

    auto loaded = std::make_unique<Adv::Loaded>();
    auto world = std::make_unique<World>();
    Story::StoryState story{};
    const Story::Script *script = nullptr;
    Adv::SdSession session;
    CHECK(session.start("build_host_multimap/signature", entry(), *loaded, *world, story, script,
                        err, sizeof(err)));
    story.items[0] = 1;
    std::vector<uint8_t> image(Adv::SdSession::SAVE_CAP);
    const size_t size = session.save(*world, story, image.data(), image.size());
    CHECK(size > 0);
    {
        std::ofstream file(folder / "01_multimap.adv", std::ios::binary | std::ios::app);
        file << "\n# edited after save\n";
    }
    const int playerX = world->playerX();
    CHECK(!session.restore(image.data(), size, *loaded, *world, story, script, err, sizeof(err)));
    CHECK(std::strstr(err, "changed"));
    CHECK(world->playerX() == playerX && story.items[0] == 1);
    std::filesystem::remove_all("build_host_multimap/signature");
}

} // namespace

int main()
{
    parser();
    runtime("docs/adventures");
    runtime("sdcard_bundle/otr/adventure");
    legacy("docs/adventures");
    editedFileRejectsSave();
    std::printf("Multimap: %d checks passed\n", checks);
}
