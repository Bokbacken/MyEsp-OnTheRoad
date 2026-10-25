// Host tests for main/files_page.h (v0.13.7 file manager sorting/pagination)
#include "check.h"
#include "files_page.h"

#include <string>

using namespace FilesPage;

static std::string g_pool;

static Item mk(const char *name, bool dir, uint64_t size, int64_t mtime)
{
    Item i;
    i.name = (uint32_t)g_pool.size();
    g_pool.append(name);
    g_pool += '\0';
    i.dir = dir;
    i.size = size;
    i.mtime = mtime;
    return i;
}

static const char *nm(const Item &i) { return g_pool.c_str() + i.name; }

static std::string names(const std::vector<Item> &v)
{
    std::string s;
    for (const Item &i : v) s += std::string(nm(i)) + ",";
    return s;
}

int main()
{
    std::vector<Item> v = {
        mk("b.mid", false, 300, 10), mk("Songs", true, 0, 5), mk("a.MID", false, 100, 30),
        mk("c.kar", false, 200, 20), mk("albums", true, 0, 50), mk("B.mid", false, 300, 10),
    };
    sortItems(v, g_pool.c_str(), Sort::NAME, false);
    CHECK(names(v) == "albums,Songs,a.MID,B.mid,b.mid,c.kar,");
    sortItems(v, g_pool.c_str(), Sort::NAME, true);
    CHECK(names(v) == "Songs,albums,c.kar,b.mid,B.mid,a.MID,");
    sortItems(v, g_pool.c_str(), Sort::SIZE, false);
    CHECK(names(v) == "albums,Songs,a.MID,c.kar,B.mid,b.mid,");
    sortItems(v, g_pool.c_str(), Sort::SIZE, true);
    CHECK(v[0].dir && v[1].dir && !strcmp(nm(v[2]), "b.mid") && !strcmp(nm(v[5]), "a.MID"));
    sortItems(v, g_pool.c_str(), Sort::MTIME, false);
    CHECK(names(v) == "Songs,albums,B.mid,b.mid,c.kar,a.MID,");
    sortItems(v, g_pool.c_str(), Sort::MTIME, true);
    CHECK(names(v) == "albums,Songs,a.MID,c.kar,b.mid,B.mid,");
    std::vector<Item> empty;
    sortItems(empty, g_pool.c_str(), Sort::NAME, false);
    CHECK(empty.empty());

    CHECK(parseSort("size") == Sort::SIZE && parseSort("mtime") == Sort::MTIME && parseSort("date") == Sort::MTIME);
    CHECK(parseSort("name") == Sort::NAME && parseSort(nullptr) == Sort::NAME && parseSort("x") == Sort::NAME);

    Page p = pageOf(0, 1, 50);
    CHECK(p.pages == 1 && p.page == 1 && p.first == 0 && p.count == 0);
    p = pageOf(120, 1, 50);
    CHECK(p.pages == 3 && p.first == 0 && p.count == 50);
    p = pageOf(120, 3, 50);
    CHECK(p.page == 3 && p.first == 100 && p.count == 20);
    p = pageOf(120, 9, 50);
    CHECK(p.page == 3 && p.count == 20);
    p = pageOf(120, -2, 50);
    CHECK(p.page == 1 && p.first == 0);
    p = pageOf(100, 2, 50);
    CHECK(p.pages == 2 && p.count == 50 && p.first == 50);
    p = pageOf(5, 1, 0);
    CHECK(p.pages == 5 && p.count == 1);
    p = pageOf(-3, 1, 10);
    CHECK(p.pages == 1 && p.count == 0);

    CHECK(isInside("/sdcard/a/b", "/sdcard/a"));
    CHECK(isInside("/sdcard/a", "/sdcard/a"));
    CHECK(!isInside("/sdcard/ab", "/sdcard/a"));
    CHECK(isInside("/sdcard/A/x", "/sdcard/a"));
    CHECK(!isInside("/sdcard", "/sdcard/a"));
    CHECK(isInside("/sdcard/x", "/sdcard/"));

    CHECK(!strcmp(baseName("/a/b/c.mid"), "c.mid") && !strcmp(baseName("c"), "c") && !strcmp(baseName("/a/"), ""));
    CHECK_DONE();
}
