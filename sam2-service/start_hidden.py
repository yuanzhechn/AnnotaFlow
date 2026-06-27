from __future__ import annotations

import os
import socket
import subprocess
import sys
from pathlib import Path


def port_is_open(host: str = "127.0.0.1", port: int = 8765, timeout: float = 0.3) -> bool:
    try:
        with socket.create_connection((host, port), timeout=timeout):
            return True
    except OSError:
        return False


def main() -> int:
    if port_is_open():
        return 0

    service_dir = Path(__file__).resolve().parent
    log_dir = service_dir / "logs"
    log_dir.mkdir(exist_ok=True)
    log_path = log_dir / "sam2-service.log"

    python_exe = Path(r"D:\anaconda2025.06-1\envs\AnnotaFlow\python.exe")
    if not python_exe.exists():
        python_exe = Path(sys.executable)

    env = os.environ.copy()
    env.setdefault("OMP_NUM_THREADS", "2")
    env.setdefault("MKL_NUM_THREADS", "2")
    env.setdefault("OPENBLAS_NUM_THREADS", "2")
    env.setdefault("NUMEXPR_NUM_THREADS", "2")
    env.setdefault("KMP_BLOCKTIME", "0")
    env.setdefault("CUDA_MODULE_LOADING", "LAZY")
    env.setdefault("ANNOTAFLOW_SAM2_SOURCE", str(service_dir.parent))
    env.setdefault(
        "ANNOTAFLOW_SAM2_CHECKPOINT",
        str(service_dir.parent / "models" / "sam2.1_hiera_small.pt"),
    )
    env.setdefault("ANNOTAFLOW_SAM2_CONFIG", "configs/sam2.1/sam2.1_hiera_s.yaml")

    cmd = [str(python_exe), str(service_dir / "server.py")]
    creationflags = 0
    if os.name == "nt":
        creationflags = (
            subprocess.CREATE_NO_WINDOW
            | subprocess.DETACHED_PROCESS
            | subprocess.BELOW_NORMAL_PRIORITY_CLASS
        )

    with log_path.open("ab") as log:
        subprocess.Popen(
            cmd,
            cwd=str(service_dir),
            env=env,
            stdin=subprocess.DEVNULL,
            stdout=log,
            stderr=subprocess.STDOUT,
            creationflags=creationflags,
            close_fds=True,
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
