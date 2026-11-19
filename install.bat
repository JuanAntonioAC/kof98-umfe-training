@echo off
setlocal
set "GAME=D:\SteamLibrary\steamapps\common\KOF98UMFE"
set "HERE=%~dp0"
set "PROXY=D:\KOF-VS-Controller-Selector\dist\xinput1_4.dll"
if not exist "%GAME%\KingOfFighters98UM_x64.exe" (
    echo No esta el ejecutable x64 en %GAME%
    pause
    exit /b 1
)
if not exist "%HERE%dist\Kof98Training.dll" (
    echo Falta dist\Kof98Training.dll. Ejecuta build.bat primero.
    pause
    exit /b 1
)
if not exist "%PROXY%" (
    echo Falta el proxy xinput1_4.dll recompilado del selector.
    pause
    exit /b 1
)
for %%I in ("%GAME%\dinput8.dll") do if %%~zI LSS 1000000 (
    echo dinput8.dll no es el modulo grande de King of Moves. No se toca, pero revisa la cadena.
)
if exist "%GAME%\xinput1_4.dll.kof98training-backup" goto copyproxy
for %%I in ("%GAME%\xinput1_4.dll") do if not %%~zI==6656 (
    echo El xinput1_4.dll instalado no mide 6656 bytes. No lo reemplazo.
    pause
    exit /b 1
)
copy /y "%GAME%\xinput1_4.dll" "%GAME%\xinput1_4.dll.kof98training-backup" >nul
if errorlevel 1 (
    echo No se pudo hacer la copia de xinput1_4.dll
    pause
    exit /b 1
)
:copyproxy
copy /y "%PROXY%" "%GAME%\xinput1_4.dll" >nul
if errorlevel 1 (
    echo No se pudo copiar el proxy xinput1_4.dll
    pause
    exit /b 1
)
copy /y "%HERE%dist\Kof98Training.dll" "%GAME%\Kof98Training.dll" >nul
if errorlevel 1 (
    echo No se pudo copiar Kof98Training.dll. Cierra el juego e intenta de nuevo.
    pause
    exit /b 1
)
echo Instalado. dinput8.dll no se modifico.
echo Desinstalar: uninstall.bat
pause
exit /b 0
