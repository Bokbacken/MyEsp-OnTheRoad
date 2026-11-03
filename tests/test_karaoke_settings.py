"""Exercise production karaoke registry transactions and preview/save/discard semantics."""
import pathlib
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
source = (ROOT / "main/karaoke.cpp").read_text()


def function(signature):
    start = source.index(signature)
    opening = source.index("{", start)
    end, depth = opening + 1, 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


functions = [
    "Err loadRegistry()", "Midi::RegEntry currentSettings()", "bool writeRegistry(",
    "void setSongFont(", "void setTempo(", "void setTranspose(", "void setMelody(",
    "void setMelodyMuted(", "void setSongVolume(", "bool songSettingsDirty()",
    "Err saveSongSettings()", "Err discardSongSettings()", "Err songKeyByPath(",
    "Err validateSongFont(", "Err playlists(", "Err editPlaylist(", "Err songFontByPath(",
    "Err saveSongFontByPath(",
]
code = r'''
#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>
#include "otr_midi.h"
#include "kara_index.h"
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
int audioDepth=0, registryDepth=0, writes=0, reloads=0;
bool mounted=true, failWrite=false, busy=false;
struct Lock { Lock(){++audioDepth;} ~Lock(){--audioDepth;} };
struct RegistryLock { RegistryLock(){++registryDepth;} ~RegistryLock(){--registryDepth;} };
namespace BoardSd {
struct Guard { Guard(){assert(!audioDepth);} };
bool mounted(){return ::mounted;}
bool ensureDir(const char *p){return std::filesystem::is_directory(p);}
}
namespace FsUpload {
enum class InstallResult { INSTALLED, FAILED };
InstallResult install(const char *tmp,const char *dst,const char *,bool){
    assert(!audioDepth && registryDepth == 1); ++writes;
    if(failWrite) return InstallResult::FAILED;
    return std::rename(tmp,dst)==0 ? InstallResult::INSTALLED : InstallResult::FAILED;
}
void discard(const char *p){std::remove(p);}
}
namespace Karaoke {
constexpr const char *DIR_SONGS="songs", *REG_FILE="songs/karaoke.reg";
enum class Err { OK, NO_SD, FILE_BAD, SAVE_FAILED, BUSY, NO_FILE, BAD_INPUT, FILE_TOO_BIG, UNSAVED };
struct PlaylistView { uint16_t id; std::string name; size_t count; std::vector<Midi::SongKey> songs; };
struct FontFile { std::string name; Err err; };
Err scanFonts(std::vector<FontFile>& out){
    out={{"Bank with spaces.sf2",Err::OK},{"Bad.sf2",Err::FILE_BAD}}; return Err::OK;
}
struct FakeSynth {
    int tr=0;
    void setTranspose(int n){tr=n;} int transpose(){return tr;}
    bool selective(){return false;} bool loaded(){return true;}
} s_synth;
Midi::Registry s_reg;
Midi::RegEntry s_savedSettings;
Midi::Song s_song;
Midi::Player s_player;
bool s_regLoaded=false,s_open=false,s_melodyReg=false,s_melodyMuted=false;
uint32_t s_crc=123,s_size=456,s_songGen=0;
std::string s_fileName="Test.kar",s_songSf2;
struct { std::string path="songs/Test.kar"; } s_cur;
int s_pendingTr=0,s_melody=3,s_prevTr=0;
bool s_trReload=false,s_resumePlay=false;
uint8_t s_songVolume=100;
bool synthBusy(){return busy;}
void applyMelodyMute(){}
void startTask(bool){}
void ensureFont(){++reloads;}
@FUNCTIONS@
}
void put(const char *path,const std::string &text){
    FILE *f=fopen(path,"wb"); assert(f);
    assert(fwrite(text.data(),1,text.size(),f)==text.size()); assert(!fclose(f));
}
std::string read(const char *path){
    FILE *f=fopen(path,"rb"); assert(f);
    std::string out; char buf[1024]; size_t n;
    while((n=fread(buf,1,sizeof(buf),f))) out.append(buf,n);
    fclose(f); return out;
}
int main(){
    using namespace Karaoke;
    std::filesystem::create_directory("songs");
    {RegistryLock l; assert(loadRegistry()==Err::OK);}
    s_open=true; s_song.melody=3;
    s_savedSettings=currentSettings();
    assert(!songSettingsDirty());
    setTempo(120); setTranspose(2); setMelody(5); setMelodyMuted(true); setSongVolume(160);
    setSongFont("Bank with spaces.sf2");
    assert(songSettingsDirty() && writes==0 && s_reg.size()==0);
    busy=true; assert(saveSongSettings()==Err::BUSY); busy=false;
    failWrite=true; assert(saveSongSettings()==Err::SAVE_FAILED);
    assert(songSettingsDirty() && s_reg.size()==0);
    failWrite=false; assert(saveSongSettings()==Err::OK);
    assert(!songSettingsDirty() && s_reg.size()==1);
    const std::string saved=read(REG_FILE);
    setTempo(90); setSongVolume(40); setSongFont("");
    const int before=writes;
    assert(discardSongSettings()==Err::OK && writes==before);
    assert(!songSettingsDirty() && s_player.tempo()==120 && s_songVolume==160);
    assert(s_songSf2=="Bank with spaces.sf2");
    uint16_t id=0;
    assert(editPlaylist("create",id,"Road trip","",0,0)==Err::OK && id);
    assert(editPlaylist("add",id,"","../outside.kar",0,0)==Err::BAD_INPUT);
    put("songs/Test.kar","123456789");
    Midi::SongKey key; assert(songKeyByPath("Test.kar",key)==Err::OK);
    assert(key.crc==0xcbf43926 && key.size==9);
    assert(editPlaylist("add",id,"","Test.kar",0,0)==Err::OK);
    std::vector<PlaylistView> lists;
    assert(playlists(lists,0,0)==Err::OK && lists.size()==1 && lists[0].songs.empty() && lists[0].count==1);
    assert(playlists(lists,id,0)==Err::OK && lists[0].songs[0].path=="Test.kar");
    assert(playlists(lists,id,100)==Err::OK && lists[0].songs.empty() && lists[0].count==1);
    failWrite=true;
    assert(editPlaylist("delete",id,"","",0,0)==Err::SAVE_FAILED);
    assert(playlists(lists,0,0)==Err::OK && lists.size()==1);
    failWrite=false;
    s_open=false;
    Midi::RegEntry e; e.crc=key.crc; e.size=key.size; e.melody=8; e.tempo=110; e.volume=180;
    s_reg.set(e);
    assert(saveSongFontByPath("Test.kar","../Bad.sf2")==Err::BAD_INPUT);
    assert(saveSongFontByPath("Test.kar","Bad.sf2")==Err::BAD_INPUT);
    assert(saveSongFontByPath("Test.kar","Bank with spaces.sf2")==Err::OK);
    assert(s_reg.find(key.crc,key.size)->melody==8 && s_reg.find(key.crc,key.size)->tempo==110);
    assert(s_reg.find(key.crc,key.size)->volume==180);
    std::string font;
    assert(songFontByPath("Test.kar",font)==Err::OK && font=="Bank with spaces.sf2");
    assert(saveSongFontByPath("Test.kar","")==Err::OK);
    s_crc=key.crc; s_size=key.size; s_open=true; s_savedSettings=currentSettings();
    setTempo(135);
    assert(saveSongFontByPath("Test.kar","Bank with spaces.sf2")==Err::UNSAVED);
    assert(discardSongSettings()==Err::OK);
    assert(saveSongFontByPath("Test.kar","Bank with spaces.sf2")==Err::OK && !songSettingsDirty());
    s_open=false;
    const uint8_t midi[] = {
        'M','T','h','d',0,0,0,6,0,0,0,1,0,96,
        'M','T','r','k',0,0,0,22,
        0,0xff,3,6,'M','e','l','o','d','y',
        0,0x93,60,100,96,0x83,60,0,0,0xff,0x2f,0
    };
    put("songs/New.kar",std::string((const char*)midi,sizeof(midi)));
    assert(saveSongFontByPath("New.kar","Bank with spaces.sf2")==Err::OK);
    assert(songKeyByPath("New.kar",key)==Err::OK);
    const auto *first=s_reg.find(key.crc,key.size);
    assert(first && first->melody==3 && first->tempo==100 && first->volume==100);
    put("songs/Invalid.kar","not MIDI");
    assert(saveSongFontByPath("Invalid.kar","Bank with spaces.sf2")==Err::FILE_BAD);
    // CRC is incremental even for songs larger than the fixed read buffer.
    const std::string large(4097,'x'); put("songs/Large.kar",large);
    assert(songKeyByPath("Large.kar",key)==Err::OK);
    assert(key.crc==Midi::crc32((const uint8_t*)large.data(),large.size()));
    assert(editPlaylist("delete",id,"","",0,0)==Err::OK);
    s_regLoaded=false; s_reg=Midi::Registry{};
    {RegistryLock l; assert(loadRegistry()==Err::OK);}
    assert(s_reg.playlists().empty() && s_reg.size()==3);
    mounted=false;
    assert(saveSongFontByPath("Test.kar","")==Err::NO_SD);
    mounted=true;
    s_regLoaded=false; put(REG_FILE,std::string(256*1024+1,'x'));
    {RegistryLock l; assert(loadRegistry()==Err::FILE_BAD && !s_regLoaded);}
    puts("Karaoke settings: previews, save/discard, write rollback, playlist paging and song fonts passed");
}
'''.replace("@FUNCTIONS@", "\n".join(function(sig) for sig in functions))
assert "updateRegistry(" not in source, "No automatic register writes may remain"
with tempfile.TemporaryDirectory() as directory:
    work = pathlib.Path(directory)
    (work / "settings.cpp").write_text(code)
    subprocess.run([
        "g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
        "-I" + str(ROOT / "main"), "-I" + str(ROOT / "components/otr_midi/include"),
        str(work / "settings.cpp"), str(ROOT / "components/otr_midi/otr_midi.cpp"),
        str(ROOT / "components/otr_midi/otr_midi_gm.cpp"),
        str(ROOT / "main/kara_index.cpp"), "-o", str(work / "settings")
    ], check=True)
    subprocess.run([str(work / "settings")], cwd=work, check=True)
