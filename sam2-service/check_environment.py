from __future__ import annotations

import importlib
import os
import sys
from pathlib import Path


def check_import(name: str) -> object | None:
    try:
        module = importlib.import_module(name)
    except Exception as exc:
        print(f"[FAIL] import {name}: {type(exc).__name__}: {exc}")
        return None
    version = getattr(module, "__version__", "")
    print(f"[ OK ] import {name} {version}".rstrip())
    return module


def main() -> int:
    print(f"Python: {sys.executable}")
    print(f"Python version: {sys.version.split()[0]}")

    ok = True
    numpy = check_import("numpy")
    ok = ok and numpy is not None and hasattr(numpy, "ndarray")
    if numpy is not None and not hasattr(numpy, "ndarray"):
        print("[FAIL] numpy.ndarray is missing")

    torch = check_import("torch")
    ok = ok and torch is not None
    if torch is not None:
        try:
            print(f"Torch CUDA available: {torch.cuda.is_available()}")
        except Exception as exc:
            print(f"[WARN] torch.cuda check failed: {exc}")

    sam2_build = check_import("sam2.build_sam")
    ok = ok and sam2_build is not None
    if sam2_build is not None:
        for symbol in ("build_sam2", "build_sam2_video_predictor"):
            if hasattr(sam2_build, symbol):
                print(f"[ OK ] sam2.build_sam.{symbol}")
            else:
                print(f"[FAIL] sam2.build_sam.{symbol} is missing")
                ok = False

    checkpoint = os.environ.get(
        "ANNOTAFLOW_SAM2_CHECKPOINT",
        r"D:\AnnotaFlow\models\sam2.1_hiera_small.pt",
    )
    if Path(checkpoint).exists():
        print(f"[ OK ] checkpoint: {checkpoint}")
    else:
        print(f"[WARN] checkpoint not found: {checkpoint}")
        print("       Put the checkpoint there or set ANNOTAFLOW_SAM2_CHECKPOINT.")

    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
