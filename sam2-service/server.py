from __future__ import annotations

import argparse
import os
import json
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Any

from sam2_backend import create_backend


def _apply_process_limits() -> None:
    os.environ.setdefault("OMP_NUM_THREADS", "2")
    os.environ.setdefault("MKL_NUM_THREADS", "2")
    os.environ.setdefault("OPENBLAS_NUM_THREADS", "2")
    os.environ.setdefault("NUMEXPR_NUM_THREADS", "2")
    os.environ.setdefault("KMP_BLOCKTIME", "0")
    os.environ.setdefault("CUDA_MODULE_LOADING", "LAZY")
    if os.name != "nt":
        return
    try:
        import ctypes

        below_normal_priority_class = 0x00004000
        handle = ctypes.windll.kernel32.GetCurrentProcess()
        ctypes.windll.kernel32.SetPriorityClass(handle, below_normal_priority_class)
    except Exception:
        pass


def _json_response(handler: BaseHTTPRequestHandler, status: int, payload: dict[str, Any]) -> None:
    body = json.dumps(payload, ensure_ascii=False).encode("utf-8")
    handler.send_response(status)
    handler.send_header("Content-Type", "application/json; charset=utf-8")
    handler.send_header("Content-Length", str(len(body)))
    handler.end_headers()
    handler.wfile.write(body)


def _read_points(request: dict[str, Any]) -> tuple[list[tuple[float, float]], list[int]]:
    if "points" in request:
        raw_points = request["points"]
        if not isinstance(raw_points, list) or not raw_points:
            raise ValueError("points must be a non-empty array")
        points = []
        for item in raw_points:
            if not isinstance(item, list) or len(item) != 2:
                raise ValueError("each point must be [x, y]")
            points.append((float(item[0]), float(item[1])))
    else:
        point_raw = request["point"]
        if not isinstance(point_raw, list) or len(point_raw) != 2:
            raise ValueError("point must be [x, y]")
        points = [(float(point_raw[0]), float(point_raw[1]))]

    raw_labels = request.get("point_labels", request.get("labels"))
    if raw_labels is None:
        labels = [int(request.get("label", 1))] * len(points)
    else:
        if not isinstance(raw_labels, list) or len(raw_labels) != len(points):
            raise ValueError("point_labels must match points length")
        labels = [int(value) for value in raw_labels]
    return points, labels


class Sam2RequestHandler(BaseHTTPRequestHandler):
    backend = create_backend(False)
    inference_gate = threading.Lock()

    def log_message(self, format: str, *args: Any) -> None:
        print("%s - %s" % (self.address_string(), format % args))

    def do_GET(self) -> None:
        if self.path != "/health":
            _json_response(self, 404, {"ok": False, "error": "not found"})
            return
        _json_response(self, 200, {"ok": True, "backend": self.backend.name})

    def do_POST(self) -> None:
        if self.path == "/shutdown":
            _json_response(self, 200, {"ok": True, "message": "shutting down"})
            threading.Thread(target=self.server.shutdown, daemon=True).start()
            return

        if self.path == "/prepare":
            if not self.inference_gate.acquire(blocking=False):
                _json_response(self, 429, {"ok": False, "error": "SAM2 is busy"})
                return
            try:
                length = int(self.headers.get("Content-Length", "0"))
                request = json.loads(self.rfile.read(length).decode("utf-8"))
                image_path = str(request["image_path"])
                self.backend.prepare_image(image_path)
                _json_response(self, 200, {"ok": True, "backend": self.backend.name})
            except Exception as exc:
                _json_response(self, 500, {"ok": False, "error": str(exc)})
            finally:
                self.inference_gate.release()
            return

        if self.path != "/predict":
            _json_response(self, 404, {"ok": False, "error": "not found"})
            return

        acquired_gate = False
        try:
            acquired_gate = self.inference_gate.acquire(blocking=False)
            if not acquired_gate:
                _json_response(self, 429, {"ok": False, "error": "SAM2 is busy"})
                return
            length = int(self.headers.get("Content-Length", "0"))
            request = json.loads(self.rfile.read(length).decode("utf-8"))
            image_path = str(request["image_path"])
            points, labels = _read_points(request)

            prediction = self.backend.predict(image_path, points=points, labels=labels)
            response = {
                "ok": True,
                "bbox": prediction.bbox,
                "bbox_format": "xywh",
                "contours": prediction.contours or [],
            }
            if prediction.score is not None:
                response["score"] = prediction.score
            _json_response(self, 200, response)
        except Exception as exc:
            _json_response(self, 500, {"ok": False, "error": str(exc)})
        finally:
            if acquired_gate:
                self.inference_gate.release()


def main() -> int:
    _apply_process_limits()
    parser = argparse.ArgumentParser(description="AnnotaFlow local SAM2 image prompt service")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--mock", action="store_true", help="return a fixed bbox for Qt integration testing")
    args = parser.parse_args()

    Sam2RequestHandler.backend = create_backend(args.mock)
    server = ThreadingHTTPServer((args.host, args.port), Sam2RequestHandler)
    print(f"AnnotaFlow SAM2 service listening on http://{args.host}:{args.port}")
    print(f"Backend: {Sam2RequestHandler.backend.name}")
    try:
        server.serve_forever()
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
