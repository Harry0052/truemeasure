# Changelog

## 0.1.0

- Measure one planar distance from a `DICT_4X4_50` reference board, or withhold it.
- Publish a deterministic sensitivity interval from the declared click, marker-corner, and scale bounds.
- Require an explicit coplanarity attestation and stay inside the marker hull.
- Ship synthetic oblique, rotated, low-resolution, distorted, and failure fixtures with checksums.
- Provide a headless command, a loopback point-placement page, and an optional annotation.
- Run the fixture demo in GitHub Actions, including an AddressSanitizer and UndefinedBehaviorSanitizer job.
