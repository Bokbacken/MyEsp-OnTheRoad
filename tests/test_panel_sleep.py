"""Exercise the production P4 panel transition and flush bodies with DBI doubles."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "main/board_display_p4.cpp").read_text()
power = "bool setPanelSleeping" + source.split("bool setPanelSleeping", 1)[1].split("void fillScreen", 1)[0]
flush = "void flush" + source.split("void flush", 1)[1].split("bool fail", 1)[0]
stub = r'''
#include <atomic>
#include <cassert>
#include <vector>
using esp_err_t=int;
constexpr int ESP_OK=0;
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define pdMS_TO_TICKS(ms) (ms)
bool g_panelOk=true, g_ok=true;
std::atomic<bool> g_panelSleeping{false};
void *g_io=nullptr, *g_panel=nullptr;
std::vector<int> commands;
int failCommand=-1, waits=0, redraws=0, flushes=0;
int esp_lcd_panel_io_tx_param(void *, int cmd, const void *, size_t) {
    commands.push_back(cmd); return cmd==failCommand ? -1 : ESP_OK;
}
void vTaskDelay(int ms) { assert(ms==120); ++waits; }
void *lv_screen_active() { return nullptr; }
void *lv_layer_top() { return nullptr; }
void lv_obj_invalidate(void *) { ++redraws; }
void esp_lcd_panel_draw_bitmap(void *, int,int,int,int,const void *) { ++flushes; }
'''
test = r'''
int main() {
    assert(setPanelSleeping(true));
    assert((commands==std::vector<int>{0x28,0x10}) && waits==1 && g_panelSleeping);
    flush(0,0,3,3,nullptr,false); assert(flushes==0);
    assert(setPanelSleeping(true) && commands.size()==2);
    assert(setPanelSleeping(false));
    assert((commands==std::vector<int>{0x28,0x10,0x11,0x29}));
    assert(waits==2 && redraws==2 && !g_panelSleeping);
    flush(0,0,3,3,nullptr,false); assert(flushes==1);
    failCommand=0x28;
    assert(!setPanelSleeping(true) && !g_panelSleeping);
    failCommand=0x10;
    assert(!setPanelSleeping(true) && g_panelSleeping);
    failCommand=0x11;
    assert(!setPanelSleeping(false) && g_panelSleeping);
    failCommand=0x29;
    assert(!setPanelSleeping(false) && g_panelSleeping);
    flush(0,0,3,3,nullptr,false); assert(flushes==1);
    failCommand=-1;
    assert(setPanelSleeping(false) && !g_panelSleeping);
    g_panelOk=false;
    const auto count=commands.size();
    assert(!setPanelSleeping(true) && commands.size()==count);
}
'''
with tempfile.TemporaryDirectory(prefix="otr-panel-sleep-") as tmp:
    path = Path(tmp)
    (path / "test.cpp").write_text(stub + power + flush + test)
    subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                    str(path / "test.cpp"), "-o", str(path / "test")], check=True)
    subprocess.run([str(path / "test")], check=True)
print("P4 panel sleep/wake: command order, delays, redraw, no sleeping flush and failures passed")
