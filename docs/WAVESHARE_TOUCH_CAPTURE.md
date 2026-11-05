# Waveshare factory touch capture

## Application comparison: v0.25.1 and later

The S3_35B application diagnostic build records the same raw fields, without
live touch-event/sample logs. Reset with serial logging open at 115200 baud.
The `touch_raw: ARMED` message precedes recording. First valid contact starts
512 consecutive reads: hold still for two seconds, drag slowly and release.
Wait for `touch_raw: END capture` and save the complete log. This is automatic,
not triggered by the application's clear-log button. Reset to repeat.
Dumping briefly pauses input after measurement. Other application module logs
remain enabled. Transport success in this trace does not imply a valid report.

`dt_us` measures the synchronous call's elapsed time, not pure on-wire bus
occupancy. From v0.25.4 it also includes any wait for the application's shared
bus mutex and 100 us inter-transaction gap. Touch remains at a 30 ms minimum
poll interval and PWR at 100 ms for this comparison. An idle bus does not
necessarily incur the extra 100 us: only the unelapsed portion is waited.

## Factory comparison

The optional `waveshare_touch_trace` input to the manual firmware workflow
instruments `examples/esp-idf/01_factory/components/esp_bsp/bsp_touch.c`.
The original I2C command, clock, timeout, report parsing and LVGL handling
are unchanged. The artifact includes the instrumented source and upstream SHA.
The instrumentation adds the BSP's explicit `esp_timer` build dependency for
timestamp collection.

The most recent 512 reads are continuously saved in a RAM ring buffer, including
empty reports and transport errors. Pressing the drawing screen's Refresh/clear
button prints the buffer in chronological order as `touch_raw` lines and empties
it for the next measurement. The button still clears the canvas. Each line includes the read
start time since boot (`t_us`), I2C transaction duration (`dt_us`), transport
result and all 14 bytes. Bytes on failed transfers are not valid touch data.
There is a small RAM/timestamp overhead during capture, but no per-sample
serial output until Refresh is pressed. Dumping can briefly pause the UI.

1. Back up application data before replacing the partition table.
2. Flash the trace artifact's `merged.bin` at `0x0` using USB.
3. Keep the factory example's required FAT32 SD card inserted.
4. Open serial logging at 115200 baud and reset, keeping fingers off the screen.
5. Hold a finger still for about two seconds, drag slowly, then release.
   Promptly press Refresh/clear to dump the recent reads. Wait until
   `touch_raw: END capture` appears and save the complete log. Older reads
   are overwritten after 512 new reads; the time window depends on polling rate.
6. Repeat and press Refresh again for another capture. The capture is not written to SD and cannot be downloaded
   through the normal application's web log page.

This is a diagnostic build of the factory example, not controller firmware
and not an application OTA image. Restoring MyEsp-OnTheRoad requires its
S3 3.5B merged image over USB because the partition layouts differ.
