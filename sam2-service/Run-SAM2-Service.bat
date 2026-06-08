@echo off
chcp 65001 > nul
setlocal

set "SERVICE_DIR=%~dp0"
set "CONDA_BAT=D:\anaconda2025.06-1\Scripts\activate.bat"
set "BASE_PYTHON=D:\anaconda2025.06-1\python.exe"
set "ENV_NAME=AnnotaFlow"

if exist "%BASE_PYTHON%" (
    "%BASE_PYTHON%" -c "import socket; s=socket.create_connection(('127.0.0.1', 8765), 0.3); s.close()" > nul 2> nul
    if not errorlevel 1 (
        echo [AnnotaFlow] SAM2 service is already running on 127.0.0.1:8765.
        exit /b 0
    )
)

if not exist "D:\anaconda2025.06-1\envs\AnnotaFlow\python.exe" (
    if exist "D:\anaconda2025.06-1\envs\LabelQuick_env\python.exe" (
        set "ENV_NAME=LabelQuick_env"
    )
)

if exist "%CONDA_BAT%" (
    call "%CONDA_BAT%" %ENV_NAME%
) else (
    call conda activate %ENV_NAME%
)

if "%ANNOTAFLOW_SAM2_SOURCE%"=="" (
    if exist "D:\LabelQuick\sampro\sam2" (
        set "ANNOTAFLOW_SAM2_SOURCE=D:\LabelQuick;D:\LabelQuick\sampro"
    )
)

if "%ANNOTAFLOW_SAM2_CHECKPOINT%"=="" (
    if exist "D:\LabelQuick\sampro\checkpoints\sam2.1_hiera_small.pt" (
        set "ANNOTAFLOW_SAM2_CHECKPOINT=D:\LabelQuick\sampro\checkpoints\sam2.1_hiera_small.pt"
    )
)

if "%ANNOTAFLOW_SAM2_CONFIG%"=="" (
    if exist "D:\LabelQuick\sampro\sam2\configs\sam2.1\sam2.1_hiera_s.yaml" (
        set "ANNOTAFLOW_SAM2_CONFIG=configs/sam2.1/sam2.1_hiera_s.yaml"
    )
)

if "%ANNOTAFLOW_SAM2_DEVICE%"=="" (
    set "ANNOTAFLOW_SAM2_DEVICE=cuda"
)

echo [AnnotaFlow] SAM2 service env: %ENV_NAME%
echo [AnnotaFlow] SAM2 checkpoint: %ANNOTAFLOW_SAM2_CHECKPOINT%
echo [AnnotaFlow] SAM2 config: %ANNOTAFLOW_SAM2_CONFIG%

if "%ANNOTAFLOW_SAM2_CHECKPOINT%"=="" (
    echo [AnnotaFlow] SAM2 checkpoint was not found. For UI testing, run with --mock.
)

if "%ANNOTAFLOW_SAM2_CONFIG%"=="" (
    echo [AnnotaFlow] SAM2 config was not found. For UI testing, run with --mock.
)

python "%SERVICE_DIR%server.py" %*
