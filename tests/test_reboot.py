"""Exercise the shared restart task used by web and local settings."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "components/my_web/my_web.cpp").read_text()
body = "bool rebootSoon" + source.split("bool rebootSoon", 1)[1].split("// ----------------------------------------------------------------------------------------", 1)[0]
stub = r'''
#include <atomic>
#include <cassert>
#include <stdexcept>
using BaseType_t=int;
constexpr int pdPASS=1;
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define pdMS_TO_TICKS(ms) (ms)
bool busy=false, failCreate=false;
int tasks=0, waits=0, hooks=0;
void (*task)(void *)=nullptr;
struct Config { bool (*canReboot)(); void (*onReboot)(); };
Config s_cfg{[] { return !busy; }, [] { ++hooks; }};
void vTaskDelay(int ms) {
    ++waits;
    if (ms==1500) busy=true; // Transfer began after request.
    if (ms==100) busy=false;
}
int xTaskCreate(void (*fn)(void *), const char *, int, void *, int, void *) {
    ++tasks; if(failCreate) return 0; task=fn; return pdPASS;
}
namespace MyLog { void event(const char *, const char *) {} }
void esp_restart() { throw std::runtime_error("restart"); }
'''
test = r'''
int main() {
    busy=true;
    assert(!rebootSoon(1500) && tasks==0);
    busy=false; failCreate=true;
    assert(!rebootSoon(1500) && tasks==1);
    failCreate=false;
    assert(rebootSoon(1500) && tasks==2);
    assert(rebootSoon(20) && tasks==2); // One task, original delay retained.
    try { task(nullptr); assert(false); } catch(const std::runtime_error &) {}
    assert(waits==3 && hooks==1 && !busy);
}
'''
with tempfile.TemporaryDirectory(prefix="otr-reboot-") as tmp:
    path = Path(tmp)
    (path / "test.cpp").write_text(stub + body + test)
    subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                    str(path / "test.cpp"), "-o", str(path / "test")], check=True)
    subprocess.run([str(path / "test")], check=True)
ui = (ROOT / "main/ui_settings.cpp").read_text()
assert "lv_msgbox_add_text(box, tr(Str::SET_REBOOT_Q))" in ui
assert "MyOta::busy() || AppWebFiles::busy()" in ui
assert "if (!MyWeb::rebootSoon())" in ui
print("Shared restart: busy guard, creation failure, duplicate requests, delayed busy wait passed")
