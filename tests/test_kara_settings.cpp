#include <assert.h>
#include <string.h>

#include <string>

#include "kara_settings.h"
#include "otr_midi.h"

int main()
{
    KaraSettings::Song song;
    song.soundfont = "Weeds GM3.sf2";
    song.midiVolume = 150;
    song.melodyChannel = 3;
    song.melodyMuted = true;
    song.melodyGainPct = 75;
    song.tempo = 120;
    song.transpose = -2;
    song.instrumentMaps = {{3, 62, 13}, {3, 111, 20}, {0, 0, 127}};
    std::string text;
    assert(KaraSettings::serialize(song, text) == KaraSettings::Err::OK);
    KaraSettings::Song parsed;
    assert(KaraSettings::parse(text.data(), text.size(), parsed) == KaraSettings::Err::OK);
    assert(parsed.soundfont == song.soundfont);
    assert(parsed.midiVolume == song.midiVolume && parsed.melodyChannel == song.melodyChannel);
    assert(parsed.melodyMuted == song.melodyMuted && parsed.melodyGainPct == song.melodyGainPct);
    assert(parsed.tempo == song.tempo && parsed.transpose == song.transpose);
    assert(parsed.instrumentMaps.size() == song.instrumentMaps.size());
    for (size_t i = 0; i < song.instrumentMaps.size(); i++) {
        assert(parsed.instrumentMaps[i].channel == song.instrumentMaps[i].channel);
        assert(parsed.instrumentMaps[i].sourceProgram == song.instrumentMaps[i].sourceProgram);
        assert(parsed.instrumentMaps[i].replacementProgram == song.instrumentMaps[i].replacementProgram);
    }

    const char defaults[] = "# user file\nversion=1\n";
    assert(KaraSettings::parse(defaults, sizeof(defaults) - 1, parsed) == KaraSettings::Err::OK);
    assert(parsed.melodyChannel == -2 && parsed.midiVolume == 100 && parsed.melodyGainPct == 100);
    assert(parsed.tempo == 100 && parsed.transpose == 0 && parsed.instrumentMaps.empty());
    assert(KaraSettings::serialize(parsed, text) == KaraSettings::Err::OK);
    assert(text.find("soundfont=") == std::string::npos);
    assert(text.find("melody_channel=") == std::string::npos);
    assert(text.find("melody_gain_pct=") == std::string::npos);

    const char *invalid[] = {
        "version=2\n",
        "version=1\nmidi_volume=201\n",
        "version=1\nmelody_channel=0\n",
        "version=1\nmelody_gain_pct=-1\n",
        "version=1\ninstrument_map=4,63,14\ninstrument_map=4,63,15\n",
        "version=1\nsoundfont=\"../outside.sf2\"\n",
        "version=1\nunknown=1\n",
        "version=1\nmelody_muted=yes\n",
        "version=1\ntranspose=13\n",
    };
    for (const char *candidate : invalid)
        assert(KaraSettings::parse(candidate, strlen(candidate), parsed) == KaraSettings::Err::INVALID);
    std::string oversized(KaraSettings::MAX_FILE_SIZE + 1, 'x');
    assert(KaraSettings::parse(oversized.data(), oversized.size(), parsed) == KaraSettings::Err::TOO_BIG);
    assert(!KaraSettings::validSoundfont("folder/Bank.sf2"));
    assert(!KaraSettings::validSoundfont("Bank.sf3"));
    assert(KaraSettings::validSoundfont("Bank.sf2"));

    Midi::Registry registry;
    Midi::RegEntry legacy;
    legacy.crc = 1234;
    legacy.size = 5678;
    legacy.melody = 2;
    registry.set(legacy);
    const uint16_t playlist = registry.createPlaylist("Road trip");
    assert(playlist != 0);
    assert(registry.setPlaylistSong(playlist, legacy.crc, legacy.size, true, "Song.mid"));
    assert(registry.setFavorite(1, legacy.crc, legacy.size, true));
    registry.clearSongSettings();
    assert(registry.size() == 0);
    assert(registry.playlists().size() == 1 && registry.playlists()[0].songs.size() == 1);
    assert(registry.isFavorite(1, legacy.crc, legacy.size));
    Midi::Registry restored;
    restored.load(registry.save());
    assert(restored.size() == 0 && restored.playlists().size() == 1);
    assert(restored.playlists()[0].songs[0].path == "Song.mid" &&
           restored.isFavorite(1, legacy.crc, legacy.size));
}
