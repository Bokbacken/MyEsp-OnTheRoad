// kara_songs_web.h - v0.13.7 web song picker API (no PIN, like the web karaoke page):
//   GET  /api/karaoke/songs?dir=<folder>&q=<search>&all=1&offset=&limit=
//        -> {ok, scanning, error, total, dir, dirs:[{name,path,count}], count, offset,
//            songs:[{path,title,artist,size,dir}], current, playing}; all=1 suppresses dirs.
//        Folder views contain only songs directly in that folder; q searches all folders.
//        path = relative to /sdcard/otr/karaoke.
//   POST /api/karaoke/open   {"path":"Pop/ABBA_Waterloo.kar"} -> {"ok":true} (opens + plays)
//   POST /api/karaoke/rescan -> {"ok":true} (scan runs in the background; poll songs)
#pragma once

namespace KaraSongsWeb {

bool registerRoutes();

}  // namespace KaraSongsWeb
