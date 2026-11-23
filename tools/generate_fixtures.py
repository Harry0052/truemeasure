#!/usr/bin/env python3
"""Generate the versioned TrueMeasure board, views, and request files.

Marker artwork comes only from OpenCV's DICT_4X4_50 renderer. The script does
not download images. Run it from the repository root with python3-opencv 4.6.
"""

from __future__ import annotations

import hashlib
import json
from pathlib import Path

import cv2
import numpy as np

ROOT = Path(__file__).resolve().parents[1]
FIXTURES = ROOT / "fixtures"
SEED = 20261123
BOARD_ID = "tm-board-v1"
BOARD_W_MM = 240.0
BOARD_H_MM = 170.0
MARKER_MM = 28.0
PPM = 10
# Top-left corners, millimetres, origin at the board's top left, +y down.
MARKERS = [
    (0, 12.0, 12.0),
    (4, 74.0, 12.0),
    (6, 136.0, 12.0),
    (1, 198.0, 12.0),
    (2, 12.0, 130.0),
    (5, 106.0, 130.0),
    (3, 198.0, 130.0),
]
SEG_A = (90.0, 85.0)
SEG_B = (150.0, 85.0)
TOP_IDS = {0, 4, 6, 1}
REFERENCE_MM = float(np.hypot(SEG_B[0] - SEG_A[0], SEG_B[1] - SEG_A[1]))


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    digest.update(path.read_bytes())
    return digest.hexdigest()


def draw_board(ids: set[int] | None, line: bool) -> np.ndarray:
    dictionary = cv2.aruco.Dictionary_get(cv2.aruco.DICT_4X4_50)
    side = int(round(MARKER_MM * PPM))
    image = np.full((int(BOARD_H_MM * PPM), int(BOARD_W_MM * PPM), 3), 255, np.uint8)
    for marker_id, x_mm, y_mm in MARKERS:
        if ids is not None and marker_id not in ids:
            continue
        tile = cv2.aruco.drawMarker(dictionary, marker_id, side)
        x = int(round(x_mm * PPM))
        y = int(round(y_mm * PPM))
        image[y : y + side, x : x + side] = cv2.cvtColor(tile, cv2.COLOR_GRAY2BGR)
    if line:
        a = (int(round(SEG_A[0] * PPM)), int(round(SEG_A[1] * PPM)))
        b = (int(round(SEG_B[0] * PPM)), int(round(SEG_B[1] * PPM)))
        cv2.line(image, a, b, (30, 30, 160), 3)
        cv2.circle(image, a, 6, (20, 20, 140), -1)
        cv2.circle(image, b, 6, (20, 20, 140), -1)
    return image


def warp(board: np.ndarray, dst: np.ndarray, size: tuple[int, int]) -> tuple[np.ndarray, np.ndarray]:
    height, width = board.shape[:2]
    src = np.array([[0, 0], [width - 1, 0], [width - 1, height - 1], [0, height - 1]], np.float32)
    matrix = cv2.getPerspectiveTransform(src, dst.astype(np.float32))
    view = cv2.warpPerspective(
        board, matrix, size, flags=cv2.INTER_LINEAR, borderValue=(236, 236, 236)
    )
    return view, matrix


def project_mm(matrix: np.ndarray, point_mm: tuple[float, float]) -> np.ndarray:
    pixel = np.array([[[point_mm[0] * PPM, point_mm[1] * PPM]]], np.float32)
    return cv2.perspectiveTransform(pixel, matrix)[0, 0]


def distort_image(image: np.ndarray, camera: np.ndarray, distortion: np.ndarray) -> np.ndarray:
    height, width = image.shape[:2]
    ys, xs = np.indices((height, width))
    points = np.stack([xs, ys], axis=-1).reshape(-1, 1, 2).astype(np.float32)
    undistorted = cv2.undistortPoints(points, camera, distortion, P=camera)
    map_x = undistorted[:, 0, 0].reshape(height, width).astype(np.float32)
    map_y = undistorted[:, 0, 1].reshape(height, width).astype(np.float32)
    return cv2.remap(
        image,
        map_x,
        map_y,
        cv2.INTER_LINEAR,
        borderMode=cv2.BORDER_CONSTANT,
        borderValue=(236, 236, 236),
    )


def distort_point(point: np.ndarray, camera: np.ndarray, distortion: np.ndarray) -> np.ndarray:
    x = (float(point[0]) - camera[0, 2]) / camera[0, 0]
    y = (float(point[1]) - camera[1, 2]) / camera[1, 1]
    obj = np.array([[[x, y, 1.0]]], np.float64)
    image, _ = cv2.projectPoints(obj, np.zeros(3), np.zeros(3), camera, distortion)
    return image[0, 0]


def rotate_quad(quad: np.ndarray, degrees: float, center: tuple[float, float]) -> np.ndarray:
    radians = np.deg2rad(degrees)
    rotation = np.array(
        [[np.cos(radians), -np.sin(radians)], [np.sin(radians), np.cos(radians)]]
    )
    return ((quad - center) @ rotation.T) + center


def write_board() -> None:
    payload = {
        "schema": "truemeasure.board",
        "schema_version": 1,
        "board_id": BOARD_ID,
        "units": "millimetre",
        "origin": "top_left_x_right_y_down",
        "dictionary": "DICT_4X4_50",
        "marker_size_mm": MARKER_MM,
        "width_mm": BOARD_W_MM,
        "height_mm": BOARD_H_MM,
        "markers": [
            {"id": marker_id, "x_mm": x_mm, "y_mm": y_mm, "corner": "top_left"}
            for marker_id, x_mm, y_mm in MARKERS
        ],
    }
    path = FIXTURES / "boards" / f"{BOARD_ID}.json"
    path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")


def request(
    image_name: str,
    points: list[tuple[float, float]],
    *,
    camera: dict | None = None,
    coplanar: bool = True,
    scale: float = 0.3,
    click: float = 1.0,
    marker_tol: float = 0.5,
    extra: dict | None = None,
) -> dict:
    payload = {
        "schema": "truemeasure.request",
        "schema_version": 1,
        "board_id": BOARD_ID,
        "board_file": f"../boards/{BOARD_ID}.json",
        "image": f"../images/{image_name}",
        "coplanar_attestation": coplanar,
        "points_px": [
            {"id": "a", "x": round(float(points[0][0]), 4), "y": round(float(points[0][1]), 4)},
            {"id": "b", "x": round(float(points[1][0]), 4), "y": round(float(points[1][1]), 4)},
        ],
        "camera": camera or {"model": "none"},
        "scale_tolerance_mm": scale,
        "endpoint_click_tolerance_px": click,
        "marker_corner_tolerance_px": marker_tol,
    }
    if extra:
        payload.update(extra)
    return payload


def sanity(image: np.ndarray, points: list[np.ndarray], camera=None, distortion=None) -> tuple[float, list[int]]:
    dictionary = cv2.aruco.Dictionary_get(cv2.aruco.DICT_4X4_50)
    params = cv2.aruco.DetectorParameters_create()
    params.cornerRefinementMethod = cv2.aruco.CORNER_REFINE_SUBPIX
    corners, ids, _ = cv2.aruco.detectMarkers(image, dictionary, parameters=params)
    detected = [] if ids is None else [int(i) for i in ids.flatten()]
    if ids is None:
        return float("nan"), detected
    by_id = {marker_id: (x, y) for marker_id, x, y in MARKERS}
    image_pts = []
    board_pts = []
    used = []
    for corner, marker_id in zip(corners, detected):
        if marker_id not in by_id:
            continue
        used.append(marker_id)
        origin = by_id[marker_id]
        local = [(0, 0), (MARKER_MM, 0), (MARKER_MM, MARKER_MM), (0, MARKER_MM)]
        for index, offset in enumerate(local):
            image_pts.append(corner[0][index])
            board_pts.append([origin[0] + offset[0], origin[1] + offset[1]])
    image_pts = np.array(image_pts, np.float64)
    board_pts = np.array(board_pts, np.float64)
    endpoints = np.array(points, np.float64).reshape(-1, 1, 2)
    if camera is not None:
        image_pts = cv2.undistortPoints(
            image_pts.reshape(-1, 1, 2).astype(np.float32), camera, distortion, P=camera
        ).reshape(-1, 2)
        endpoints = cv2.undistortPoints(endpoints.astype(np.float32), camera, distortion, P=camera)
    if len(image_pts) < 4:
        return float("nan"), used
    matrix, _ = cv2.findHomography(image_pts, board_pts, 0)
    mapped = cv2.perspectiveTransform(np.array(endpoints, np.float64).reshape(-1, 1, 2), matrix)
    mapped = mapped.reshape(-1, 2)
    distance = float(np.hypot(mapped[1][0] - mapped[0][0], mapped[1][1] - mapped[0][1]))
    return distance, sorted(used)


def main() -> None:
    for folder in ("boards", "images", "requests", "oracle"):
        (FIXTURES / folder).mkdir(parents=True, exist_ok=True)
    write_board()
    full = draw_board(None, True)
    top = draw_board(TOP_IDS, True)
    pair = draw_board({0, 1}, True)
    blank = np.full_like(full, 236)

    oblique_dst = np.array([[160, 110], [1460, 190], [1320, 1080], [250, 990]], np.float32)
    steep_dst = np.array([[280, 70], [1480, 40], [1180, 1120], [620, 1080]], np.float32)
    views = {
        "oblique_60mm.png": warp(full, oblique_dst, (1600, 1200)),
        "oblique_steep_60mm.png": warp(full, steep_dst, (1600, 1200)),
        "rotated_60mm.png": warp(
            full, rotate_quad(oblique_dst, 12.0, (800, 600)), (1600, 1200)
        ),
        "lowres_60mm.png": warp(full, oblique_dst * 0.5, (800, 600)),
        "top_row_60mm.png": warp(top, oblique_dst, (1600, 1200)),
        "two_markers_60mm.png": warp(pair, oblique_dst, (1600, 1200)),
        "blank_60mm.png": warp(blank, oblique_dst, (1600, 1200)),
    }

    camera = np.array([[1400.0, 0.0, 800.0], [0.0, 1400.0, 600.0], [0.0, 0.0, 1.0]])
    distortion = np.array([-0.15, 0.02, 0.0, 0.0, 0.0])
    oblique, oblique_h = views["oblique_60mm.png"]
    distorted = distort_image(oblique, camera, distortion)
    blurred = cv2.GaussianBlur(oblique, (121, 121), 20)
    cut = 760
    cropped = oblique[:cut, :].copy()

    images = {name: view for name, (view, _) in views.items()}
    images["distorted_60mm.png"] = distorted
    images["blurred_60mm.png"] = blurred
    images["cropped_60mm.png"] = cropped
    for name, image in images.items():
        cv2.imwrite(str(FIXTURES / "images" / name), image)

    def ends(matrix: np.ndarray) -> list[np.ndarray]:
        return [project_mm(matrix, SEG_A), project_mm(matrix, SEG_B)]

    oblique_pts = ends(oblique_h)
    steep_pts = ends(views["oblique_steep_60mm.png"][1])
    rotated_pts = ends(views["rotated_60mm.png"][1])
    lowres_pts = ends(views["lowres_60mm.png"][1])
    outside_board = [project_mm(oblique_h, (-40.0, 85.0)), project_mm(oblique_h, (280.0, 85.0))]
    distorted_pts = [distort_point(point, camera, distortion) for point in oblique_pts]

    camera_json = {
        "model": "opencv_pinhole",
        "fx": 1400.0,
        "fy": 1400.0,
        "cx": 800.0,
        "cy": 600.0,
        "distortion": [-0.15, 0.02, 0.0, 0.0, 0.0],
    }
    requests = {
        "oblique_60mm.json": request("oblique_60mm.png", oblique_pts),
        "oblique_steep_60mm.json": request("oblique_steep_60mm.png", steep_pts),
        "rotated_60mm.json": request("rotated_60mm.png", rotated_pts),
        "lowres_60mm.json": request("lowres_60mm.png", lowres_pts),
        "distorted_60mm.json": request("distorted_60mm.png", distorted_pts, camera=camera_json),
        "distorted_undeclared.json": request("distorted_60mm.png", distorted_pts),
        "top_row_60mm.json": request("top_row_60mm.png", oblique_pts),
        "two_markers_60mm.json": request("two_markers_60mm.png", oblique_pts),
        "blank_60mm.json": request("blank_60mm.png", oblique_pts),
        "blurred_60mm.json": request("blurred_60mm.png", oblique_pts),
        "cropped_60mm.json": request("cropped_60mm.png", oblique_pts),
        "outside_board.json": request("oblique_60mm.png", outside_board),
        "unattested.json": request("oblique_60mm.png", oblique_pts, coplanar=False),
        "nonfinite.json": request("oblique_60mm.png", oblique_pts),
    }
    requests["nonfinite.json"]["points_px"][0]["x"] = 1e309
    for name, payload in requests.items():
        text = json.dumps(payload, indent=2, allow_nan=True)
        if name == "nonfinite.json":
            text = text.replace("Infinity", "1e309")
        (FIXTURES / "requests" / name).write_text(text + "\n", encoding="utf-8")
    (FIXTURES / "requests" / "malformed_image.json").write_text(
        json.dumps(
            request("malformed.png", [np.array([10.0, 10.0]), np.array([40.0, 40.0])]),
            indent=2,
        )
        + "\n",
        encoding="utf-8",
    )
    (FIXTURES / "requests" / "missing_points.json").write_text(
        json.dumps({"schema": "truemeasure.request", "schema_version": 1}, indent=2) + "\n",
        encoding="utf-8",
    )
    (FIXTURES / "images" / "malformed.png").write_bytes(b"not a png\n")

    checks = [
        ("oblique", images["oblique_60mm.png"], oblique_pts, None, None),
        ("steep", images["oblique_steep_60mm.png"], steep_pts, None, None),
        ("rotated", images["rotated_60mm.png"], rotated_pts, None, None),
        ("lowres", images["lowres_60mm.png"], lowres_pts, None, None),
        ("distorted", distorted, distorted_pts, camera, distortion),
        ("distorted_raw", distorted, distorted_pts, None, None),
        ("top", images["top_row_60mm.png"], oblique_pts, None, None),
        ("two", images["two_markers_60mm.png"], oblique_pts, None, None),
        ("blur", blurred, oblique_pts, None, None),
        ("crop", cropped, oblique_pts, None, None),
        ("blank", images["blank_60mm.png"], oblique_pts, None, None),
    ]
    print(f"reference_mm {REFERENCE_MM}")
    failed = False
    for name, image, points, cam, dist in checks:
        distance, ids = sanity(image, points, cam, dist)
        error = abs(distance - REFERENCE_MM) if distance == distance else float("nan")
        print(f"{name:16} ids={ids} distance={distance:.4f} error={error:.4f}" if distance == distance else f"{name:16} ids={ids} distance=nan")
        if name in {"oblique", "steep", "rotated", "lowres", "distorted"} and not (error <= 0.5):
            failed = True
    for point in (*oblique_pts, *outside_board, *distorted_pts):
        if not (0 <= point[0] < 1600 and 0 <= point[1] < 1200):
            print("endpoint left the oblique frame", point)
            failed = True
    if not (0 <= oblique_pts[0][1] < cut and 0 <= oblique_pts[1][1] < cut):
        print("segment is outside the crop", oblique_pts, cut)
        failed = True

    files = []
    for path in sorted(FIXTURES.rglob("*")):
        if path.is_file() and path.name != "manifest.json":
            files.append(
                {
                    "path": path.relative_to(ROOT).as_posix(),
                    "bytes": path.stat().st_size,
                    "sha256": sha256(path),
                }
            )
    manifest = {
        "name": "truemeasure-fixtures",
        "version": 1,
        "seed": SEED,
        "seed_note": "Revision token. Generation is deterministic and does not draw random samples.",
        "generator": "tools/generate_fixtures.py",
        "opencv": cv2.__version__,
        "dictionary": {
            "name": "DICT_4X4_50",
            "renderer": "cv2.aruco.drawMarker",
            "provenance": "Marker bits are rendered by OpenCV at generation time. No external marker image is stored or fetched.",
            "license": "Apache-2.0",
            "source": "https://github.com/opencv/opencv",
        },
        "reference_segment_mm": {
            "a": list(SEG_A),
            "b": list(SEG_B),
            "distance_mm": REFERENCE_MM,
            "method": "Euclidean distance of the board coordinates before projection.",
        },
        "files": files,
    }
    (FIXTURES / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    if failed:
        raise SystemExit("fixture self-check failed")


if __name__ == "__main__":
    main()
