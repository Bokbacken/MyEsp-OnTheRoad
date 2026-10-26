#include "adv_session.h"
#include <cstring>
#include <memory>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif
namespace Adventure {
namespace {
// Reuse the existing exit/ground/treasure art. No new story or Bible text.
const char *const MEADOW[] = {
 "TTTTTTTTTTTT", "T..........T", "T.S......*.T", "T.X........T", "T....b.....T",
 "T..........T", "T......k...T", "T..........T", "T..........T", "TTTTTTTTTTTT"
};
const Otr::MapDef MEADOW_DEF={4,MEADOW,10,Otr::CLASSIC_RULES,nullptr,0};
struct DeleteWorld { void operator()(Otr::World *w) const {
#ifdef ESP_PLATFORM
 heap_caps_free(w);
#else
 delete w;
#endif
}};
std::unique_ptr<Otr::World,DeleteWorld> scratch() {
#ifdef ESP_PLATFORM
 auto *p=(Otr::World*)heap_caps_malloc(sizeof(Otr::World),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
 if(p) new(p) Otr::World{};
 return std::unique_ptr<Otr::World,DeleteWorld>(p);
#else
 return std::unique_ptr<Otr::World,DeleteWorld>(new(std::nothrow) Otr::World{});
#endif
}
uint32_t get32(const uint8_t *p) { return p[0]|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24); }
void put32(uint8_t *p,uint32_t v) { for(int i=0;i<4;i++) p[i]=uint8_t(v>>(i*8)); }
constexpr uint32_t MAGIC=0x3253544f; // OTS2: atomic world + maps + story
constexpr size_t HEADER=24;
}
uint32_t saveCrc(const uint8_t *p,size_t n) {
 uint32_t c=~0u; while(n--) { c^=*p++; for(int i=0;i<8;i++) c=(c>>1)^(0xedb88320u & (0u-(c&1u))); } return ~c;
}
bool loadMap(int id,int index,int level,Otr::World &w) {
 if(id==Otr::WORLD_VILLAGE && index==1 && !level) return w.load(MEADOW_DEF);
 if(index || id<1 || id>Otr::WORLD_COUNT) return false;
 if(id==Otr::WORLD_MINES && level) return w.loadMines(level);
 if(level) return false;
 return w.load(Otr::WORLDS[id-1]);
}
void Session::clear() { worldId_=map_=completed_=0; for(auto &m:maps_) std::vector<uint8_t>().swap(m); }
bool Session::start(int id,Otr::World &w,Otr::Story::StoryState &story) {
 if(!loadMap(id,0,id==Otr::WORLD_MINES?1:0,w)) return false;
 clear(); worldId_=id; story.clear(); return true;
}
bool Session::travel(Otr::World &w) {
 if(worldId_!=Otr::WORLD_VILLAGE || !w.exitReached()) return false;
 // Only the declared portal, not an arbitrary exit added by a corrupted save.
 if(map_==0 ? (w.playerX()!=28||w.playerY()!=1) : (w.playerX()!=2||w.playerY()!=3)) return false;
 const int dest=1-map_; auto temp=scratch();
 if(!temp || !loadMap(worldId_,dest,0,*temp)) return false;
 if(!maps_[dest].empty() && !temp->restore(maps_[dest].data(),maps_[dest].size())) return false;
 temp->carryFrom(w);
 if(!temp->spawnAt(dest==1?2:29,dest==1?2:1)) return false;
 std::vector<uint8_t> old(w.saveSize());
 if(!w.save(old.data(),old.size())) return false;
 maps_[map_]=std::move(old); map_=dest; w=*temp; return true;
}
void Session::recordWin(const Otr::World &w) {
 if(w.mineLevel() && w.exitReached() && !w.gameOver() && w.mineLevel()>completed_) completed_=w.mineLevel();
}
bool Session::next(Otr::World &w) {
 if(worldId_!=Otr::WORLD_MINES || !w.mineLevel() || !w.exitReached() || w.gameOver() || w.mineLevel()>=Otr::MINE_LEVELS) return false;
 recordWin(w); return w.loadMines(w.mineLevel()+1);
}
bool Session::restart(Otr::World &w) {
 if(worldId_!=Otr::WORLD_MINES) return false;
 return w.loadMines(w.mineLevel()?w.mineLevel():1);
}
size_t Session::save(const Otr::World &w,const Otr::Story::StoryState &st,uint8_t *out,size_t cap) const {
 if(!out || worldId_<1 || worldId_>3 || map_<0 || map_>1) return 0;
 const size_t a=map_==0?w.saveSize():maps_[0].size(), b=map_==1?w.saveSize():maps_[1].size();
 const size_t n=HEADER+a+b+Otr::Story::StoryState::SAVE_BYTES+4;
 if(cap<n || n>SAVE_CAP) return 0;
 memset(out,0,HEADER); put32(out,MAGIC); out[4]=2; out[5]=worldId_; out[6]=map_; out[7]=w.mineLevel();
 out[8]=completed_; if(w.mineLevel() && w.exitReached() && !w.gameOver() && out[8]<w.mineLevel()) out[8]=w.mineLevel();
 put32(out+12,a); put32(out+16,b); put32(out+20,Otr::Story::StoryState::SAVE_BYTES);
 uint8_t *p=out+HEADER;
 for(int i=0;i<2;i++) {
  size_t len=i==0?a:b;
  if(i==map_) { if(!w.save(p,len)) return 0; }
  else if(len) memcpy(p,maps_[i].data(),len);
  p+=len;
 }
 if(!st.save(p,Otr::Story::StoryState::SAVE_BYTES)) return 0;
 put32(out+n-4,saveCrc(out,n-4)); return n;
}
bool Session::restore(const uint8_t *d,size_t n,Otr::World &w,Otr::Story::StoryState &st) {
 if(!d || n<20 || n>SAVE_CAP) return false;
 auto temp=scratch(); if(!temp) return false;
 if(get32(d)==Otr::World::SAVE_MAGIC) {
  // Legacy map saves are accepted only for the selected world's original map, never a new level.
  if(!loadMap(worldId_,0,0,*temp) || !temp->restore(d,n)) return false;
  for(auto &m:maps_) m.clear();
  map_=completed_=0; st.clear(); w=*temp; return true;
 }
 if(n<HEADER+4 || get32(d)!=MAGIC || d[4]!=2 || d[5]!=worldId_ || d[6]>1 || d[7]>12 || d[8]>12 ||
    d[9]||d[10]||d[11] || get32(d+n-4)!=saveCrc(d,n-4)) return false;
 const size_t a=get32(d+12),b=get32(d+16),sn=get32(d+20);
 if(a>SAVE_CAP || b>SAVE_CAP || sn!=Otr::Story::StoryState::SAVE_BYTES || HEADER+a+b+sn+4!=n) return false;
 if(worldId_!=Otr::WORLD_VILLAGE && (b||d[6])) return false;
 if(worldId_!=Otr::WORLD_MINES && (d[7]||d[8])) return false;
 if(worldId_==Otr::WORLD_MINES && ((d[7]==0 && d[8]!=0) || (d[7] && (d[8]+1<d[7] || d[8]>d[7])))) return false;
 if(!(d[6]?b:a)) return false;
 Otr::Story::StoryState story{}; if(!story.restore(d+HEADER+a+b,sn)) return false;
 const uint8_t *p=d+HEADER;
 for(int i=0;i<2;i++) {
  size_t len=i?b:a;
  if(len && (!loadMap(worldId_,i,d[7],*temp) || !temp->restore(p,len))) return false;
  p+=len;
 }
 if(!loadMap(worldId_,d[6],d[7],*temp) || !temp->restore(d+HEADER+(d[6]?a:0),d[6]?b:a)) return false;
 // All validation passed. Cache blobs and commit the state together.
 maps_[0].assign(d+HEADER,d+HEADER+a); maps_[1].assign(d+HEADER+a,d+HEADER+a+b);
 map_=d[6]; completed_=d[8]; w=*temp; st=story; return true;
}
}
