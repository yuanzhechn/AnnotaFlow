@echo off
setlocal

set "APP_DIR=%~dp0"
set "QT_BIN=D:\anaconda2025.06-1\Library\bin"
set "PYTHONW_EXE=D:\anaconda2025.06-1\pythonw.exe"
set "APP_EXE=%APP_DIR%bin\AnnotaFlow.exe"
set "SAM2_LAUNCHER=%APP_DIR%sam2-service\start_hidden.py"

if not exist "%APP_EXE%" (
    echo AnnotaFlow has not been built yet.
    echo Expected executable:
    echo %APP_EXE%
    echo.
    echo Please build it first. See README.md for build commands.
    pause
    exit /b 1
)

set "PATH=%QT_BIN%;%PATH%"

if exist "%PYTHONW_EXE%" if exist "%SAM2_LAUNCHER%" (
    "%PYTHONW_EXE%" "%SAM2_LAUNCHER%"
)

start "" "%APP_EXE%"

endlocal
exit /b 0
