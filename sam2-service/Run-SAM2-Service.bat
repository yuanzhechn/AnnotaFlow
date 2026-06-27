@echo off
chcp 65001 > nul
setlocal

set "SERVICE_DIR=%~dp0"
set "CONDA_BAT="
set "BASE_PYTHON=D:\anaconda2025.06-1\python.exe"
set "ENV_NAME=AnnotaFlow"
set "PROJECT_ROOT=%SERVICE_DIR%.."
set "ALLOW_MISSING_MODEL=0"

if "%OMP_NUM_THREADS%"=="" set "OMP_NUM_THREADS=2"
if "%MKL_NUM_THREADS%"=="" set "MKL_NUM_THREADS=2"
if "%OPENBLAS_NUM_THREADS%"=="" set "OPENBLAS_NUM_THREADS=2"
if "%NUMEXPR_NUM_THREADS%"=="" set "NUMEXPR_NUM_THREADS=2"
if "%KMP_BLOCKTIME%"=="" set "KMP_BLOCKTIME=0"
if "%CUDA_MODULE_LOADING%"=="" set "CUDA_MODULE_LOADING=LAZY"
if "%ANNOTAFLOW_TORCH_THREADS%"=="" set "ANNOTAFLOW_TORCH_THREADS=2"
if "%ANNOTAFLOW_TORCH_INTEROP_THREADS%"=="" set "ANNOTAFLOW_TORCH_INTEROP_THREADS=1"

for %%A in (%*) do (
    if "%%~A"=="--mock" set "ALLOW_MISSING_MODEL=1"
)

if exist "%BASE_PYTHON%" (
    "%BASE_PYTHON%" -c "import socket; s=socket.create_connection(('127.0.0.1', 8765), 0.3); s.close()" > nul 2> nul
    if not errorlevel 1 (
        echo [AnnotaFlow] SAM2 service is already running on 127.0.0.1:8765.
        exit /b 0
    )
)

if exist "%USERPROFILE%\miniconda3\Scripts\activate.bat" set "CONDA_BAT=%USERPROFILE%\miniconda3\Scripts\activate.bat"
if exist "%USERPROFILE%\anaconda3\Scripts\activate.bat" set "CONDA_BAT=%USERPROFILE%\anaconda3\Scripts\activate.bat"
if exist "D:\anaconda2025.06-1\Scripts\activate.bat" set "CONDA_BAT=D:\anaconda2025.06-1\Scripts\activate.bat"

if not "%CONDA_BAT%"=="" (
    call "%CONDA_BAT%" %ENV_NAME%
) else (
    where conda > nul 2> nul
    if errorlevel 1 (
        echo [AnnotaFlow] Conda was not found. Please install Miniconda or Anaconda.
        exit /b 1
    )
    call conda activate %ENV_NAME%
)

where python > nul 2> nul
if errorlevel 1 (
    echo [AnnotaFlow] Python was not found after activating %ENV_NAME%.
    echo [AnnotaFlow] Please create the environment with:
    echo conda env create -f "%SERVICE_DIR%environment.yml"
    exit /b 1
)

if "%ANNOTAFLOW_SAM2_SOURCE%"=="" (
    set "ANNOTAFLOW_SAM2_SOURCE=%PROJECT_ROOT%"
)

if "%ANNOTAFLOW_SAM2_CHECKPOINT%"=="" (
    set "ANNOTAFLOW_SAM2_CHECKPOINT=%PROJECT_ROOT%\models\sam2.1_hiera_small.pt"
)

if "%ANNOTAFLOW_SAM2_CONFIG%"=="" (
    set "ANNOTAFLOW_SAM2_CONFIG=configs/sam2.1/sam2.1_hiera_s.yaml"
)

echo [AnnotaFlow] SAM2 service env: %ENV_NAME%
echo [AnnotaFlow] SAM2 checkpoint: %ANNOTAFLOW_SAM2_CHECKPOINT%
echo [AnnotaFlow] SAM2 config: %ANNOTAFLOW_SAM2_CONFIG%

if "%ANNOTAFLOW_SAM2_CHECKPOINT%"=="" (
    echo [AnnotaFlow] SAM2 checkpoint was not found. For UI testing, run with --mock.
)

if not exist "%ANNOTAFLOW_SAM2_CHECKPOINT%" if "%ALLOW_MISSING_MODEL%"=="0" (
    echo [AnnotaFlow] SAM2 checkpoint was not found:
    echo %ANNOTAFLOW_SAM2_CHECKPOINT%
    echo [AnnotaFlow] Put sam2.1_hiera_small.pt under "%PROJECT_ROOT%\models" or run with --mock.
    exit /b 1
)

if "%ANNOTAFLOW_SAM2_CONFIG%"=="" (
    echo [AnnotaFlow] SAM2 config was not found. For UI testing, run with --mock.
)

python "%SERVICE_DIR%server.py" %*
