@echo off


REM convert obj's to data
REM for /r ".\assets\obj\"   %%F in (*.obj) do (python tools\convertObject.py "%%F" 32)

REM convert png's to data
REM for /r ".\assets\png\"   %%F in (*.png) do (python tools\convertImage.py "%%F" 32)

REM convert data to .S asm
REM for /r ".\assets\dat\"   %%F in (*.dat) do (python tools\convertData.py "%%F" 32)

REM generate font data files
python tools\convertImage.py -b 4 assets\png\font.png assets\dat\fontTexture.dat assets\dat\fontPalette.dat
REM generate font and palette .S
python tools\linkData.py fontTexture assets\dat\fontTexture.dat
python tools\linkData.py fontPalette assets\dat\fontPalette.dat



REM addBinaryFile(example06_fonts fontTexture "${PROJECT_BINARY_DIR}/example06/fontTexture.dat")
REM addBinaryFile(example06_fonts fontPalette "${PROJECT_BINARY_DIR}/example06/fontPalette.dat")


