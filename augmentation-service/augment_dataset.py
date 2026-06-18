from __future__ import annotations

import argparse
import html
import json
import math
import random
import re
from pathlib import Path
from typing import Any

import cv2
import numpy as np


def read_image(path: str) -> np.ndarray:
    data = np.fromfile(path, dtype=np.uint8)
    image = cv2.imdecode(data, cv2.IMREAD_COLOR)
    if image is None:
        raise RuntimeError(f"无法读取图片: {path}")
    return image


def write_image(path: Path, image: np.ndarray, quality: int = 95) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    extension = path.suffix.lower()
    params: list[int] = []
    if extension in {".jpg", ".jpeg"}:
        params = [cv2.IMWRITE_JPEG_QUALITY, quality]
    ok, encoded = cv2.imencode(extension, image, params)
    if not ok:
        raise RuntimeError(f"无法编码图片: {path}")
    encoded.tofile(str(path))


def sanitize_token(text: str) -> str:
    text = re.sub(r"[^0-9A-Za-z_-]+", "-", text).strip("-_")
    return text or "aug"


def sanitize_stem(text: str) -> str:
    text = re.sub(r"[^\w-]+", "-", text, flags=re.UNICODE).strip("-_")
    return text[:80] or "image"


def percent(value: float) -> int:
    return int(round(value * 100))


def signed_integer(value: float) -> int:
    rounded = int(round(value))
    return 0 if rounded == 0 else rounded


def compact_chain(tokens: list[str], maximum_length: int = 96) -> str:
    cleaned = [sanitize_token(token) for token in tokens]
    selected: list[str] = []
    for index, token in enumerate(cleaned):
        candidate = "_".join([*selected, token])
        remaining = len(cleaned) - index - 1
        suffix = f"_plus{remaining}" if remaining else ""
        if selected and len(candidate + suffix) > maximum_length:
            selected.append(f"plus{len(cleaned) - index}")
            break
        selected.append(token)
    return "_".join(selected) or "aug"


def unique_output_path(output_dir: Path, stem: str, extension: str) -> Path:
    candidate = output_dir / f"{stem}{extension}"
    suffix = 2
    while candidate.exists():
        candidate = output_dir / f"{stem}_{suffix}{extension}"
        suffix += 1
    return candidate


OPERATION_NAMES = {
    "hflip": "水平翻转", "vflip": "垂直翻转", "mosaic": "Mosaic 四图拼接",
    "cutmix": "CutMix", "paste": "Copy-Paste", "gray": "灰度化",
    "sp": "椒盐噪声", "erase": "随机擦除", "grid": "GridMask",
    "hide": "Hide-and-Seek", "shift": "随机平移",
}


def describe_operation(token: str) -> str:
    if token in OPERATION_NAMES:
        return OPERATION_NAMES[token]
    patterns = [
        (r"rot([+-]?\d+)", "旋转 {}°"),
        (r"scale(\d+)", "缩放至 {}%"),
        (r"shear([+-]?\d+)", "仿射剪切 {}°"),
        (r"rcrop(\d+)", "随机裁剪，保留 {}%"),
        (r"ccrop(\d+)", "中心裁剪，保留 {}%"),
        (r"persp(\d+)", "透视扰动 {}%"),
        (r"bri(\d+)", "亮度系数 {}%"),
        (r"con(\d+)", "对比度系数 {}%"),
        (r"sat(\d+)", "饱和度系数 {}%"),
        (r"hue([+-]?\d+)", "色相偏移 {}°"),
        (r"gam(\d+)", "Gamma 系数 {}%"),
        (r"noise(\d+)", "高斯噪声 σ={}"),
        (r"gblur(\d+)", "高斯模糊核 {}"),
        (r"mblur(\d+)", "运动模糊核 {}"),
        (r"jpg(\d+)", "JPEG 质量 {}"),
        (r"cutout(\d+)", "Cutout 边长 {}%"),
        (r"mix(\d+)", "MixUp 主图权重 {}%"),
        (r"plus(\d+)", "另有 {} 项，详见 JSON"),
    ]
    for pattern, template in patterns:
        match = re.fullmatch(pattern, token)
        if match:
            return template.format(match.group(1))
    return token


def write_readable_reports(
    output_root: Path,
    result: dict[str, Any],
    schemes: list[dict[str, Any]],
) -> None:
    scheme_rows = "".join(
        "<tr>"
        f"<td>{index}</td><td>{scheme.get('copies_per_image', 1)}</td>"
        f"<td>{float(scheme.get('probability', 1)):.0%}</td>"
        f"<td>{html.escape('、'.join(scheme.get('operations', {}).keys()))}</td>"
        "</tr>"
        for index, scheme in enumerate(schemes, 1)
    )
    detail_rows = "".join(
        "<tr>"
        f"<td>{html.escape(Path(item['image_path']).name)}</td>"
        f"<td>{html.escape(Path(item['source_image']).name)}</td>"
        f"<td>{item.get('scheme', 1)}</td>"
        f"<td>{html.escape('；'.join(describe_operation(token) for token in item['operations']))}</td>"
        f"<td>{len(item['boxes'])}</td>"
        "</tr>"
        for item in result["items"]
    )
    report = f"""<!doctype html><html lang="zh-CN"><head><meta charset="utf-8">
<title>AnnotaFlow 数据增强报告</title><style>
body{{font-family:"Microsoft YaHei",sans-serif;margin:28px;color:#20262b}}
h1{{font-size:25px}} .summary{{background:#f2f4f5;padding:14px;border:1px solid #cbd0d4}}
table{{border-collapse:collapse;width:100%;margin:14px 0 28px}}
th,td{{border:1px solid #cbd0d4;padding:8px;text-align:left;vertical-align:top}}
th{{background:#eceff1}} tr:nth-child(even){{background:#f8f9fa}}
</style></head><body><h1>AnnotaFlow 数据增强报告</h1>
<div class="summary">共生成 <strong>{result['generated']}</strong> 张增强图片；
随机种子：<strong>{result['seed']}</strong>。</div>
<h2>增强方案</h2><table><thead><tr><th>方案</th><th>每张生成</th>
<th>应用概率</th><th>所选方法</th></tr></thead><tbody>{scheme_rows}</tbody></table>
<h2>生成明细</h2><table><thead><tr><th>增强图片</th><th>原图</th>
<th>方案</th><th>实际执行操作</th><th>框数</th></tr></thead>
<tbody>{detail_rows}</tbody></table></body></html>"""
    (output_root / "augmentation_report.html").write_text(report, encoding="utf-8")


def clip_boxes(boxes: list[dict[str, Any]], width: int, height: int) -> list[dict[str, Any]]:
    result = []
    for box in boxes:
        x1 = max(0.0, min(float(width), float(box["x"])))
        y1 = max(0.0, min(float(height), float(box["y"])))
        x2 = max(0.0, min(float(width), float(box["x"]) + float(box["w"])))
        y2 = max(0.0, min(float(height), float(box["y"]) + float(box["h"])))
        if x2 - x1 >= 2.0 and y2 - y1 >= 2.0:
            result.append({"label": box["label"], "x": x1, "y": y1, "w": x2 - x1, "h": y2 - y1})
    return result


def occlusion_ratio_for_box(box: dict[str, Any], rect: tuple[int, int, int, int]) -> float:
    rx, ry, rw, rh = rect
    bx1, by1 = float(box["x"]), float(box["y"])
    bx2, by2 = bx1 + float(box["w"]), by1 + float(box["h"])
    ix1, iy1 = max(bx1, rx), max(by1, ry)
    ix2, iy2 = min(bx2, rx + rw), min(by2, ry + rh)
    inter = max(0.0, ix2 - ix1) * max(0.0, iy2 - iy1)
    area = max(1.0, float(box["w"]) * float(box["h"]))
    return inter / area


def choose_safe_occlusion_rect(
    width: int,
    height: int,
    rect_w: int,
    rect_h: int,
    boxes: list[dict[str, Any]],
    rng: random.Random,
    max_box_occlusion: float = 0.40,
    attempts: int = 80,
) -> tuple[int, int, int, int] | None:
    if rect_w <= 0 or rect_h <= 0 or rect_w > width or rect_h > height:
        return None
    for _ in range(attempts):
        x = rng.randint(0, width - rect_w)
        y = rng.randint(0, height - rect_h)
        rect = (x, y, rect_w, rect_h)
        if all(occlusion_ratio_for_box(box, rect) <= max_box_occlusion for box in boxes):
            return rect
    return None


def transform_boxes(
    boxes: list[dict[str, Any]], matrix: np.ndarray, width: int, height: int
) -> list[dict[str, Any]]:
    transformed = []
    perspective = matrix.shape == (3, 3)
    for box in boxes:
        x, y, w, h = (float(box[key]) for key in ("x", "y", "w", "h"))
        corners = np.array([[[x, y], [x + w, y], [x + w, y + h], [x, y + h]]], dtype=np.float32)
        points = (
            cv2.perspectiveTransform(corners, matrix)
            if perspective
            else cv2.transform(corners, matrix)
        )[0]
        x1, y1 = points.min(axis=0)
        x2, y2 = points.max(axis=0)
        transformed.append({"label": box["label"], "x": x1, "y": y1, "w": x2 - x1, "h": y2 - y1})
    return clip_boxes(transformed, width, height)


def warp_affine(
    image: np.ndarray, boxes: list[dict[str, Any]], matrix: np.ndarray
) -> tuple[np.ndarray, list[dict[str, Any]]]:
    height, width = image.shape[:2]
    output = cv2.warpAffine(
        image, matrix, (width, height), flags=cv2.INTER_LINEAR,
        borderMode=cv2.BORDER_CONSTANT, borderValue=(114, 114, 114)
    )
    return output, transform_boxes(boxes, matrix, width, height)


def choose_range(config: dict[str, Any], rng: random.Random) -> float:
    return rng.uniform(float(config["min"]), float(config["max"]))


def resize_sample(
    image: np.ndarray, boxes: list[dict[str, Any]], width: int, height: int
) -> tuple[np.ndarray, list[dict[str, Any]]]:
    source_h, source_w = image.shape[:2]
    resized = cv2.resize(image, (width, height), interpolation=cv2.INTER_LINEAR)
    sx, sy = width / source_w, height / source_h
    scaled = [
        {"label": box["label"], "x": box["x"] * sx, "y": box["y"] * sy,
         "w": box["w"] * sx, "h": box["h"] * sy}
        for box in boxes
    ]
    return resized, clip_boxes(scaled, width, height)


def random_sample(samples: list[dict[str, Any]], rng: random.Random) -> tuple[np.ndarray, list[dict[str, Any]], str]:
    sample = rng.choice(samples)
    return read_image(sample["image_path"]), [dict(box) for box in sample["boxes"]], sample["image_path"]


def apply_mosaic(
    samples: list[dict[str, Any]], width: int, height: int, rng: random.Random
) -> tuple[np.ndarray, list[dict[str, Any]], list[str]]:
    canvas = np.full((height, width, 3), 114, dtype=np.uint8)
    boxes: list[dict[str, Any]] = []
    sources: list[str] = []
    placements = [
        (0, 0, width // 2, height // 2),
        (width // 2, 0, width - width // 2, height // 2),
        (0, height // 2, width // 2, height - height // 2),
        (width // 2, height // 2, width - width // 2, height - height // 2),
    ]
    for x, y, cell_w, cell_h in placements:
        image, source_boxes, source = random_sample(samples, rng)
        image, source_boxes = resize_sample(image, source_boxes, cell_w, cell_h)
        canvas[y:y + cell_h, x:x + cell_w] = image
        for box in source_boxes:
            box["x"] += x
            box["y"] += y
            boxes.append(box)
        sources.append(source)
    return canvas, clip_boxes(boxes, width, height), sources


def apply_operations(
    base_image: np.ndarray,
    base_boxes: list[dict[str, Any]],
    samples: list[dict[str, Any]],
    operations: dict[str, Any],
    probability: float,
    rng: random.Random,
) -> tuple[np.ndarray, list[dict[str, Any]], list[str], list[str]]:
    image = base_image.copy()
    boxes = [dict(box) for box in base_boxes]
    height, width = image.shape[:2]
    chain: list[str] = []
    sources: list[str] = []

    def enabled(name: str) -> bool:
        return name in operations and rng.random() <= probability

    if enabled("mosaic"):
        image, boxes, mosaic_sources = apply_mosaic(samples, width, height, rng)
        sources.extend(mosaic_sources)
        chain.append("mosaic")

    if enabled("mixup"):
        other, other_boxes, source = random_sample(samples, rng)
        other, other_boxes = resize_sample(other, other_boxes, width, height)
        alpha = rng.uniform(0.35, 0.65)
        image = cv2.addWeighted(image, alpha, other, 1.0 - alpha, 0)
        boxes.extend(other_boxes)
        sources.append(source)
        chain.append(f"mix{percent(alpha)}")

    if enabled("cutmix"):
        other, other_boxes, source = random_sample(samples, rng)
        other, other_boxes = resize_sample(other, other_boxes, width, height)
        cut_w, cut_h = rng.randint(max(2, width // 5), max(3, width // 2)), rng.randint(max(2, height // 5), max(3, height // 2))
        x1, y1 = rng.randint(0, max(0, width - cut_w)), rng.randint(0, max(0, height - cut_h))
        x2, y2 = x1 + cut_w, y1 + cut_h
        image[y1:y2, x1:x2] = other[y1:y2, x1:x2]
        for box in other_boxes:
            bx1, by1 = max(x1, box["x"]), max(y1, box["y"])
            bx2, by2 = min(x2, box["x"] + box["w"]), min(y2, box["y"] + box["h"])
            if bx2 - bx1 >= 2 and by2 - by1 >= 2:
                boxes.append({"label": box["label"], "x": bx1, "y": by1, "w": bx2 - bx1, "h": by2 - by1})
        sources.append(source)
        chain.append("cutmix")

    if enabled("copy_paste"):
        other, other_boxes, source = random_sample(samples, rng)
        other, other_boxes = resize_sample(other, other_boxes, width, height)
        candidates = [box for box in other_boxes if box["w"] >= 3 and box["h"] >= 3]
        if candidates:
            source_box = rng.choice(candidates)
            sx, sy = int(source_box["x"]), int(source_box["y"])
            sw, sh = int(source_box["w"]), int(source_box["h"])
            patch = other[sy:sy + sh, sx:sx + sw]
            if patch.size:
                dx, dy = rng.randint(0, max(0, width - sw)), rng.randint(0, max(0, height - sh))
                image[dy:dy + sh, dx:dx + sw] = patch
                boxes.append({"label": source_box["label"], "x": dx, "y": dy, "w": sw, "h": sh})
                sources.append(source)
                chain.append("paste")

    if enabled("horizontal_flip"):
        image = cv2.flip(image, 1)
        boxes = [{"label": box["label"], "x": width - box["x"] - box["w"], "y": box["y"], "w": box["w"], "h": box["h"]} for box in boxes]
        chain.append("hflip")
    if enabled("vertical_flip"):
        image = cv2.flip(image, 0)
        boxes = [{"label": box["label"], "x": box["x"], "y": height - box["y"] - box["h"], "w": box["w"], "h": box["h"]} for box in boxes]
        chain.append("vflip")

    angle = choose_range(operations["rotate"], rng) if enabled("rotate") else 0.0
    scale = choose_range(operations["scale"], rng) if enabled("scale") else 1.0
    tx = choose_range(operations["translate"], rng) * width if enabled("translate") else 0.0
    ty = choose_range(operations["translate"], rng) * height if "translate" in operations and tx else 0.0
    shear = choose_range(operations["shear"], rng) if enabled("shear") else 0.0
    if angle or scale != 1.0 or tx or ty or shear:
        matrix = cv2.getRotationMatrix2D((width / 2, height / 2), angle, scale)
        matrix[0, 1] += math.tan(math.radians(shear))
        matrix[0, 2] += tx
        matrix[1, 2] += ty
        image, boxes = warp_affine(image, boxes, matrix)
        if angle:
            chain.append(f"rot{signed_integer(angle):+d}")
        if scale != 1.0:
            chain.append(f"scale{percent(scale)}")
        if tx or ty:
            chain.append("shift")
        if shear:
            chain.append(f"shear{signed_integer(shear):+d}")

    if enabled("crop"):
        ratio = choose_range(operations["crop"], rng)
        crop_w, crop_h = max(2, int(width * ratio)), max(2, int(height * ratio))
        if rng.random() < 0.5:
            x1, y1 = (width - crop_w) // 2, (height - crop_h) // 2
            token = "ccrop"
        else:
            x1, y1 = rng.randint(0, width - crop_w), rng.randint(0, height - crop_h)
            token = "rcrop"
        image = image[y1:y1 + crop_h, x1:x1 + crop_w]
        shifted = [{"label": box["label"], "x": box["x"] - x1, "y": box["y"] - y1, "w": box["w"], "h": box["h"]} for box in boxes]
        boxes = clip_boxes(shifted, crop_w, crop_h)
        image, boxes = resize_sample(image, boxes, width, height)
        chain.append(f"{token}{percent(ratio)}")

    if enabled("perspective"):
        amount = choose_range(operations["perspective"], rng)
        source = np.float32([[0, 0], [width, 0], [width, height], [0, height]])
        jitter = np.float32([[rng.uniform(-amount, amount) * width, rng.uniform(-amount, amount) * height] for _ in range(4)])
        target = source + jitter
        matrix = cv2.getPerspectiveTransform(source, target)
        image = cv2.warpPerspective(image, matrix, (width, height), borderValue=(114, 114, 114))
        boxes = transform_boxes(boxes, matrix, width, height)
        chain.append(f"persp{percent(amount)}")

    if enabled("brightness"):
        value = choose_range(operations["brightness"], rng)
        image = np.clip(image.astype(np.float32) * value, 0, 255).astype(np.uint8)
        chain.append(f"bri{percent(value)}")
    if enabled("contrast"):
        value = choose_range(operations["contrast"], rng)
        mean = image.mean(axis=(0, 1), keepdims=True)
        image = np.clip((image.astype(np.float32) - mean) * value + mean, 0, 255).astype(np.uint8)
        chain.append(f"con{percent(value)}")
    if enabled("saturation") or enabled("hue"):
        hsv = cv2.cvtColor(image, cv2.COLOR_BGR2HSV).astype(np.float32)
        if "saturation" in operations and rng.random() <= probability:
            value = choose_range(operations["saturation"], rng)
            hsv[:, :, 1] = np.clip(hsv[:, :, 1] * value, 0, 255)
            chain.append(f"sat{percent(value)}")
        if "hue" in operations and rng.random() <= probability:
            value = choose_range(operations["hue"], rng)
            hsv[:, :, 0] = (hsv[:, :, 0] + value / 2.0) % 180
            chain.append(f"hue{value:+.0f}")
        image = cv2.cvtColor(hsv.astype(np.uint8), cv2.COLOR_HSV2BGR)
    if enabled("grayscale"):
        gray = cv2.cvtColor(image, cv2.COLOR_BGR2GRAY)
        image = cv2.cvtColor(gray, cv2.COLOR_GRAY2BGR)
        chain.append("gray")
    if enabled("gamma"):
        value = choose_range(operations["gamma"], rng)
        table = np.array([((i / 255.0) ** value) * 255 for i in range(256)], dtype=np.uint8)
        image = cv2.LUT(image, table)
        chain.append(f"gam{percent(value)}")

    if enabled("gaussian_noise"):
        sigma = choose_range(operations["gaussian_noise"], rng)
        noise = np.random.default_rng(rng.randrange(2**32)).normal(0, sigma, image.shape)
        image = np.clip(image.astype(np.float32) + noise, 0, 255).astype(np.uint8)
        chain.append(f"noise{sigma:.0f}")
    if enabled("salt_pepper"):
        count = max(1, int(width * height * rng.uniform(0.002, 0.015)))
        ys = np.random.default_rng(rng.randrange(2**32)).integers(0, height, count)
        xs = np.random.default_rng(rng.randrange(2**32)).integers(0, width, count)
        image[ys, xs] = np.where(np.arange(count)[:, None] % 2, 255, 0)
        chain.append("sp")
    if enabled("gaussian_blur"):
        kernel = max(1, int(round(choose_range(operations["gaussian_blur"], rng))) | 1)
        image = cv2.GaussianBlur(image, (kernel, kernel), 0)
        chain.append(f"gblur{kernel}")
    if enabled("motion_blur"):
        kernel = max(3, int(round(choose_range(operations["motion_blur"], rng))) | 1)
        matrix = np.zeros((kernel, kernel), dtype=np.float32)
        if rng.random() < 0.5:
            matrix[kernel // 2, :] = 1.0
        else:
            np.fill_diagonal(matrix, 1.0)
        image = cv2.filter2D(image, -1, matrix / matrix.sum())
        chain.append(f"mblur{kernel}")
    if enabled("jpeg"):
        quality = int(choose_range(operations["jpeg"], rng))
        ok, encoded = cv2.imencode(".jpg", image, [cv2.IMWRITE_JPEG_QUALITY, quality])
        if ok:
            image = cv2.imdecode(encoded, cv2.IMREAD_COLOR)
        chain.append(f"jpg{quality}")

    if enabled("cutout"):
        ratio = choose_range(operations["cutout"], rng)
        side = max(2, int(min(width, height) * ratio))
        rect = choose_safe_occlusion_rect(width, height, side, side, boxes, rng)
        if rect is not None:
            x, y, rect_w, rect_h = rect
            image[y:y + rect_h, x:x + rect_w] = rng.randint(0, 114)
            chain.append(f"cutout{percent(ratio)}")
    if enabled("random_erasing"):
        erase_w, erase_h = rng.randint(max(2, width // 12), max(3, width // 3)), rng.randint(max(2, height // 12), max(3, height // 3))
        x, y = rng.randint(0, width - erase_w), rng.randint(0, height - erase_h)
        image[y:y + erase_h, x:x + erase_w] = np.random.default_rng(rng.randrange(2**32)).integers(0, 256, (erase_h, erase_w, 3), dtype=np.uint8)
        chain.append("erase")
    if enabled("gridmask"):
        period = rng.randint(24, max(25, min(width, height) // 4))
        thickness = max(2, period // 4)
        for x in range(rng.randrange(period), width, period):
            image[:, x:x + thickness] = 114
        for y in range(rng.randrange(period), height, period):
            image[y:y + thickness, :] = 114
        chain.append("grid")
    if enabled("hide_seek"):
        grid = rng.randint(3, 8)
        cell_w, cell_h = max(1, width // grid), max(1, height // grid)
        for gy in range(grid):
            for gx in range(grid):
                if rng.random() < 0.35:
                    image[gy * cell_h:min(height, (gy + 1) * cell_h), gx * cell_w:min(width, (gx + 1) * cell_w)] = 114
        chain.append("hide")

    boxes = clip_boxes(boxes, width, height)
    return image, boxes, chain or ["identity"], sources


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--job", required=True)
    parser.add_argument("--result", required=True)
    args = parser.parse_args()

    job = json.loads(Path(args.job).read_text(encoding="utf-8-sig"))
    config = job["config"]
    samples = job["samples"]
    output_dir = Path(job["images_output"])
    output_dir.mkdir(parents=True, exist_ok=True)
    rng = random.Random(int(config["seed"]))
    schemes = job.get("schemes")
    if not schemes:
        schemes = [config]
    results = []
    total = len(samples) * sum(int(scheme["copies_per_image"]) for scheme in schemes)
    current = 0

    for scheme_index, scheme in enumerate(schemes):
        probability = float(scheme["probability"])
        operations = scheme["operations"]
        for sample in samples:
            base_image = read_image(sample["image_path"])
            base_boxes = sample["boxes"]
            original_path = Path(sample["image_path"])
            for _ in range(int(scheme["copies_per_image"])):
                current += 1
                image, boxes, chain, sources = apply_operations(
                    base_image, base_boxes, samples, operations, probability, rng
                )
                extension = original_path.suffix.lower()
                if config["image_format"] == "jpg":
                    extension = ".jpg"
                elif config["image_format"] == "png":
                    extension = ".png"
                elif extension not in {".jpg", ".jpeg", ".png", ".bmp", ".tif", ".tiff"}:
                    extension = ".jpg"
                chain_token = compact_chain(chain)
                output_path = unique_output_path(
                    output_dir,
                    f"{sanitize_stem(original_path.stem)}__{chain_token}",
                    extension,
                )
                file_name = output_path.name
                write_image(output_path, image)
                results.append({
                    "image_path": str(output_path),
                    "source_image": sample["image_path"],
                    "source_images": [sample["image_path"], *sources],
                    "scheme": scheme_index + 1,
                    "width": int(image.shape[1]),
                    "height": int(image.shape[0]),
                    "boxes": boxes,
                    "operations": chain,
                })
                print(json.dumps({"progress": current, "total": total, "file": file_name}, ensure_ascii=False), flush=True)

    result = {
        "ok": True,
        "seed": config["seed"],
        "generated": len(results),
        "items": results,
    }
    Path(args.result).write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
    (output_dir.parent / "augmentation_manifest.json").write_text(
        json.dumps({"config": config, "schemes": schemes, **result}, ensure_ascii=False, indent=2),
        encoding="utf-8",
    )
    write_readable_reports(output_dir.parent, result, schemes)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
