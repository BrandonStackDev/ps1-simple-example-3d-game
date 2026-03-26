@echo off


REM convert obj's to data
REM for /r ".\assets\obj\"   %%F in (*.obj) do (python tools\convertObject.py "%%F" 32)

REM convert png's to data
REM for /r ".\assets\png\"   %%F in (*.png) do (python tools\convertImage.py "%%F" 32)

REM convert data to .S asm
REM for /r ".\assets\dat\"   %%F in (*.dat) do (python tools\convertData.py "%%F" 32)


REM generate obj data files
python tools\convertObject.py assets\obj\char_01.obj 16 player 64 64
python tools\convertObject.py assets\obj\level_01.obj 2048 level
REM generate .s
python tools\linkData.py playerVertices assets\dat\player_verts.dat
python tools\linkData.py playerTextCoords assets\dat\player_vert_text.dat
python tools\linkData.py playerFaces assets\dat\player_faces.dat
python tools\linkData.py levelVertices assets\dat\level_verts.dat
python tools\linkData.py levelFaces assets\dat\level_faces.dat

REM generate player texture stuff
python tools\convertImage.py -b 4 assets\png\char01.png assets\dat\char_01_t.dat assets\dat\char_01_p.dat
REM generate player texture and palette .S
python tools\linkData.py playerTexture assets\dat\char_01_t.dat
python tools\linkData.py playerPalette assets\dat\char_01_p.dat

REM generate font data files
python tools\convertImage.py -b 4 assets\png\font.png assets\dat\fontTexture.dat assets\dat\fontPalette.dat
REM generate font and palette .S
python tools\linkData.py fontTexture assets\dat\fontTexture.dat
python tools\linkData.py fontPalette assets\dat\fontPalette.dat



REM addBinaryFile(example06_fonts fontTexture "${PROJECT_BINARY_DIR}/example06/fontTexture.dat")
REM addBinaryFile(example06_fonts fontPalette "${PROJECT_BINARY_DIR}/example06/fontPalette.dat")


