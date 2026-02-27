@echo off
setlocal EnableDelayedExpansion

if not exist build mkdir build

REM 1) Gather sources into a response file (so gcc doesn't need fancy scripting)
REM replace contents of file with nothing
type nul > build\sources.rsp
REM document all of the things it cant find without help, recursive from . (here) down
for /r "."   %%F in (*.c *.S) do (set "P=%%F" & set "P=!P:\=/!" & >>build\sources.rsp echo !P!)

REM 2) Build ELF (startup + your code + baremetal sources) using the baremetal linker script
mipsel-none-elf-gcc ^
  -Os -ffreestanding -fno-builtin -nostdlib ^
  -march=r3000 -mabi=32 -mno-abicalls -G0 ^
  -I"libc" -I"ps1" -I"vendor" ^
  @build\sources.rsp ^
  -Wl,-T,"ps1\ps1.ld" -Wl,--gc-sections ^
  -o build\game.elf ^
  -lgcc ^
  -ggdb

REM remove ggdb flag then for release builds

if errorlevel 1 exit /b 1

REM 3) Convert ELF -> PS-EXE / PSEXE
python "tools\convertExecutable.py" build\game.elf build\game.psexe

if errorlevel 1 exit /b 1

echo OK: build\game.psexe

@REM REM convert to iso also, not ready yet
@REM exe2iso build\game.psexe -o build\game.bin
@REM if errorlevel 1 exit /b 1
@REM echo Created ISO
