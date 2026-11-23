# Validation

Recorded on Ubuntu 24.04.5 LTS, g++ 13.3.0, CMake 3.28.3, OpenCV 4.6.0 (`libopencv` 4.6.0+dfsg-13.1ubuntu1), nlohmann/json 3.11.3, clang-format 18.1.3, 4 cores. The fixture photograph for the demo is `fixtures/images/oblique_60mm.png`, 1600×1200, 52144 bytes.

Tolerances below apply to the synthetic fixture set generated in this repository. There is no external dataset and no bench measurement in this release.

## Acceptance metric

The reference length is 60.0 mm by construction (`fixtures/oracle/reference.md`). A measured result must have absolute error of at most 1.0 mm at the published 0.1 mm rounding, and its sensitivity interval must contain 60.0. Every listed bad input must withhold or reject and must leave `distance_mm` null.

Command, from a Release build:

```bash
./build/truemeasure measure --request fixtures/requests/<name>.json
```

| Fixture | Observed status | Observed distance | Observed interval | Exit |
| --- | --- | --- | --- | --- |
| oblique_60mm | measured | 60.0 mm | 59.2 to 60.8 | 0 |
| oblique_steep_60mm | measured | 60.0 mm | 59.0 to 61.0 | 0 |
| rotated_60mm | measured | 60.0 mm | 59.3 to 60.8 | 0 |
| lowres_60mm | measured | 60.0 mm | 58.8 to 61.1 | 0 |
| distorted_60mm | measured | 60.0 mm | 59.3 to 60.8 | 0 |
| top_row_60mm | withheld, markers_ill_spread | null | null | 2 |
| cropped_60mm | withheld, markers_ill_spread | null | null | 2 |
| two_markers_60mm | withheld, markers_insufficient | null | null | 2 |
| blank_60mm | withheld, markers_insufficient | null | null | 2 |
| blurred_60mm | withheld, markers_insufficient | null | null | 2 |
| outside_board | withheld, point_outside_valid_region | null | null | 2 |
| unattested | withheld, coplanarity_not_attested | null | null | 2 |
| distorted_undeclared | withheld, calibration_residual | null | null | 2 |
| nonfinite | rejected, nonfinite_point | null | null | 3 |
| missing_points | rejected, schema_invalid | null | null | 3 |
| malformed_image | rejected, image_malformed | null | null | 3 |

Five of five success views published 60.0 mm, so the published absolute error on this set is 0.0 mm. Five of five intervals contain 60.0. Eleven of eleven bad inputs published no distance. Raising `endpoint_click_tolerance_px` from 1 to 3 widened the oblique interval by more than 0.2 mm. Raising `scale_tolerance_mm` from 0.3 to 2.0 widened it by more than 3.0 mm. Those two comparisons are unit tests.

The generator's separate homography, before this program's rounding, was within 0.03 mm on the same success views. That check is in `tools/generate_fixtures.py` and is not the acceptance oracle.

## Demo repeat

```bash
./build/truemeasure measure \
  --request fixtures/requests/oblique_60mm.json \
  --json-out out/oblique_60mm.json \
  --annotate out/oblique_60mm.png
```

Observed label `60.0 mm [59.2, 60.8] mm`. The annotation draws that same string. A second run writes the same JSON and the same PNG bytes. Runtime on this host was 0.061 seconds. The unit test fails the demo measurement if it takes 2.0 seconds or longer. That budget is about thirty times the observed run, which leaves room for a cold cache without allowing a hung decode to pass.

`ctest --test-dir build` passed `unit` and `viewer`. The viewer smoke test loads the page, fetches the PNG, posts the fixture endpoints, and checks a measured result whose interval contains 60. It then posts the same points with coplanarity unset and checks that the distance is null.

A browser pass on http://127.0.0.1:8765/ confirmed keyboard nudging updates the status region, a measurement without the attestation is withheld with a readable reason, and attested points near the 60 mm segment report `60.3 mm [59.5, 61.0] mm` (the clicks were about two pixels off the construction endpoints). At a 390 pixel viewport the photograph, controls, focus ring, and result stack in one column.

## Domain safeguard

- `coplanar_attestation: false` on the good photograph returns `coplanarity_not_attested` and a null distance.
- Endpoints that map outside the fit hull return `point_outside_valid_region` and a null distance.
- Undeclared distortion on `distorted_60mm.png` returns `calibration_residual` and a null distance. An independent homography without undistortion was about 61.4 mm, which would have missed the 1 mm bar; the residual gate stops it.
- Images above 16 MiB are rejected as `image_oversized` before decode. The unit test builds that file at runtime and does not commit it.
- Published distances use 0.1 mm, which is coarser than the sub-pixel synthetic residual and matches the click-scale uncertainty the fixture declares.

## Trust boundaries

- The measurement path reads the request, the board, and one image. It does not spawn a process and it does not open a network connection.
- Writes occur only for `--json-out`, `--annotate`, and the viewer's HTTP responses. The viewer binds 127.0.0.1 and rejects a POST body above 64 KiB.
- JSON inputs are capped at 1 MiB. Images are capped before and after decode.
- Release and sanitizer builds compile with `-Wall -Wextra -Wpedantic -Werror`.
- AddressSanitizer and UndefinedBehaviorSanitizer (`-DTRUEMEASURE_SANITIZERS=ON`, `detect_leaks=1`) passed `ctest` in 1.20 seconds on this host.
- Dependencies are the Ubuntu packages in `THIRD_PARTY.md`. No binary blob, credential, or private key is in the tree. License pairing is MIT for this project and nlohmann/json, Apache-2.0 for OpenCV.

## Residual limitations

- The evidence is the synthetic set. A real camera, print scale, or non-flat part is outside the claim.
- The interval is a bound on the declared perturbations, not a coverage probability from a calibration laboratory.
- OpenCV 4.7 and later are not supported, because ArUco moved and the detector API changed.
- The viewer is a local page for placing points. It is not a packaged desktop application.
