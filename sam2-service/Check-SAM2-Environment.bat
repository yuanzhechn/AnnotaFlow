@echo off
chcp 65001 > nul
setlocal

set "SERVICE_DIR=%~dp0"
set "CONDA_BAT=D:\anaconda2025.06-1\Scripts\activate.bat"

if not exist "D:\anaconda2025.06-1\envs\AnnotaFlow\python.exe" (
    echo [AnnotaFlow] Missing conda environment: AnnotaFlow
    echo conda env create -f "%SERVICE_DIR%environment.yml"
    exit /b 1
)

if exist "%CONDA_BAT%" (
    call "%CONDA_BAT%" AnnotaFlow
) else (
    call conda activate AnnotaFlow
)

python "%SERVICE_DIR%check_environment.py"
