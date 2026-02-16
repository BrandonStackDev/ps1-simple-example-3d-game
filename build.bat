@echo off
setlocal EnableDelayedExpansion

if not exist build mkdir build

REM 1) Gather sources into a response file (so gcc doesn't need fancy scripting)
REM > build\sources.rsp  echo "src\ps1\crt0.S"
REM >>build\sources.rsp  echo "test.c"
> build\sources.rsp  echo "libc/crt0.c"
>> build\sources.rsp  echo "main.c"

REM for /r "libc"   %%F in (*.c *.S) do (set "P=%%F" & set "P=!P:\=/!" & >>build\sources.rsp echo !P!)
REM for /r "ps1"    %%F in (*.c *.S) do (set "P=%%F" & set "P=!P:\=/!" & >>build\sources.rsp echo !P!)
REM for /r "vendor" %%F in (*.c *.S) do (set "P=%%F" & set "P=!P:\=/!" & >>build\sources.rsp echo !P!)

>> build\sources.rsp  echo "gpu.c"
>> build\sources.rsp  echo "trig.c"
>> build\sources.rsp  echo "libc/malloc.c"
>> build\sources.rsp  echo "libc/misc.c"
>> build\sources.rsp  echo "libc/string.c"
>> build\sources.rsp  echo "libc/clz.s"
>> build\sources.rsp  echo "libc/setjmp.s"
>> build\sources.rsp  echo "libc/string.s"
>> build\sources.rsp  echo "ps1/cache.s"
>> build\sources.rsp  echo "vendor/printf.c"

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

REM 3) Convert ELF -> PS-EXE / PSEXE (use whatever converter script the repo provides)
REM If this filename differs, search in %BM%\tools for "psexe" and update this line.
python "tools\convertExecutable.py" build\game.elf build\game.psexe

if errorlevel 1 exit /b 1

echo OK: build\game.psexe
