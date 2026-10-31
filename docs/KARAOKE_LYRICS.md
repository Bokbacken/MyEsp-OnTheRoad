# Karaoke `.lyr` sidecar lyrics

Put a UTF-8 text file beside a MIDI/KAR file, using the same basename and the
`.lyr` extension. For example, `Artist_Title.mid` can use
`Artist_Title.lyr`. Embedded MIDI/KAR lyrics always take priority over the
sidecar.

Without timestamps, each non-empty line is shown as a manually scrollable lyric
sheet on the device and on the web karaoke page. Blank lines are retained as
spacing.

For line timing, use LRC timestamps at the start of each line:

```text
[00:12.30]First lyric line
[00:16]Next lyric line
[00:19.125]Another line
[00:24]
```

Timestamps accept `[mm:ss]`, `[mm:ss.xx]` (centiseconds), or
`[mm:ss.xxx]` (milliseconds); seconds are `00`–`59`. Minutes may have more
than two digits, up to the device's timestamp range (about 71 minutes).
Multiple timestamps before the same text and repeated timestamp lines are
preserved. A timestamp with no text clears the current lyric line. Standard
metadata tags such as `[ar:Artist]` and `[ti:Title]` are ignored. An optional
`[offset:milliseconds]` tag shifts every timestamp, including when it appears
after lyric lines; a negative offset clips times below zero, while times beyond
the supported range are rejected. Timed sidecars follow the MIDI playback
position, so play, pause, and seek also control lyric timing.

The parser accepts UTF-8 (optionally with a UTF-8 BOM), LF or CRLF line endings,
files up to 64 KiB, at most 1,024 lyric lines, and at most 512 bytes per line.
Invalid UTF-8, malformed LRC timestamps, empty sidecars, and exceeded limits
are reported when opening the song. A missing sidecar is normal.
