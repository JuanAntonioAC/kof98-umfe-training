@echo off
setlocal
set "BIN=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64"
set "ROOT=%~dp0"
set "SRC=%ROOT%src"
set "OUT=%ROOT%build"
set "DIST=%ROOT%dist"
if not exist "%OUT%" mkdir "%OUT%"
if not exist "%DIST%" mkdir "%DIST%"

if not exist "%BIN%\ml64.exe" (
    echo No esta ml64.exe en %BIN%
    pause
    exit /b 1
)
"%BIN%\ml64.exe" /nologo /c /Fo"%OUT%\hook.obj" "%SRC%\hook.asm"
if errorlevel 1 (
    echo Fallo ml64.
    pause
    exit /b 1
)
set "CC=D:\KOF-VS-Controller-Selector\build\llvm-mingw-20261020-ucrt-x86_64\bin\clang.exe"
if not exist "%CC%" (
    echo No esta clang.exe en %CC%
    pause
    exit /b 1
)
"%CC%" -std=c11 -O2 -Wall -Wno-unused-parameter -I"%SRC%" -o "%DIST%\TrainLogicTest.exe" "%SRC%\test_logic.c" "%SRC%\logic.c" "%SRC%\moves.c"
if errorlevel 1 (
    echo Fallo al compilar TrainLogicTest.exe
    pause
    exit /b 1
)
"%DIST%\TrainLogicTest.exe"
if errorlevel 1 (
    echo Fallo TrainLogicTest.exe
    pause
    exit /b 1
)

"%CC%" -std=c11 -O2 -Wall -Wno-unused-parameter -D_CRT_SECURE_NO_WARNINGS -I"%SRC%" -shared -o "%DIST%\Kof98Training.dll" "%SRC%\dllmain.c" "%SRC%\bind.c" "%SRC%\menu.c" "%SRC%\padnav.c" "%SRC%\log.c" "%SRC%\logic.c" "%SRC%\moves.c" "%OUT%\hook.obj" -luser32 -lgdi32
if errorlevel 1 (
    echo Fallo al compilar Kof98Training.dll
    pause
    exit /b 1
)
echo BUILD_OK
pause
exit /b 0
