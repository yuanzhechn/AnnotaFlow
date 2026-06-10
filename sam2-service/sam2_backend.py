from __future__ import annotations

import os
import sys
import threading
from dataclasses import dataclass
from pathlib import Path
from typing import Any


@dataclass
class Prediction:
    bbox: list[float]
    score: float | None = None
    contours: list[list[list[float]]] | None = None


class MockBackend:
    def __init__(self) -> None:
        self.name = "mock"

    def predict(
        self,
        image_path: str,
        point: tuple[float, float] | None = None,
        points: list[tuple[float, float]] | None = None,
        labels: list[int] | None = None,
    ) -> Prediction:
        del image_path
        if points:
            x, y = points[-1]
        elif point:
            x, y = point
        else:
            x, y = 48.0, 48.0
        size = 96.0
        x0 = max(0.0, x - size / 2)
        y0 = max(0.0, y - size / 2)
        contour = [[[x0, y0], [x0 + size, y0], [x0 + size, y0 + size], [x0, y0 + size]]]
        return Prediction([x0, y0, size, size], 1.0, contour)

    def prepare_image(self, image_path: str) -> None:
        del image_path
        return None


class SAM2Backend:
    def __init__(self) -> None:
        self.name = "sam2"
        self._lock = threading.Lock()
        self._predictor: Any | None = None
        self._loaded_image_key: tuple[str, float] | None = None
        self._last_prompt_image_key: tuple[str, float] | None = None
        self._last_prompt_points: tuple[tuple[float, float], ...] = ()
        self._last_prompt_labels: tuple[int, ...] = ()
        self._last_best_mask_input: Any | None = None

    def prepare_image(self, image_path: str) -> None:
        with self._lock:
            predictor = self._ensure_predictor()
            self._ensure_image_loaded(predictor, image_path)

    def predict(
        self,
        image_path: str,
        point: tuple[float, float] | None = None,
        points: list[tuple[float, float]] | None = None,
        labels: list[int] | None = None,
    ) -> Prediction:
        with self._lock:
            predictor = self._ensure_predictor()
            self._ensure_image_loaded(predictor, image_path)

            import numpy as np
            import torch

            if not points:
                if not point:
                    raise ValueError("At least one point is required.")
                points = [point]
            if not labels:
                labels = [1] * len(points)
            if len(points) != len(labels):
                raise ValueError("points and labels must have the same length.")

            point_coords = np.array(points, dtype=np.float32)
            point_labels = np.array(labels, dtype=np.int32)
            predict_kwargs: dict[str, Any] = {
                "point_coords": point_coords,
                "point_labels": point_labels,
            }
            reused_mask_input = self._reuse_mask_input(points, labels)
            if reused_mask_input is None:
                predict_kwargs["multimask_output"] = True
            else:
                predict_kwargs["mask_input"] = reused_mask_input[None, :, :]
                predict_kwargs["multimask_output"] = False
            with torch.inference_mode():
                masks, scores, logits = predictor.predict(**predict_kwargs)

            if len(masks) == 0:
                raise RuntimeError("SAM2 did not return any mask.")

            best_index = int(np.argmax(scores))
            mask = masks[best_index]
            ys, xs = np.nonzero(mask)
            if len(xs) == 0 or len(ys) == 0:
                raise RuntimeError("SAM2 returned an empty mask.")
            self._remember_prompt(points, labels, logits[best_index])

            bbox, contours = self._mask_to_bbox_and_contours(mask)
            return Prediction(
                bbox,
                float(scores[best_index]),
                contours,
            )

    def _ensure_predictor(self) -> Any:
        if self._predictor is not None:
            return self._predictor

        for source_path in os.environ.get("ANNOTAFLOW_SAM2_SOURCE", "").split(os.pathsep):
            source_path = source_path.strip()
            if source_path and source_path not in sys.path:
                sys.path.insert(0, source_path)

        import torch
        from sam2.build_sam import build_sam2
        from sam2.sam2_image_predictor import SAM2ImagePredictor

        checkpoint = os.environ.get("ANNOTAFLOW_SAM2_CHECKPOINT", "").strip()
        config = os.environ.get("ANNOTAFLOW_SAM2_CONFIG", "").strip()
        if not checkpoint or not config:
            raise RuntimeError(
                "Please set ANNOTAFLOW_SAM2_CHECKPOINT and ANNOTAFLOW_SAM2_CONFIG "
                "before starting the SAM2 service."
            )

        device_name = os.environ.get("ANNOTAFLOW_SAM2_DEVICE", "").strip()
        if not device_name:
            device_name = "cuda" if torch.cuda.is_available() else "cpu"

        model = build_sam2(config, checkpoint, device=device_name)
        self._predictor = SAM2ImagePredictor(model)
        return self._predictor

    def _ensure_image_loaded(self, predictor: Any, image_path: str) -> None:
        from PIL import Image
        import numpy as np

        path = Path(image_path)
        if not path.exists():
            raise FileNotFoundError(f"Image not found: {image_path}")

        key = (str(path.resolve()), path.stat().st_mtime)
        if key == self._loaded_image_key:
            return

        with Image.open(path) as image:
            predictor.set_image(np.array(image.convert("RGB")))
        self._loaded_image_key = key
        self._clear_prompt_cache()

    def _mask_to_bbox_and_contours(self, mask: Any) -> tuple[list[float], list[list[list[float]]]]:
        import cv2
        import numpy as np

        binary = mask.astype(np.uint8) * 255
        contours, _ = cv2.findContours(binary, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
        contours = [contour for contour in contours if cv2.contourArea(contour) >= 4]
        contours.sort(key=cv2.contourArea, reverse=True)

        if not contours:
            ys, xs = np.nonzero(mask)
            x_min = float(xs.min())
            y_min = float(ys.min())
            x_max = float(xs.max())
            y_max = float(ys.max())
            return [x_min, y_min, x_max - x_min + 1.0, y_max - y_min + 1.0], []

        x, y, w, h = cv2.boundingRect(contours[0])
        serialized: list[list[list[float]]] = []
        for contour in contours[:8]:
            epsilon = max(1.0, 0.002 * cv2.arcLength(contour, True))
            approx = cv2.approxPolyDP(contour, epsilon, True).reshape(-1, 2)
            if len(approx) > 600:
                step = max(1, len(approx) // 600)
                approx = approx[::step]
            serialized.append([[float(px), float(py)] for px, py in approx])
        return [float(x), float(y), float(w), float(h)], serialized

    def _clear_prompt_cache(self) -> None:
        self._last_prompt_image_key = None
        self._last_prompt_points = ()
        self._last_prompt_labels = ()
        self._last_best_mask_input = None

    def _reuse_mask_input(
        self,
        points: list[tuple[float, float]],
        labels: list[int],
    ) -> Any | None:
        if self._loaded_image_key is None or self._last_best_mask_input is None:
            return None
        if self._last_prompt_image_key != self._loaded_image_key:
            return None

        current_points = tuple((float(x), float(y)) for x, y in points)
        current_labels = tuple(int(label) for label in labels)
        if len(current_points) != len(self._last_prompt_points) + 1:
            return None
        if current_points[:-1] != self._last_prompt_points:
            return None
        if current_labels[:-1] != self._last_prompt_labels:
            return None
        return self._last_best_mask_input

    def _remember_prompt(
        self,
        points: list[tuple[float, float]],
        labels: list[int],
        best_logits: Any,
    ) -> None:
        import numpy as np

        self._last_prompt_image_key = self._loaded_image_key
        self._last_prompt_points = tuple((float(x), float(y)) for x, y in points)
        self._last_prompt_labels = tuple(int(label) for label in labels)
        self._last_best_mask_input = np.asarray(best_logits, dtype=np.float32).copy()


def create_backend(use_mock: bool = False) -> MockBackend | SAM2Backend:
    if use_mock or os.environ.get("ANNOTAFLOW_SAM2_BACKEND", "").lower() == "mock":
        return MockBackend()
    return SAM2Backend()
