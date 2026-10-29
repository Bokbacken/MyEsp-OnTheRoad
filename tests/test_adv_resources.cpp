#include "adv_sd_session.h"
#include "adv_live.h"
#include "adv_resource_art.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

using namespace Otr;
static char err[512];
static int checks;
#define CHECK(c) do { ++checks; if (!(c)) { std::printf("FAIL %d: %s (%s)\n", __LINE__, #c, err); std::abort(); } } while (0)

static std::string read(const std::string &path)
{
    std::ifstream file(path, std::ios::binary);
    CHECK(file.good());
    return {std::istreambuf_iterator<char>(file), {}};
}

static Adv::CatalogEntry entry()
{
    Adv::CatalogEntry e{};
    std::snprintf(e.folder, sizeof e.folder, "z_resource_test");
    const char *names[] = {"01_collect.adv", "02_carry.adv", "03_reset.adv"};
    e.mapCount = 3;
    for (int i = 0; i < 3; ++i) std::snprintf(e.maps[i], Adv::MAP_NAME_CAP, "%s", names[i]);
    return e;
}

static void play(const char *root)
{
    auto loaded = std::make_unique<Adv::Loaded>();
    auto world = std::make_unique<World>();
    Story::StoryState state{};
    const Story::Script *script = nullptr;
    Adv::SdSession session;
    const auto e = entry();
    CHECK(session.start(root, e, *loaded, *world, state, script, err, sizeof err));
    CHECK(script && !script->validate(err, sizeof err));
    CHECK(loaded->map.resourceCount == 3 && loaded->items[0].cap == 2);
    CHECK(world->obj(2, 1) == Obj::RESOURCE);
    CHECK(world->move(Dir::RIGHT) & EV_TREASURE);
    CHECK(state.items[0] == 1 && world->obj(2, 1) == Obj::NONE);
    CHECK(world->move(Dir::RIGHT) & EV_TREASURE);
    CHECK(state.items[0] == 2);
    CHECK(!(world->move(Dir::RIGHT) & EV_TREASURE));
    CHECK(world->playerX() == 4 && world->obj(4, 1) == Obj::RESOURCE);
    CHECK(state.items[0] == 2);

    Story::Dialog dialog;
    CHECK(dialog.start(*script, state, "tester", nullptr));
    CHECK(dialog.enabled(0) && dialog.choose(0));
    CHECK(state.items[0] == 1 && Story::evalCond(*script, state, "has:stone"));
    world->move(Dir::RIGHT);
    CHECK(world->move(Dir::LEFT) & EV_TREASURE);
    CHECK(state.items[0] == 2 && world->obj(4, 1) == Obj::NONE);
    CHECK(dialog.start(*script, state, "tester", nullptr));
    CHECK(dialog.choose(1));
    CHECK(state.items[0] == 2);
    CHECK(Story::applyEffects(*script, state, "take stone 9", nullptr));
    CHECK(!state.items[0] && !Story::evalCond(*script, state, "has:stone"));
    CHECK(dialog.start(*script, state, "tester", nullptr));
    CHECK(!dialog.enabled(0));
    CHECK(dialog.choose(1) && state.items[0] == 2);
    CHECK(Story::applyEffects(*script, state, "take stone", nullptr));
    world->move(Dir::RIGHT);
    CHECK(world->move(Dir::RIGHT) & EV_CHECKPOINT);
    CHECK(world->checkpointX() == 6 && world->checkpointY() == 1);
    std::vector<uint8_t> image(65536);
    const size_t size = session.save(*world, state, image.data(), image.size());
    CHECK(size > 0);
    state.items[0] = 0;
    world->move(Dir::LEFT);
    CHECK(session.restore(image.data(), size, *loaded, *world, state, script, err, sizeof err));
    CHECK(state.items[0] == 1 && world->playerX() == 6 && world->checkpointX() == 6);
    CHECK(world->obj(2, 1) == Obj::NONE && world->obj(4, 1) == Obj::NONE);
    // Collector remains bound after restore, and give uses the same inventory and cap.
    CHECK(Story::applyEffects(*script, state, "give stone 9", nullptr) && state.items[0] == 2);
    for (int i = 0; i < 4; ++i) world->move(Dir::RIGHT);
    CHECK(world->exitReached());
    CHECK(session.travel(*loaded, *world, state, script, err, sizeof err));
    CHECK(session.mapIndex() == 1 && script && script->nodeCount == 0);
    CHECK(script->itemId("stone", 5) == 1 && state.items[1] == 1 && !state.items[0]);
    world->move(Dir::RIGHT);
    CHECK(!(world->move(Dir::RIGHT) & EV_TREASURE));
    CHECK(world->obj(2, 0) == Obj::RESOURCE && state.items[1] == 1);
    CHECK(Story::applyEffects(*script, state, "take stone", nullptr));
    world->move(Dir::LEFT);
    CHECK(world->move(Dir::RIGHT) & EV_TREASURE);
    CHECK(state.items[1] == 1);
    for (int i = 0; i < 3; ++i) world->move(Dir::RIGHT);
    CHECK(session.travel(*loaded, *world, state, script, err, sizeof err));
    CHECK(session.mapIndex() == 2 && state.items[0] == 0);
    CHECK(world->move(Dir::RIGHT) & EV_TREASURE);
    CHECK(state.items[0] == 1);
    CHECK(world->move(Dir::RIGHT) & EV_CHECKPOINT);
    const size_t lastSize = session.save(*world, state, image.data(), image.size());
    CHECK(session.restart(*loaded, *world, state, script, err, sizeof err));
    CHECK(!state.items[0] && session.mapIndex() == 0 && world->obj(2, 1) == Obj::RESOURCE);
    CHECK(session.restore(image.data(), lastSize, *loaded, *world, state, script, err, sizeof err));
    CHECK(session.mapIndex() == 2 && state.items[0] == 1 && world->checkpointX() == 2);
    CHECK(world->obj(1, 0) == Obj::NONE);
}

static void live()
{
    Adv::SdSession session;
    auto loaded = std::make_unique<Adv::Loaded>();
    auto world = std::make_unique<World>();
    auto snap = std::make_unique<AdvLive::Snapshot>();
    std::vector<char> json(AdvLive::JSON_CAP);
    Story::StoryState state{};
    const Story::Script *script = nullptr;
    CHECK(session.start("docs/adventures", entry(), *loaded, *world, state, script, err, sizeof err));
    AdvLive::Source source{world.get(), "Resources", script, &state, nullptr, 1, "%s", "OK", "Bye", 0};
    source.adventure = loaded.get();
    source.folder = session.folder();
    AdvLive::fillMap(*snap, source);
    AdvLive::fillState(*snap, source);
    CHECK(snap->resourceCount == 3 && snap->resourceTypeCount == 1 && snap->itemCount == 0);
    CHECK(!std::strcmp(snap->resourceTypes[0].name, "Stone"));
    CHECK(!std::strcmp(snap->resourceTypes[0].image, "stone.png"));
    world->move(Dir::RIGHT);
    AdvLive::publish(source);
    CHECK(AdvLive::writeJson(0, json.data(), json.size()));
    CHECK(std::strstr(json.data(), "\"count\":1,\"cap\":2"));
    CHECK(std::strstr(json.data(), "\"resource_folder\":\"z_resource_test\""));
    CHECK(std::strstr(json.data(), "\"resources\":[{\"x\":3,\"y\":1,\"type\":0}"));
    AdvLive::fillState(*snap, source);
    CHECK(snap->resourceCount == 2 && snap->itemCount == 1);
    loaded->darks[0] = {3, 1, 4, 1};
    loaded->map.darks = loaded->darks;
    loaded->map.darkCount = 1;
    CHECK(world->load(loaded->map));
    AdvLive::fillState(*snap, source);
    CHECK(snap->resourceCount == 1);
    AdvLive::setInactive();
}

static void restoreErrors()
{
    Adv::SdSession session;
    auto loaded = std::make_unique<Adv::Loaded>();
    auto world = std::make_unique<World>();
    Story::StoryState state{};
    const Story::Script *script = nullptr;
    CHECK(session.start("docs/adventures", entry(), *loaded, *world, state, script, err, sizeof err));
    world->move(Dir::RIGHT);
    state.items[0] = 3;
    std::vector<uint8_t> image(65536);
    const size_t size = session.save(*world, state, image.data(), image.size());
    state.items[0] = 1;
    world->move(Dir::RIGHT);
    CHECK(!session.restore(image.data(), size, *loaded, *world, state, script, err, sizeof err));
    CHECK(std::strstr(err, "exceeds cap") && world->playerX() == 3 && state.items[0] == 2);
    CHECK(session.mapIndex() == 0 && script == &loaded->script && !script->validate(err, sizeof err));
    CHECK(Story::applyEffects(*script, state, "take stone", nullptr));
    CHECK(world->move(Dir::RIGHT) & EV_TREASURE);
    CHECK(state.items[0] == 2);
    const size_t validSize = session.save(*world, state, image.data(), image.size());
    image[56 + 20] ^= 1;
    CHECK(!session.restore(image.data(), validSize, *loaded, *world, state, script, err, sizeof err));
    CHECK(world->playerX() == 4 && state.items[0] == 2 && world->obj(2, 1) == Obj::NONE);
}

static void errors()
{
    auto loaded = std::make_unique<Adv::Loaded>();
    for (const char *text : {
        "@map 2\nS.\n", "@map\nS.\n@map\nS.\n",
        "@resources map=2\ns : stone\n@map\nSs\n",
        "@rules map=2\nfog: true\n@map\nS.\n",
        "@resources\ns : stone\nw : stone\n@map\nSs\n",
        "@resources\ns : stone : Sten : Stone : 2 : ../stone.png\n@map\nSs\n",
        "@resources\ns : stone : Sten : Stone : 2 : stone.bmp\n@map\nSs\n"}) {
        CHECK(!Adv::parse(text, std::strlen(text), *loaded, err, sizeof err));
        CHECK(err[0]);
    }
    // Resources on a forced-motion landing are picked in the engine, not by inspecting
    // only the final position in the UI. A full pickup remains available.
    const char *text = "@legend\nc = belt right\n@resources\ns : stone : Sten : Stone : 1 :\n@map\nScs.\n";
    CHECK(Adv::parse(text, std::strlen(text), *loaded, err, sizeof err));
    World world;
    CHECK(world.load(loaded->map));
    struct Collector : ItemCollector {
        int count = 0;
        bool collect(uint8_t item) override { CHECK(item == 0); ++count; return true; }
    } collector;
    world.setItemCollector(&collector);
    CHECK(world.move(Dir::RIGHT) & EV_TREASURE);
    CHECK(world.playerX() == 2 && collector.count == 1);
    const char *danger = "@resources\ns : stone : Sten : Stone : 1 :\n@map\nSsQ^\n";
    CHECK(Adv::parse(danger, std::strlen(danger), *loaded, err, sizeof err));
    CHECK(world.load(loaded->map));
    world.setItemCollector(&collector);
    CHECK(world.move(Dir::RIGHT) & EV_TREASURE);
    CHECK(world.move(Dir::RIGHT) & EV_CHECKPOINT);
    world.setEnergy(1);
    const int inventory = collector.count;
    CHECK(world.move(Dir::RIGHT) & EV_DIED);
    CHECK(world.playerX() == 2 && world.checkpointX() == 2);
    CHECK(collector.count == inventory && world.obj(1, 0) == Obj::NONE);
    // Without an inventory binding the engine must not erase a resource.
    CHECK(world.load(loaded->map));
    world.move(Dir::RIGHT);
    CHECK(world.obj(1, 0) == Obj::RESOURCE);
    std::vector<uint8_t> legacy(world.saveSize());
    CHECK(world.save(legacy.data(), legacy.size()));
    legacy[4] = 6;
    CHECK(!world.restore(legacy.data(), legacy.size()));
    CHECK(world.obj(1, 0) == Obj::RESOURCE);
    int pixels = 0;
    Adv::drawResource(nullptr, 16, [&](int x, int y, int, int, int, int alpha) {
        CHECK(x >= 0 && x < 16 && y >= 0 && y < 16 && alpha == 255);
        ++pixels;
    });
    CHECK(pixels > 20);
    // Exercise full map/resource/inventory capacities through production JSON.
    std::string many = "@resources\ns : stone : Sten : Stone : 2 :\n@items\n";
    for (int i = 0; i < 31; ++i) many += "item" + std::to_string(i) + " : Item\n";
    many += "@map\n";
    for (int y = 0; y < 80; ++y) {
        std::string row(80, '.');
        for (int x = 0; x < 80; ++x) if (y * 80 + x >= 1 && y * 80 + x <= 160) row[x] = 's';
        if (!y) row[0] = 'S';
        many += row + "\n";
    }
    CHECK(Adv::parse(many.data(), many.size(), *loaded, err, sizeof err));
    CHECK(world.load(loaded->map) && loaded->placementCount == Adv::MAX_RES_PLACE);
    Story::StoryState state{};
    for (auto &item : state.items) item = 1;
    auto snap = std::make_unique<AdvLive::Snapshot>();
    AdvLive::Source source{&world, "Capacity", &loaded->script, &state, nullptr, 0, "", "", "", 0};
    source.adventure = loaded.get();
    AdvLive::fillMap(*snap, source);
    AdvLive::fillState(*snap, source);
    CHECK(snap->itemCount == 32 && snap->resourceCount == 160);
    CHECK(snap->items[31].cap == 255);
    snap->active = true;
    std::vector<char> json(AdvLive::JSON_CAP);
    CHECK(AdvLive::toJson(*snap, 99, json.data(), json.size()));
}

int main()
{
    for (const char *name : {"01_collect.adv", "02_carry.adv", "03_reset.adv", "README.md", "bitmaps/stone.png"})
        CHECK(read(std::string("docs/adventures/z_resource_test/") + name) ==
              read(std::string("sdcard_bundle/otr/adventure/z_resource_test/") + name));
    play("docs/adventures");
    play("sdcard_bundle/otr/adventure");
    live();
    restoreErrors();
    errors();
    std::printf("Resources: %d checks passed\n", checks);
}
