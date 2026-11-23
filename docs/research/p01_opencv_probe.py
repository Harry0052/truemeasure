#!/usr/bin/env python3
"""P01 comparison probe. Not part of the TrueMeasure product.

Shared fixture: a 200 mm by 150 mm white board, OpenCV DICT_4X4_50 markers
of 30 mm, and a 60 mm segment. The oblique view is a single perspective warp.
The script reports what OpenCV's homography primitive returns, including on
weak marker sets, and what a two-point scale-bar analogue returns.
"""

import json

import cv2
import numpy as np

PPM = 10
BOARD_W_MM, BOARD_H_MM = 200.0, 150.0
MARKER_MM = 30.0
MARKERS = {
    0: (10.0, 10.0),
    1: (160.0, 10.0),
    2: (10.0, 110.0),
    3: (160.0, 110.0),
    4: (85.0, 10.0),
    5: (85.0, 110.0),
}
SEG_A = np.array([40.0, 70.0], dtype=np.float64)
SEG_B = np.array([100.0, 70.0], dtype=np.float64)


def main() -> None:
    dictionary = cv2.aruco.Dictionary_get(cv2.aruco.DICT_4X4_50)
    side = int(MARKER_MM * PPM)
    board = np.full((int(BOARD_H_MM * PPM), int(BOARD_W_MM * PPM), 3), 255, np.uint8)
    for marker_id, (x_mm, y_mm) in MARKERS.items():
        tile = cv2.aruco.drawMarker(dictionary, marker_id, side)
        x = int(round(x_mm * PPM))
        y = int(round(y_mm * PPM))
        board[y : y + side, x : x + side] = cv2.cvtColor(tile, cv2.COLOR_GRAY2BGR)

    src = np.array(
        [[0, 0], [BOARD_W_MM * PPM, 0], [BOARD_W_MM * PPM, BOARD_H_MM * PPM], [0, BOARD_H_MM * PPM]],
        np.float32,
    )
    dst = np.array([[180, 120], [1480, 210], [1320, 1080], [240, 1020]], np.float32)
    render = cv2.getPerspectiveTransform(src, dst)
    oblique = cv2.warpPerspective(board, render, (1600, 1200), borderValue=(240, 240, 240))

    def to_img(point_mm: np.ndarray) -> np.ndarray:
        pixel = np.array([[[point_mm[0] * PPM, point_mm[1] * PPM]]], np.float32)
        return cv2.perspectiveTransform(pixel, render)[0, 0]

    img_a = to_img(SEG_A)
    img_b = to_img(SEG_B)
    endpoints = np.array([[img_a, img_b]], np.float64)
    params = cv2.aruco.DetectorParameters_create()
    corners, ids, _ = cv2.aruco.detectMarkers(oblique, dictionary, parameters=params)

    def homography(id_set):
        image_points = []
        board_points = []
        for corner, marker_id in zip(corners, ids.flatten()):
            if int(marker_id) not in id_set:
                continue
            origin = MARKERS[int(marker_id)]
            local = [(0, 0), (MARKER_MM, 0), (MARKER_MM, MARKER_MM), (0, MARKER_MM)]
            for index, offset in enumerate(local):
                image_points.append(corner[0][index])
                board_points.append([origin[0] + offset[0], origin[1] + offset[1]])
        image_points = np.array(image_points, np.float64)
        board_points = np.array(board_points, np.float64)
        if len(image_points) < 4:
            return {"homography": None, "reason": "fewer than 4 point correspondences"}
        try:
            matrix, _ = cv2.findHomography(image_points, board_points, 0)
        except cv2.error as error:
            return {"homography": None, "reason": str(error).splitlines()[0]}
        if matrix is None:
            return {"homography": None, "reason": "findHomography returned empty"}
        mapped = cv2.perspectiveTransform(endpoints, matrix)[0]
        distance = float(np.linalg.norm(mapped[1] - mapped[0]))
        hull = cv2.convexHull(board_points.astype(np.float32))
        return {
            "homography": True,
            "distance_mm": distance,
            "abs_error_mm": abs(distance - 60.0),
            "hull_area_mm2": float(cv2.contourArea(hull)),
            "reports_interval": False,
            "withholds": False,
        }

    marker0 = next(corner for corner, marker_id in zip(corners, ids.flatten()) if int(marker_id) == 0)
    scale = MARKER_MM / float(np.linalg.norm(marker0[0][1] - marker0[0][0]))
    naive = float(np.linalg.norm(img_b - img_a) * scale)
    report = {
        "opencv_version": cv2.__version__,
        "true_distance_mm": 60.0,
        "detected_ids": sorted(int(marker_id) for marker_id in ids.flatten()),
        "full_board": homography({0, 1, 2, 3, 4, 5}),
        "top_edge_two_markers": homography({0, 1}),
        "single_marker": homography({0}),
        "naive_scalebar_mm": naive,
        "naive_scalebar_abs_error_mm": abs(naive - 60.0),
    }
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
