@echo off
setlocal

if "%~1"=="" goto usage
if "%~2"=="" goto usage
if not "%~3"=="" goto usage

where ffmpeg >nul 2>&1
if errorlevel 1 (
    echo Error: ffmpeg was not found. Install FFmpeg and add it to PATH.
    exit /b 1
)

if not exist "%~1" (
    echo Error: Input file not found: "%~1"
    exit /b 1
)

rem Baseline MJPEG (yuvj420p, 800x480 = multiples of 16) for the P4 hardware JPEG decoder, MP3 audio in AVI.
ffmpeg -i "%~1" -vf "scale=800:480:force_original_aspect_ratio=decrease:force_divisible_by=2,pad=800:480:(ow-iw)/2:(oh-ih)/2" -r 15 -c:v mjpeg -pix_fmt yuvj420p -q:v 10 -c:a libmp3lame -b:a 160k -ar 44100 "%~2"
exit /b %errorlevel%

:usage
echo Usage: konvert_mjpeg.bat input.mp4 output.avi
exit /b 1
