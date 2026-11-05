# Waveshare factory touch capture

The optional `waveshare_touch_trace` input to the manual firmware workflow
instruments `examples/esp-idf/01_factory/components/esp_bsp/bsp_touch.c`.
The original I2C command, clock, timeout, report parsing and LVGL handling
are unchanged. The artifact includes the instrumented source and upstream SHA.

The first successful read with a point count of one or two starts a single
512-read capture. Every subsequent result is saved in RAM, including empty
reports and transport errors. Only after all 512 reads have completed are
the buffered records printed as `touch_raw` lines. Each includes the read
start time since boot (`t_us`), I2C transaction duration (`dt_us`), transport
result and all 14 bytes. Bytes on failed transfers are not valid touch data.
There is a small RAM/timestamp overhead during capture, but no per-sample
serial output until capture is complete. Dumping can briefly pause the UI.

1. Back up application data before replacing the partition table.
2. Flash the trace artifact's `merged.bin` at `0x0` using USB.
3. Keep the factory example's required FAT32 SD card inserted.
4. Open serial logging at 115200 baud and reset, keeping fingers off the screen.
5. Hold a finger still for about two seconds, drag slowly, then release.
   Wait until `touch_raw: END capture` appears and save the complete log.
   The capture duration depends on the factory polling rate; keep logging
   until the end marker rather than assuming a fixed number of seconds.
6. Reset to repeat. The capture is not written to SD and cannot be downloaded
   through the normal application's web log page.

This is a diagnostic build of the factory example, not controller firmware
and not an application OTA image. Restoring MyEsp-OnTheRoad requires its
S3 3.5B merged image over USB because the partition layouts differ.
