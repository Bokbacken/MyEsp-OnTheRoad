// supernova_playlists.h - Supernova playlists: ordered lists of song folder names (the folder name
// is the song identity, like the karaoke playlists). Pure C++ (host-tested).
//
// supernova.pl, UTF-8 text: "#\t<id>\t<name>" starts a playlist, each following line is a song
// folder name.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include <string>
#include <vector>

namespace SupernovaPlaylists {

constexpr size_t MAX_LISTS = 32;
constexpr size_t MAX_SONGS = 1000;   // Per playlist
constexpr size_t MAX_NAME = 48;      // UTF-8 bytes
constexpr size_t MAX_FOLDER = 255;

enum class Err : uint8_t { OK, BAD_NAME, BAD_FOLDER, NO_LIST, NO_SONG, FULL, DUPLICATE_NAME };

struct Playlist {
    uint16_t id = 0;
    std::string name;
    std::vector<std::string> folders;
};

class Store {
public:
    const std::vector<Playlist> &lists() const { return m_lists; }
    const Playlist *find(uint16_t id) const;

    Err create(const std::string &name, uint16_t &idOut);
    Err rename(uint16_t id, const std::string &name);
    Err remove(uint16_t id);
    // Adding a folder already in the list is not an error (the list is left as it was).
    Err add(uint16_t id, const std::string &folder);
    Err removeSong(uint16_t id, const std::string &folder);
    Err move(uint16_t id, const std::string &folder, int delta);   // -1 up, +1 down

    std::string encode() const;
    bool decode(const char *data, size_t len);

    static bool validName(const std::string &name);
    static bool validFolder(const std::string &folder);

private:
    Playlist *get(uint16_t id);
    bool nameTaken(const std::string &name, uint16_t except) const;
    std::vector<Playlist> m_lists;
};

}  // namespace SupernovaPlaylists
