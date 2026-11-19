@echo off
setlocal
set "GAME=D:\SteamLibrary\steamapps\common\KOF98UMFE"
if not exist "%GAME%\xinput1_4.dll.kof98training-backup" (
    echo No hay copia xinput1_4.dll.kof98training-backup. No reescribo el proxy.
    pause
    exit /b 1
)
copy /y "%GAME%\xinput1_4.dll.kof98training-backup" "%GAME%\xinput1_4.dll" >nul
if errorlevel 1 (
    echo No se pudo restaurar xinput1_4.dll. Cierra el juego e intenta de nuevo.
    pause
    exit /b 1
)
del /q "%GAME%\Kof98Training.dll" 2>nul
echo Proxy restaurado y Kof98Training.dll eliminada.
echo kof98training.ini y kof98training.log, si existen, se dejan en la carpeta del juego.
pause
exit /b 0
