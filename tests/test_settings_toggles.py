#!/usr/bin/env python3
"""Exercise production Settings/status-toggle synchronization without ESP-IDF or LVGL."""
import pathlib
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
source = (ROOT / "main/ui_settings.cpp").read_text()
web = (ROOT / "main/app_web.cpp").read_text()
web_settings = (ROOT / "main/web/settings.html").read_text()
assert "MENU_SETTINGS_GAME" in source and "MENU_SETTINGS_PLAYERS" in source
assert "MENU_SETTINGS_USER" in source and "MENU_SETTINGS_ADMIN" in source
settings_home_start = source.index("\nvoid show()\n{")
settings_home = source[settings_home_start:source.index("\nstatic void showPage(Page page)\n{", settings_home_start)]
assert "Theme::button(header, LV_SYMBOL_LEFT" in settings_home
assert "UiYatzy::showMenu()" in settings_home
assert "if (page != Page::USER)" in source and "if (page != Page::GAME)" in source
assert "showPage(Page::ADMIN)" in source and "UiNumpad::MAX_PIN_LENGTH" in source
game_sudoku = source[source.index("lv_obj_t *sudoku = Theme::card"):
                     source.index("lv_obj_t *clk = Theme::card", source.index("lv_obj_t *sudoku = Theme::card"))]
assert "SUDOKU_RESET_PROGRESS" in game_sudoku
assert "UiSudoku::confirmResetProgress()" in game_sudoku
assert source.index("SCREEN_TIMEOUT"), "user screen timeout control must remain available"
assert "Theme::button(powerAdmin, tr(Str::SCREEN_SLEEP_NOW)" in source
assert "VAR_MIDI_ORDER" in source
assert "VAR_YATZY_ORDER" in source and "VAR_MAXI_ORDER" in source
assert 'getInt("default_variant", 0, 5' in web
assert '<option value="5">Midi-Yatzy i ordning</option>' in web_settings
start = source.index("bool toggleSound()")
end = source.index("void prepareForMenuNavigation()", start)
functions = source[start:end]

test = r'''
#include <cassert>
#include <cstdint>
struct AppConfig {
    bool soundOn = false, ecoSleep = false, musicOn = true;
    uint8_t sfxVol = 60, musicVol = 25;
};
namespace Config {
    AppConfig config;
    bool saveOk = true;
    AppConfig &get() { return config; }
    bool save() { return saveOk; }
}
namespace Audio {
    bool soundOn = false;
    void setVolumes(bool sound, uint8_t, bool, uint8_t) { soundOn = sound; }
}
struct lv_obj_t { bool checked = false; };
constexpr int LV_STATE_CHECKED = 1;
static AppConfig s_edit;
static lv_obj_t *s_soundSwitch = nullptr, *s_ecoSwitch = nullptr;
static void lv_obj_set_state(lv_obj_t *obj, int, bool checked) { obj->checked = checked; }
namespace UiSettings {
''' + functions + r'''
}
int main() {
    lv_obj_t soundSwitch{true}, ecoSwitch{false};
    s_soundSwitch = &soundSwitch;
    s_ecoSwitch = &ecoSwitch;
    Config::get().soundOn = s_edit.soundOn = true;
    assert(UiSettings::toggleSound());
    assert(!Config::get().soundOn && !s_edit.soundOn && !soundSwitch.checked && !Audio::soundOn);
    Config::saveOk = false;
    assert(!UiSettings::toggleSound());
    assert(!Config::get().soundOn && !s_edit.soundOn && !soundSwitch.checked);

    Config::saveOk = true;
    Config::get().ecoSleep = s_edit.ecoSleep = false;
    assert(UiSettings::togglePowerSaving());
    assert(Config::get().ecoSleep && s_edit.ecoSleep && ecoSwitch.checked);
    Config::saveOk = false;
    assert(!UiSettings::togglePowerSaving());
    assert(Config::get().ecoSleep && s_edit.ecoSleep && ecoSwitch.checked);
}
'''

with tempfile.TemporaryDirectory(prefix="settings-toggles-") as directory:
    work = pathlib.Path(directory)
    test_cpp = work / "test.cpp"
    binary = work / "test"
    test_cpp.write_text(test)
    subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                    str(test_cpp), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("Settings status toggles: persisted and failed changes keep editor switches synchronized")
