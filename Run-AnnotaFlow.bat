@echo off
setlocal

set "APP_DIR=%~dp0"
set "QT_BIN=D:\anaconda2025.06-1\Library\bin"
set "APP_EXE=%APP_DIR%bin\AnnotaFlow.exe"

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
start "" "%APP_EXE%"

endlocal
