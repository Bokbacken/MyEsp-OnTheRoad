// Real Players API + POSIX files; fault injection at the libc boundary.
// g++ -std=c++17 -Wall -Wextra -Itests/player_stubs -Imain tests/test_players_storage.cpp
//   -Wl,--wrap=fwrite,--wrap=fflush,--wrap=fsync,--wrap=fclose,--wrap=rename,--wrap=stat,--wrap=fread -o /tmp/test_players
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>
static bool mounted = true;
static std::string base;
static int failWrite, failFlush, failSync, failClose, failRename, failStat, failRead;
extern "C" {
size_t __real_fwrite(const void*,size_t,size_t,FILE*);
size_t __wrap_fwrite(const void*p,size_t s,size_t n,FILE*f) { if(failWrite) {failWrite=0; errno=ENOSPC; return __real_fwrite(p,s,n/2,f);} return __real_fwrite(p,s,n,f); }
size_t __real_fread(void*,size_t,size_t,FILE*);
size_t __wrap_fread(void*p,size_t s,size_t n,FILE*f) { if(failRead) {failRead=0; errno=EIO; return 0;} return __real_fread(p,s,n,f); }
int __real_fflush(FILE*); int __wrap_fflush(FILE*f) { int r=__real_fflush(f); if(failFlush){failFlush=0;return EOF;}return r; }
int __real_fsync(int); int __wrap_fsync(int fd) { if(failSync){failSync=0;errno=EIO;return -1;}return __real_fsync(fd); }
int __real_fclose(FILE*); int __wrap_fclose(FILE*f) {int r=__real_fclose(f);if(failClose){failClose=0;return EOF;}return r;}
int __real_rename(const char*,const char*); int __wrap_rename(const char*a,const char*b) { if((failRename<0 && (strstr(a,".tmp") || strstr(a,".bak"))) || (failRename>0 && --failRename==0)){errno=EIO;return -1;}return __real_rename(a,b); }
int __real_stat(const char*,struct stat*); int __wrap_stat(const char*p,struct stat*s) { if(failStat){failStat=0;errno=EIO;return -1;}return __real_stat(p,s); }
}
namespace MyFat {
bool mounted(){return ::mounted;}
void path(char*out,size_t n,const char*rel){snprintf(out,n,"%s%s",base.c_str(),rel);}
}
#include "../main/app_players.cpp"
static int checks;
#define CHECK(x) do { assert(x); ++checks; } while(0)
static std::string bytes(const char *suffix) {
    std::ifstream f(base+suffix,std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f),{});
}
static void restart() { free(Players::s_db); Players::s_db=nullptr; Players::s_loaded=false; Players::s_fsOk=false; }
static void fresh() {
    restart();
    for(auto p:{"/players.bin","/players.bin.bak","/players.bin.tmp","/players.tmp"}) ::remove((base+p).c_str());
}
int main() {
    char dir[]="/tmp/players-test-XXXXXX"; base=mkdtemp(dir);
    CHECK(Players::begin());
    CHECK(Players::add("   ")==int(PlayerErr::INVALID));
    CHECK(Players::add("Anna")==1);
    CHECK(Players::add(" anna ")==int(PlayerErr::DUPLICATE));
    const auto saved=bytes("/players.bin");
    restart(); CHECK(Players::begin()); CHECK(Players::findByName("Anna")==1); CHECK(Players::count()==1);
    for(int *failure:{&failWrite,&failFlush,&failSync,&failClose,&failRename}) {
        *failure=1; CHECK(Players::add("Bob")==int(PlayerErr::STORAGE));
        CHECK(Players::count()==1); CHECK(bytes("/players.bin")==saved);
    }
    failRename=2; CHECK(Players::add("Bob")==int(PlayerErr::STORAGE));
    CHECK(bytes("/players.bin")==saved); CHECK(Players::findByName("Bob")==0);
    failRename=-1; CHECK(Players::add("Bob")==int(PlayerErr::STORAGE));
    CHECK(!Players::ok()); CHECK(Players::count()==1); CHECK(bytes("/players.bin.bak")==saved);
    CHECK(Players::add("Carol")==int(PlayerErr::STORAGE));
    failRename=0; CHECK(::rename((base+"/players.bin.bak").c_str(),(base+"/players.bin").c_str())==0);
    CHECK(Players::begin()); CHECK(bytes("/players.bin")==saved);
    failWrite=1; CHECK(Players::rename(1,"Other")==int(PlayerErr::STORAGE)); CHECK(Players::findByName("Anna")==1);
    failSync=1; CHECK(Players::remove(1)==int(PlayerErr::STORAGE)); CHECK(Players::count()==1);
    failClose=1; CHECK(Players::setPin(1,"1234")==int(PlayerErr::STORAGE)); CHECK(!Players::hasPin(1));
    mounted=false; CHECK(!Players::begin()); CHECK(Players::count()==1);
    CHECK(Players::add("Bob")==int(PlayerErr::STORAGE)); CHECK(bytes("/players.bin")==saved);
    mounted=true; CHECK(Players::begin());
    failStat=1; CHECK(!Players::begin()); CHECK(Players::count()==1); CHECK(!Players::ok());
    CHECK(Players::add("Bob")==int(PlayerErr::STORAGE)); CHECK(bytes("/players.bin")==saved);
    CHECK(Players::begin()); failRead=1; CHECK(!Players::begin()); CHECK(Players::count()==1);
    CHECK(Players::begin()); CHECK(Players::add("Bob")==2);
    restart(); CHECK(Players::begin()); CHECK(Players::findByName("Bob")==2);
    // Truncated primary must not be treated as a writable empty database.
    {std::ofstream f(base+"/players.bin",std::ios::binary);f<<"broken";}
    CHECK(!Players::begin()); CHECK(Players::count()==2); // Keep last known memory on read error.
    restart(); CHECK(!Players::begin()); CHECK(Players::count()==1); // Previous committed backup.
    CHECK(Players::add("Carol")==int(PlayerErr::STORAGE)); CHECK(bytes("/players.bin")=="broken");
    // Power loss between backup rename and promotion: recover backup, never auto-overwrite it.
    ::remove((base+"/players.bin").c_str()); restart(); CHECK(!Players::begin()); CHECK(Players::findByName("Anna")==1);
    CHECK(!bytes("/players.bin.bak").empty());
    // Unreadable primary with no fallback: error, not a new empty writable DB.
    fresh(); {std::ofstream f(base+"/players.bin");f<<"bad";}
    CHECK(!Players::begin()); CHECK(Players::add("Carol")==int(PlayerErr::STORAGE)); CHECK(bytes("/players.bin")=="bad");
    // Valid old v1 remains readable; migration writes only on an explicit successful mutation.
    fresh(); PlayerDbV1 old{};old.nextId=8;old.count=1;old.items[0].id=7;strcpy(old.items[0].name,"Legacy");
    Players::Header header={Players::MAGIC,1,sizeof(PlayerRecV1),sizeof(PlayerDbV1)};
    {FILE*f=fopen((base+"/players.bin").c_str(),"wb");fwrite(&header,1,sizeof(header),f);fwrite(&old,1,sizeof(old),f);fclose(f);}
    CHECK(Players::begin()); CHECK(Players::findByName("Legacy")==7); CHECK(Players::add("New")==8);
    restart(); CHECK(Players::begin()); CHECK(Players::findByName("Legacy")==7); CHECK(Players::findByName("New")==8);
    fresh(); rmdir(base.c_str());
    printf("Spelarlagring: %d godkända kontroller\n",checks);
}
