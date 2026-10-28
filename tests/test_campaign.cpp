// Host regressions: campaign rules, map travel and atomic v2 / legacy saves.
#include "adv_session.h"
#include "otr_stories.h"
#include "adv_live.h"
#include "check.h"
#include <cstring>
#include <queue>
#include <vector>
#include <cstdio>
using namespace Otr;
static World w, other;
static Story::StoryState st;
static Adventure::Session session;
static uint8_t buffer[Adventure::Session::SAVE_CAP], backup[Adventure::Session::SAVE_CAP];
static void crc(uint8_t *p,size_t n) {
 uint32_t c=Adventure::saveCrc(p,n-4); for(int i=0;i<4;i++) p[n-4+i]=(uint8_t)(c>>(i*8));
}
// Player-driven safe path, not teleporting or bypassing move/pickup/win logic.
static bool walk(int tx,int ty) {
 int prev[MAX_W*MAX_H], dirs[MAX_W*MAX_H];
 for(int &p:prev) p=-1;
 std::queue<int> q; int start=w.playerY()*MAX_W+w.playerX(),end=ty*MAX_W+tx;
 prev[start]=start; q.push(start);
 while(!q.empty() && prev[end]<0) {
  int cell=q.front(); q.pop();
  for(int d=0;d<4;d++) {
   int x=cell%MAX_W+dirX((Dir)d),y=cell/MAX_W+dirY((Dir)d),i=y*MAX_W+x;
   if(!w.inside(x,y)||!w.playerCanEnter(x,y)||w.rawObj(x,y)==Obj::MINE||w.flagged(x,y)||prev[i]>=0) continue;
   prev[i]=cell; dirs[i]=d; q.push(i);
  }
 }
 if(prev[end]<0) return false;
 std::vector<int> path;
 for(int i=end;i!=start;i=prev[i]) path.push_back(dirs[i]);
 for(auto i=path.rbegin();i!=path.rend();++i) if(!(w.move((Dir)*i)&EV_MOVED)) return w.exitReached()&&!w.gameOver();
 return true;
}
static void supplies() {
 CHECK(walk(4,1)); CHECK(w.suitCharges()==1); CHECK(w.bombs(3)==1); CHECK(w.bombs(4)==1);
}
static void solve() {
 for(int y=1;y<w.height()-1&&!w.exitReached();y++) for(int x=1;x<w.width()-1&&!w.exitReached();x++) {
  if(w.rawObj(x,y)==Obj::MINE || w.revealed(x,y)) continue;
  if(w.flagged(x,y)) CHECK(w.toggleFlag(x,y));
  CHECK(walk(x,y));
 }
 CHECK(w.exitReached()); CHECK(!w.gameOver()); CHECK(w.safeLeft()==0);
}
static void testLevels() {
 CHECK(session.start(WORLD_MINES,w,st));
 CHECK(!w.loadMines(0)); CHECK(!w.loadMines(13)); CHECK(!session.next(w));
 double lastDensity=0;
 for(int level=1;level<=MINE_LEVELS;level++) {
  CHECK(w.mineLevel()==level);
  const auto c=MINE_LEVEL[level-1]; int mines=0;
  CHECK(w.width()==c.w&&w.height()==c.h);
  for(int y=0;y<w.height();y++) for(int x=0;x<w.width();x++) if(w.rawObj(x,y)==Obj::MINE) {
   mines++; CHECK(x%2==0 && y%2==0); CHECK(!w.revealed(x,y));
  }
  CHECK(mines==c.mines);
  double density=double(mines)/((c.w-2)*(c.h-2)); CHECK(density>lastDensity); lastDensity=density;
  CHECK(w.revealed(2,1)&&w.revealed(3,1)&&w.revealed(4,1));
  CHECK(w.rawObj(2,1)==Obj::SUIT && w.rawObj(3,1)==Obj::BOMB3 && w.rawObj(4,1)==Obj::BOMB4);
  supplies();
  // Deterministic generation and unchanged start after restart.
  CHECK(other.loadMines(level));
  for(int y=2;y<w.height()-1;y++) for(int x=1;x<w.width()-1;x++) CHECK(w.rawObj(x,y)==other.rawObj(x,y));
  solve(); session.recordWin(w); CHECK(session.completed()==level);
  size_t n=session.save(w,st,buffer,sizeof(buffer)); CHECK(n>0);
  Adventure::Session restored; Story::StoryState rst{};
  CHECK(restored.start(WORLD_MINES,other,rst)); CHECK(restored.restore(buffer,n,other,rst));
  CHECK(other.exitReached()&&other.mineLevel()==level&&restored.completed()==level);
  CHECK(!(w.move(Dir::LEFT)&EV_MOVED));
  if(level<12) CHECK(session.next(w)); else CHECK(!session.next(w));
 }
 CHECK(session.restart(w)); CHECK(w.mineLevel()==12&&!w.exitReached()&&!w.gameOver()); CHECK(session.completed()==12);
}
static void testShieldLoss() {
 CHECK(session.start(WORLD_MINES,w,st)); supplies();
 int hits=0;
 for(int y=2;y<w.height()-1 && hits<2;y++) for(int x=2;x<w.width()-1 && hits<2;x++) if(w.rawObj(x,y)==Obj::MINE) {
  CHECK(walk(x-1,y)); uint32_t ev=w.move(Dir::RIGHT); ++hits;
  CHECK(ev&EV_MINE); CHECK(w.rawObj(x,y)==Obj::NONE); CHECK(w.tile(x,y)==Tile::CRATER);
  if(hits==1) { CHECK(ev&EV_SUIT_USED); CHECK(!(ev&EV_HURT)); CHECK(w.energy()==1&&w.suitCharges()==0&&!w.gameOver()); }
  else { CHECK(ev&EV_GAME_OVER); CHECK(w.gameOver()&&w.livesLeft()==0); CHECK(!w.exitReached()); }
  size_t n=session.save(w,st,buffer,sizeof(buffer)); CHECK(n>0);
  CHECK(session.restore(buffer,n,w,st)); CHECK(w.suitCharges()==0);
 }
 CHECK(hits==2); CHECK(!w.placeBomb(3,5,5)); CHECK(!session.next(w));
 CHECK(session.restart(w)); CHECK(w.mineLevel()==1&&w.energy()==1&&w.suitCharges()==0&&!w.gameOver());
}
static void testBombs() {
 for(int size:{3,4}) for(int loc=0;loc<5;loc++) {
  CHECK(session.start(WORLD_MINES,w,st)); CHECK(w.loadMines(12)); supplies();
  int x=loc==0?16:loc==1||loc==3?0:w.width()-1;
  int y=loc==0?12:loc==1||loc==2?0:w.height()-1;
  auto area=w.bombArea(size,x,y);
  CHECK(area.x0==(x>0?x-1:0)); CHECK(area.y0==(y>0?y-1:0));
  CHECK(area.x1<=w.width()-1&&area.y1<=w.height()-1);
  size_t n=w.save(buffer,sizeof(buffer));
  (void)w.bombArea(size,x,y); CHECK(w.save(backup,sizeof(backup))==n); CHECK(!memcmp(buffer,backup,n)); // Preview/cancel inert
  CHECK(!w.placeBomb(2,x,y)); CHECK(!w.placeBomb(size,-1,y)); CHECK(w.bombs(size)==1);
  other=w;
  bool useful=false;
  for(int cy=area.y0;cy<=area.y1;cy++) for(int cx=area.x0;cx<=area.x1;cx++) {
   if(w.tile(cx,cy)==Tile::TREE) continue;
   if(!w.revealed(cx,cy)||w.rawObj(cx,cy)==Obj::MINE) useful=true;
   if(!w.revealed(cx,cy)) w.toggleFlag(cx,cy);
  }
  CHECK(w.placeBomb(size,x,y)==useful); CHECK(w.bombs(size)==(useful?0:1));
  for(int cy=0;cy<w.height();cy++) for(int cx=0;cx<w.width();cx++) {
   const bool in=cx>=area.x0&&cx<=area.x1&&cy>=area.y0&&cy<=area.y1;
   if(in&&w.tile(cx,cy)!=Tile::TREE&&useful) { CHECK(w.rawObj(cx,cy)!=Obj::MINE); CHECK(w.revealed(cx,cy)); CHECK(!w.flagged(cx,cy)); }
   else { CHECK(w.rawObj(cx,cy)==other.rawObj(cx,cy)); CHECK(w.revealed(cx,cy)==other.revealed(cx,cy)); }
   int hint=0; for(int dy=-1;dy<=1;dy++) for(int dx=-1;dx<=1;dx++) if((dx||dy)&&w.rawObj(cx+dx,cy+dy)==Obj::MINE) ++hint;
   CHECK(w.mineHint(cx,cy)==hint);
  }
  if(useful) CHECK(!w.placeBomb(size,x,y));
  CHECK(other.loadMines(12)); n=w.save(buffer,sizeof(buffer)); CHECK(other.restore(buffer,n));
  CHECK(other.bombs(size)==w.bombs(size));
 }
 CHECK(session.start(WORLD_MINES,w,st)); supplies();
 CHECK(!w.placeBomb(3,0,0)); CHECK(w.bombs(3)==1); // Entire clipped region already clear
 // Flagged safe cells do not satisfy victory and prevent walking until unflagged.
 int fx=-1,fy=-1;
 for(int y=1;y<w.height()-1&&fx<0;y++) for(int x=1;x<w.width()-1;x++) if(!w.revealed(x,y)&&w.rawObj(x,y)!=Obj::MINE) {fx=x;fy=y;break;}
 CHECK(fx>=0); CHECK(w.toggleFlag(fx,fy)); CHECK(!walk(fx,fy)); CHECK(w.safeLeft()>0);
 CHECK(w.toggleFlag(fx,fy)); solve();
}
static void testMapsSaves() {
 CHECK(session.start(WORLD_VILLAGE,w,st)); st.items[0]=2; st.quests[0]=1; st.setFlag(3,true); st.counters[0]=7;
 CHECK(!w.spawnAt(0,0)); CHECK(!w.spawnAt(8,12)); CHECK(!w.spawnAt(28,1));
 CHECK(w.spawnAt(29,1)); w.addKeys(2); CHECK(w.move(Dir::LEFT)&EV_EXIT); CHECK(session.travel(w));
 CHECK(session.mapIndex()==1&&w.mapId()==4&&w.playerX()==2&&w.playerY()==2); CHECK(!session.travel(w));
 CHECK(w.keys()==2&&st.items[0]==2&&st.flag(3));
 CHECK(walk(7,6)); CHECK(w.keys()==3); // Meadow key, must not respawn on return
 size_t n=session.save(w,st,buffer,sizeof(buffer)); CHECK(n>0);
 CHECK(session.start(WORLD_VILLAGE,other,st)); CHECK(session.restore(buffer,n,other,st)); w=other;
 CHECK(st.items[0]==2&&st.quests[0]==1&&st.flag(3)&&st.counters[0]==7);
 CHECK(session.mapIndex()==1&&w.keys()==3); CHECK(walk(2,3)); CHECK(session.travel(w));
 CHECK(session.mapIndex()==0&&w.playerX()==29&&w.playerY()==1&&w.keys()==3); CHECK(!session.travel(w));
 CHECK(w.move(Dir::LEFT)&EV_EXIT); CHECK(session.travel(w)); CHECK(w.rawObj(7,6)==Obj::NONE);
 // Corruption and unsupported versions: unchanged state, even after CRC is repaired.
 n=session.save(w,st,buffer,sizeof(buffer));
 for(int k=0;k<6;k++) {
  memcpy(backup,buffer,n);
  if(k==0) backup[4]=3;
  if(k==1) backup[6]=9;
  if(k==2) backup[12]=255,backup[13]=255;
  if(k==3) backup[5]=1;
  if(k==4) backup[24+7]=63; // bad saved map width
  if(k==5) backup[n-8]^=1; // nested story CRC fails
  crc(backup,n);
  CHECK(!session.restore(backup,n,w,st)); CHECK(session.mapIndex()==1&&w.keys()==3&&st.items[0]==2);
 }
 CHECK(!session.restore(buffer,n-1,w,st)); CHECK(!session.restore(nullptr,n,w,st));
 // Actual v1-v3 layouts: omit the v4/v5 extensions, repair body length, and clear bytes
 // that were reserved in that version before calculating the old-size CRC.
 for(int id=1;id<=3;id++) for(int version=1;version<=3;version++) {
  CHECK(session.start(id,w,st)); CHECK(w.load(WORLDS[id-1]));
  w.addKeys(5); w.setFlashlight(2); w.setPogo(3); n=w.save(buffer,sizeof(buffer));
  const size_t legacySize=16+32+7*size_t(w.width()*w.height())+4;
  CHECK(n==legacySize+87); n=legacySize;
  buffer[4]=(uint8_t)version; buffer[5]=0;
  const uint32_t body=(uint32_t)(n-20);
  for(int i=0;i<4;i++) buffer[10+i]=(uint8_t)(body>>(8*i));
  const size_t usedPlayerBytes=version==1?23:version==2?26:28;
  memset(buffer+16+usedPlayerBytes,0,32-usedPlayerBytes); crc(buffer,n);
  CHECK(session.restore(buffer,n,w,st)); CHECK(w.keys()==5&&w.mapId()==id&&w.mineLevel()==0);
  CHECK(w.flashlight()==(version>=3?2:0)&&w.pogo()==(version>=3?3:0));
  n=session.save(w,st,buffer,sizeof(buffer)); CHECK(n>0); CHECK(session.restore(buffer,n,w,st)); CHECK(w.keys()==5);
  if(id==WORLD_MINES) {CHECK(session.restart(w));CHECK(w.mineLevel()==1);}
 }
}
static void testLive() {
 CHECK(session.start(WORLD_MINES,w,st)); supplies();
 static AdvLive::Snapshot snap{}; static char json[AdvLive::JSON_CAP];
 AdvLive::Source src{&w,"Minfält",nullptr,&st,nullptr,0,"%s","OK","Hej",0,2,0,4,5,5};
 AdvLive::fillMap(snap,src); AdvLive::fillState(snap,src); snap.active=true; snap.mapRev=1;
 CHECK(AdvLive::toJson(snap,0,json,sizeof(json))>0);
 CHECK(strstr(json,"\"mine_level\":1")&&strstr(json,"\"bomb4\":1")&&strstr(json,"\"bomb_preview\":[4,5,5]"));
 for(int y=0;y<w.height();y++) for(int x=0;x<w.width();x++) if(w.rawObj(x,y)==Obj::MINE) {
  CHECK(snap.terrain[y][x]=='?'); CHECK(snap.obj[y][x]=='.');
 }
}
int main() { testLevels(); testShieldLoss(); testBombs(); testMapsSaves(); testLive(); CHECK_DONE(); }
