@echo off
rem Starts supernova_convert.ps1 (same folder) without changing the system execution policy.
rem Usage: supernova_convert.bat <songs> <SD>:\otr\supernova [-Jobs 4] [-Area 800x420] [-Force] [-DryRun]
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0supernova_convert.ps1" %*
exit /b %errorlevel%
