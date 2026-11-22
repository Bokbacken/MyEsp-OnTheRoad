// supernova_web.h - web API for the Supernova song library and playlists (no PIN, like the
// karaoke song picker):
//   GET  /api/supernova/songs?q=<search>&sort=artist|title|year&offset=&limit=
//        -> {ok, scanning, total, count, offset, songs:[{folder,artist,title,year,genre}]}
//   GET  /api/supernova/playlists            -> {ok, playlists:[{id,name,count}]}
//   GET  /api/supernova/playlists?id=&offset=&limit=
//        -> {ok, playlists:[{id,name,count,songs:[{folder,artist,title,missing}]}]}
//   POST /api/supernova/playlists {"action":"create|rename|delete|add|remove|up|down",
//        "id":n, "name":"...", "folder":"Artist - Title"} -> {ok, id}
// Songs are identified by their folder name.
#pragma once

namespace SupernovaWeb {

bool registerRoutes();

}  // namespace SupernovaWeb
