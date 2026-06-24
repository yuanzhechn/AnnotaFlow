@echo off
setlocal

set "APP_DIR=%~dp0"
set "APP_EXE=%APP_DIR%bin\AnnotaFlow.exe"
set "ENV_NAME=AnnotaFlow"

set "CONDA_BAT="
if exist "%USERPROFILE%\miniconda3\Scripts\activate.bat" set "CONDA_BAT=%USERPROFILE%\miniconda3\Scripts\activate.bat"
if exist "%USERPROFILE%\anaconda3\Scripts\activate.bat" set "CONDA_BAT=%USERPROFILE%\anaconda3\Scripts\activate.bat"
if exist "D:\anaconda2025.06-1\Scripts\activate.bat" set "CONDA_BAT=D:\anaconda2025.06-1\Scripts\activate.bat"

if not "%CONDA_BAT%"=="" (
    call "%CONDA_BAT%" %ENV_NAME%
) else (
    where conda > nul 2> nul
    if not errorlevel 1 (
        call conda activate %ENV_NAME%
    )
)

if not exist "%APP_EXE%" (
    if exist "%APP_DIR%build-msvc-release\app-qt\AnnotaFlow.exe" (
        set "APP_EXE=%APP_DIR%build-msvc-release\app-qt\AnnotaFlow.exe"
    )
)

if not exist "%APP_EXE%" (
    echo AnnotaFlow has not been built yet.
    echo Expected executable:
    echo %APP_DIR%bin\AnnotaFlow.exe
    echo or:
    echo %APP_DIR%build-msvc-release\app-qt\AnnotaFlow.exe
    echo.
    echo Please build it first. See README.md for build commands.
    pause
    exit /b 1
)

if not "%CONDA_PREFIX%"=="" (
    if exist "%CONDA_PREFIX%\Library\bin" (
        set "PATH=%CONDA_PREFIX%\Library\bin;%PATH%"
    )
)

if exist "D:\anaconda2025.06-1\Library\bin" (
    set "PATH=D:\anaconda2025.06-1\Library\bin;%PATH%"
)

start "" "%APP_EXE%"

endlocal
exit /b 0
