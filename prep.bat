@echo off


REM convert obj's to data
REM for /r ".\assets\obj\"   %%F in (*.obj) do (python tools\convertObject.py "%%F" 32)

REM convert png's to data
for /r ".\assets\png\"   %%F in (*.png) do (python tools\convertImage.py "%%F" 32)

REM convert data to .S asm
for /r ".\assets\inc\"   %%F in (*.dat) do (python tools\convertData.py "%%F" 32)


