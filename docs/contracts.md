# Input and output contract

Schema version 1. Paths inside a request are resolved from the request file's directory. Distances are millimetres. Pixel coordinates use the image origin at the top left, +x right, +y down.

## Board

`fixtures/boards/tm-board-v1.json`

| Field | Rule |
| --- | --- |
| schema | `truemeasure.board` |
| schema_version | 1 |
| units | `millimetre` |
| origin | top left, +x right, +y down |
| dictionary | `DICT_4X4_50` only |
| marker_size_mm | positive, and each marker square lies inside the board |
| markers | at least five unique integer ids. `x_mm` and `y_mm` are the marker's top-left corner |

Marker artwork is rendered by OpenCV. The corner order used for the homography is top-left, top-right, bottom-right, bottom-left, matching `detectMarkers`.

## Request

| Field | Rule |
| --- | --- |
| schema | `truemeasure.request` |
| schema_version | 1 |
| board_id | must match the board file |
| board_file, image | PNG or JPEG path. Image files larger than 16 MiB are rejected before decode |
| coplanar_attestation | boolean. `false` withholds the dimension |
| points_px | exactly two finite points, distinct ids, inside the decoded image |
| camera.model | `none`, or `opencv_pinhole` with `fx`, `fy`, `cx`, `cy` and five finite distortion coefficients |
| scale_tolerance_mm | 0 to 25 |
| endpoint_click_tolerance_px | 0 to 40 |
| marker_corner_tolerance_px | 0 to 5 |

Decoded images must be at least 32 pixels on a side, at most 4096 pixels on a side, and at most 4096×2160 pixels. JSON inputs must be at most 1 MiB. Non-finite numbers, including a JSON number that overflows, are `nonfinite_point`.

## Result

`truemeasure.result` version 1. The same object is written to stdout and to `--json-out`. Withheld and rejected results still use this shape, with `distance_mm` and `sensitivity_interval_mm` set to null.

| Field | Meaning |
| --- | --- |
| status | `measured`, `withheld`, `rejected`, or `internal` |
| category | null when measured; otherwise a stable code |
| distance_mm | nearest 0.1 mm, or null |
| sensitivity_interval_mm | outward-rounded 0.1 mm bounds, or null |
| units, unit_symbol | `millimetre` and `mm` |
| label | the same text drawn on the annotation, for example `60.0 mm [59.2, 60.8] mm` |
| calibration | detected ids, fit ids, withheld check id, residuals, hull area, minor extent |
| image_sha256, board_sha256, request_sha256 | checksums of the input bytes |
| uncertainty_method | `deterministic_bounds_v1`, described in the field |
| assumptions | planar attestation and the declared distortion model |

`check` prints `truemeasure.check` with status `accepted` or `rejected` and does not report a distance.

## Categories

| Category | Status | When |
| --- | --- | --- |
| schema_invalid | rejected | missing fields, unsupported dictionary or camera, tolerance out of range |
| nonfinite_point | rejected | a coordinate or coefficient is not finite |
| image_malformed | rejected | unreadable, not PNG or JPEG, or smaller than 32 pixels |
| image_oversized | rejected | file, edge, or pixel count above the cap |
| point_outside_image | rejected | an endpoint is outside the decoded photograph |
| coplanarity_not_attested | withheld | the operator did not attest one plane |
| markers_insufficient | withheld | fewer than four board markers |
| markers_ill_spread | withheld | hull area under 8000 mm² or minor extent under 50 mm |
| markers_no_redundant_check | withheld | no marker left to withhold from the fit |
| markers_degenerate | withheld | the homography cannot be formed |
| calibration_residual | withheld | fit or check RMS above 0.35 mm, or a corner above 0.50 mm |
| point_outside_valid_region | withheld | an endpoint maps outside the fit hull, with 0.5 mm slack |
| measurement_undefined | withheld | the sensitivity samples are not finite |

The highest detected marker id is withheld from the fit and used only as a residual check. The valid region is the convex hull of the corners that were used in the fit. MVP does not extrapolate past that hull.

## Rounding

The distance is rounded to the nearest 0.1 mm. Interval ends are rounded outward so the published interval contains the unrounded one. Annotation text, the JSON `label`, and the human summary use that same formatting. Residuals in `calibration` are diagnostic and are stored to 0.01 mm.
