#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p dist
gcc -std=c11 -O2 -Wall -Wextra -Werror timer_core.c audio.c tests/core_test.c \
  -lm -o dist/core_test
dist/core_test
x86_64-w64-mingw32-windres app.rc -O coff -o dist/app.res.o
x86_64-w64-mingw32-gcc -std=c11 -Os -Wall -Wextra -Werror -municode -mwindows \
  -static -fstack-protector-strong -D_FORTIFY_SOURCE=2 \
  win_main.c win_storage.c timer_core.c audio.c dist/app.res.o \
  -o dist/TinyLoop-Win10-x64.exe -lshell32 -luser32 -lgdi32 -lwinmm -lm
x86_64-w64-mingw32-strip dist/TinyLoop-Win10-x64.exe
file dist/TinyLoop-Win10-x64.exe
