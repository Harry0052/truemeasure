# Oracle

The reference length does not come from the program under test. On board `tm-board-v1` the segment endpoints are marked in board coordinates before any camera warp:

- A = (90.0 mm, 85.0 mm)
- B = (150.0 mm, 85.0 mm)
- dx = 60.0 mm, dy = 0.0 mm
- distance = sqrt(60.0² + 0.0²) = 60.0 mm

Every success fixture is a projective view of that same segment. The acceptance bar for a measured result is an absolute error of at most 1.0 mm and a sensitivity interval that contains 60.0 mm. A bad calibration must not publish a number.

| Request | Expected status | Expected category | Distance |
| --- | --- | --- | --- |
| oblique_60mm | measured | none | 60.0 mm within 1.0 mm, interval contains 60.0 |
| oblique_steep_60mm | measured | none | same bar |
| rotated_60mm | measured | none | same bar |
| lowres_60mm | measured | none | same bar |
| distorted_60mm | measured | none | same bar, using the declared pinhole model |
| top_row_60mm | withheld | markers_ill_spread | null |
| cropped_60mm | withheld | markers_ill_spread | null |
| two_markers_60mm | withheld | markers_insufficient | null |
| blank_60mm | withheld | markers_insufficient | null |
| blurred_60mm | withheld | markers_insufficient | null |
| outside_board | withheld | point_outside_valid_region | null |
| unattested | withheld | coplanarity_not_attested | null |
| distorted_undeclared | withheld | calibration_residual | null |
| nonfinite | rejected | nonfinite_point | null |
| missing_points | rejected | schema_invalid | null |
| malformed_image | rejected | image_malformed | null |

Observed command output for this OpenCV 4.6.0 tree is recorded in `docs/validation.md`.
