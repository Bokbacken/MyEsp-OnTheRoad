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

ffmpeg -i "%~1" -vf "scale=800:480:force_original_aspect_ratio=decrease:force_divisible_by=2,pad=800:480:(ow-iw)/2:(oh-ih)/2" -r 25 -c:v libx264 -profile:v baseline -level 3.1 -pix_fmt yuv420p -b:v 1200k -maxrate 1500k -bufsize 3000k -g 25 -c:a copy -movflags +faststart "%~2"
exit /b %errorlevel%

:usage
echo Usage: konvert.bat input.mp4 output.mp4
exit /b 1
