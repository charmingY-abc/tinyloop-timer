@echo off
setlocal
cd /d "%~dp0"
if not exist dist mkdir dist
rc /nologo /fo dist\app.res app.rc || exit /b 1
cl /nologo /std:c11 /O2 /W4 /MT /GS /DUNICODE /D_UNICODE ^
  win_main.c win_storage.c timer_core.c audio.c dist\app.res ^
  /Fe:dist\TinyLoop-Win10-x64.exe ^
  /link /SUBSYSTEM:WINDOWS /MACHINE:X64 shell32.lib user32.lib gdi32.lib winmm.lib || exit /b 1
echo Built dist\TinyLoop-Win10-x64.exe
