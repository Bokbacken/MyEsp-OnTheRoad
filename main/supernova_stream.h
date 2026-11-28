// supernova_stream.h - web playback of Supernova songs in the browser: the song's MJPEG frames one by
// one, its MP3 audio (HTTP Range) and its lyrics as JSON; the page keeps the picture in step with
// the audio clock. Needs the song to be an AVI made by tools/supernova_convert.ps1 for video.
#pragma once

namespace SupernovaStream {

bool registerRoutes();
void release();   // Close the song the browser had open (e.g. before the card is removed)

}  // namespace SupernovaStream
