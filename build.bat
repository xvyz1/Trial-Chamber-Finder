@echo off
cd /d "%~dp0"
if not exist build mkdir build
rc /nologo /fo build\TrialChamberFinder.res src\TrialChamberFinder.rc
cl /nologo /O2 /std:c11 /W3 /wd4244 /wd4305 /wd4101 /D_CRT_SECURE_NO_WARNINGS /Fobuild\ /Fe:TrialChamberFinder.exe src\*.c src\chambergenerator\*.c src\search\*.c src\util\*.c src\cubiomes\*.c build\TrialChamberFinder.res
