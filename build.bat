@echo off
cd /d "%~dp0"
if not exist build mkdir build
rc /nologo /fo build\TrialChamberFinder.res src\TrialChamberFinder.rc
cl /nologo /O2 /std:c11 /W3 /wd4244 /wd4305 /D_CRT_SECURE_NO_WARNINGS /Fobuild\ /Fe:TrialChamberFinder.exe src\main.c src\chambergenerator\ChamberGenerator.c src\search\Search.c src\util\Inputs.c src\util\Threads.c src\cubiomes\biomenoise.c src\cubiomes\biomes.c src\cubiomes\layers.c src\cubiomes\noise.c build\TrialChamberFinder.res
