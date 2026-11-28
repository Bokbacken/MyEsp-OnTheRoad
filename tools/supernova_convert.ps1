<#
.SYNOPSIS
Converts UltraStar song folders to P4 MJPEG/MP3 AVI, straight onto the SD card.

.DESCRIPTION
Each video is scaled to use the full width or full height of the player's video area
(aspect ratio kept), padded with black to 16-pixel multiples for the hardware JPEG
decoder. Output mirrors the player layout: <Dest>\<Artist - Title>\<name>.avi plus the
.txt/.mp3/cover files. Needs ffmpeg and ffprobe on PATH; works on Windows PowerShell 5.1.

-WebMp4 also writes <name>.mp4 (H.264 High, faststart, no audio, height <= -WebHeight, <= -WebKbps)
for browser playback. -Mp4Only does just that, so an already converted AVI-only tree can be upgraded
in place: .\supernova_convert.ps1 E:\otr\supernova E:\otr\supernova -Mp4Only

.EXAMPLE
.\supernova_convert.ps1 D:\Songs E:\otr\supernova -WebMp4
.\supernova_convert.ps1 D:\Songs E:\otr\supernova
.\supernova_convert.ps1 D:\Songs E:\otr\supernova -Jobs 4 -Area 800x420
.\supernova_convert.ps1 D:\Songs C:\temp\out -DryRun
#>
param(
    [Parameter(Mandatory)][string]$Source,   # song folder, folder of song folders, or one video
    [Parameter(Mandatory)][string]$Dest,     # e.g. E:\otr\supernova
    [string]$Area = '800x420',               # video area WxH below the header on the 800x480 P4_43
    [int]$Fps = 15,
    [int]$Quality = 10,                      # MJPEG -q:v, lower = better/larger
    [int]$AudioKbps = 160,
    [int]$Jobs = 2,
    [switch]$WebMp4,                         # also make a web-friendly H.264 .mp4 next to the AVI
    [switch]$Mp4Only,                        # only make the web .mp4 (e.g. Source = Dest, an already converted card)
    [int]$WebHeight = 720,
    [int]$WebKbps = 2500,
    [switch]$Force,
    [switch]$DryRun
)
$ErrorActionPreference = 'Stop'
foreach ($t in 'ffmpeg', 'ffprobe') {
    if (-not (Get-Command $t -ErrorAction SilentlyContinue)) { throw "$t not found on PATH" }
}
$videoExt = '.mp4', '.m4v', '.mkv', '.avi', '.mov', '.webm', '.mpg', '.mpeg', '.wmv', '.flv'
$copyExt = '.txt', '.mp3', '.jpg', '.jpeg', '.png', '.lrc'
$areaW, $areaH = $Area.ToLower().Split('x') | ForEach-Object { [int]$_ }

function Get-VideoSize($path) {
    $json = & ffprobe -v error -select_streams v:0 -show_entries 'stream=width,height,sample_aspect_ratio:stream_side_data=rotation' -of json $path | ConvertFrom-Json
    $s = $json.streams | Select-Object -First 1
    if (-not $s) { return $null }
    $w = [double]$s.width; $h = [double]$s.height
    if ($s.sample_aspect_ratio -match '^(\d+):(\d+)$' -and [int]$Matches[1] -gt 0 -and [int]$Matches[2] -gt 0) {
        $w = $w * [int]$Matches[1] / [int]$Matches[2]
    }
    foreach ($sd in $s.side_data_list) {
        if ($null -ne $sd.rotation -and [math]::Abs([int]$sd.rotation) % 180 -eq 90) { $w, $h = $h, $w }
    }
    return @([int][math]::Round($w), [int][math]::Round($h))
}

# Content keeps the aspect ratio (even size); canvas is the 16-aligned frame padded with black.
function Get-Layout($sw, $sh) {
    $boxW = [int][math]::Floor($areaW / 16) * 16
    $boxH = [int][math]::Floor($areaH / 16) * 16
    $scale = [math]::Min($boxW / $sw, $boxH / $sh)
    $cw = [math]::Max(2, [int][math]::Floor($sw * $scale / 2) * 2)
    $ch = [math]::Max(2, [int][math]::Floor($sh * $scale / 2) * 2)
    $vw = [math]::Min($boxW, [int][math]::Ceiling($cw / 16) * 16)
    $vh = [math]::Min($boxH, [int][math]::Ceiling($ch / 16) * 16)
    return @($cw, $ch, $vw, $vh)
}

$root = (Resolve-Path -LiteralPath $Source).Path
$isFile = Test-Path -LiteralPath $root -PathType Leaf
if ($isFile) { $folders = @((Get-Item -LiteralPath $root).Directory) } else {
    $folders = @(Get-Item -LiteralPath $root) + @(Get-ChildItem -LiteralPath $root -Directory -Recurse)
}
$base = if ($isFile) { Split-Path $root -Parent | Split-Path -Parent } else { $root }

$work = @()
foreach ($folder in $folders) {
    $video = if ($isFile) { Get-Item -LiteralPath $root } else {
        Get-ChildItem -LiteralPath $folder.FullName -File | Where-Object { $videoExt -contains $_.Extension.ToLower() } | Sort-Object Name | Select-Object -First 1
    }
    if (-not $video) { continue }
    $outDir = if ($folder.FullName -eq $root) { Join-Path $Dest $folder.Name } else { Join-Path $Dest $folder.FullName.Substring($base.Length).TrimStart('\') }
    if (-not $DryRun) {
        New-Item -ItemType Directory -Force -Path $outDir | Out-Null
        Get-ChildItem -LiteralPath $folder.FullName -File | Where-Object { $copyExt -contains $_.Extension.ToLower() } | ForEach-Object {
            $t = Join-Path $outDir $_.Name
            if ($Force -or -not (Test-Path -LiteralPath $t)) { Copy-Item -LiteralPath $_.FullName -Destination $t }
        }
    }
    $dst = Join-Path $outDir ($video.BaseName + '.avi')
    $mp4Dst = Join-Path $outDir ($video.BaseName + '.mp4')
    $size = Get-VideoSize $video.FullName
    if (-not $size) { Write-Warning "no video stream: $($video.FullName)"; continue }
    if (-not $Mp4Only) {
        if ((Test-Path -LiteralPath $dst) -and -not $Force) { Write-Host "skip (exists): $dst" }
        elseif ($video.FullName -ne $dst) {
            $l = Get-Layout $size[0] $size[1]
            $desc = "$($video.Name): $($size[0])x$($size[1]) -> $($l[0])x$($l[1]) in $($l[2])x$($l[3])"
            if ($DryRun) { Write-Host "$desc  => $dst" }
            else {
                $vf = "scale=$($l[0]):$($l[1]):flags=lanczos,pad=$($l[2]):$($l[3]):($($l[2])-iw)/2:($($l[3])-ih)/2:black,setsar=1"
                $a = @('-y', '-v', 'error', '-i', $video.FullName, '-vf', $vf, '-r', $Fps, '-c:v', 'mjpeg', '-pix_fmt', 'yuvj420p',
                       '-q:v', $Quality, '-c:a', 'libmp3lame', '-b:a', "${AudioKbps}k", '-ar', 44100, '-ac', 2, $dst)
                $work += [pscustomobject]@{ Dst = $dst; Args = $a; Desc = $desc }
            }
        }
    }
    if ($WebMp4 -or $Mp4Only) {
        if ((Test-Path -LiteralPath $mp4Dst) -and -not $Force) { Write-Host "skip (exists): $mp4Dst" }
        elseif ($video.FullName -ne $mp4Dst) {
            $desc = "$($video.Name): web MP4 (H.264, max ${WebHeight}p, ${WebKbps} kbit/s)"
            if ($DryRun) { Write-Host "$desc  => $mp4Dst" }
            else {
                # Height capped without upscaling, width from the display aspect ratio, both even; no audio (the browser plays the mp3).
                $vf = "scale=w=trunc(min(ih\,$WebHeight)*dar/2)*2:h=trunc(min(ih\,$WebHeight)/2)*2:flags=lanczos,setsar=1"
                $a = @('-y', '-v', 'error', '-i', $video.FullName, '-vf', $vf, '-an', '-c:v', 'libx264', '-preset', 'medium',
                       '-profile:v', 'high', '-pix_fmt', 'yuv420p', '-crf', 23, '-maxrate', "${WebKbps}k", '-bufsize', "$($WebKbps * 2)k",
                       '-movflags', '+faststart', $mp4Dst)
                $work += [pscustomobject]@{ Dst = $mp4Dst; Args = $a; Desc = $desc }
            }
        }
    }
}

$failed = 0
$running = @()
function Wait-One {
    $done = $script:running | Where-Object { $_.Proc.HasExited } | Select-Object -First 1
    while (-not $done) { Start-Sleep -Milliseconds 300; $done = $script:running | Where-Object { $_.Proc.HasExited } | Select-Object -First 1 }
    $script:running = @($script:running | Where-Object { $_ -ne $done })
    if ($done.Proc.ExitCode -ne 0) { $script:failed++; Write-Warning "failed: $($done.Item.Desc)"; Remove-Item -LiteralPath $done.Item.Dst -ErrorAction SilentlyContinue }
    else { Write-Host $done.Item.Desc }
}
foreach ($item in $work) {
    while ($running.Count -ge [math]::Max(1, $Jobs)) { Wait-One }
    $args = $item.Args
    $p = Start-Process ffmpeg -ArgumentList ($args | ForEach-Object { if ("$_" -match '\s') { "`"$_`"" } else { "$_" } }) -NoNewWindow -PassThru
    $null = $p.Handle   # keep the handle so ExitCode stays readable
    $running += [pscustomobject]@{ Proc = $p; Item = $item }
}
while ($running.Count -gt 0) { Wait-One }
if (-not $DryRun) { Write-Host "done: $($work.Count - $failed) converted, $failed failed" }
exit ([int]($failed -gt 0))
