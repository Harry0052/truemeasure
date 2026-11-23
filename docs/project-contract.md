# Project contract

Status: accepted for implementation after the P01 overlap review.

## User and task

A small-shop technician or maker has a flat part and a printed reference board in the same plane. They photograph both, confirm the points are coplanar, and ask for the distance between two points. TrueMeasure reports that distance in millimetres with a sensitivity interval, or it withholds the number and says why.

The product is a shop documentation aid. It is not a certified metrology instrument, not a depth estimator, and not a phone augmented-reality measure.

## Differentiation

Unqualified pixel-to-millimetre conversion hides calibration failure. TrueMeasure keeps the calibration evidence next to the number and refuses to invent a dimension when the board is missing, the markers are too weak, the points leave the calibrated region, or the operator does not attest coplanarity.

## Minimum dependencies

- C++20 compiler and CMake. Observed host compiler: g++ 13.3.0, CMake 3.28.3.
- OpenCV 4.6.x with the contrib ArUco module, Apache-2.0. Observed package: `libopencv-dev` and `libopencv-contrib-dev` 4.6.0+dfsg-13.1ubuntu1.
- nlohmann/json 3.11.3, MIT, for the request and result documents. Observed package: `nlohmann-json3-dev` 3.11.3-1.
- No paid service, no network at measurement time, no camera hardware for the default demo.

Supported platform for the working release: Ubuntu 24.04 with those packages. Other OpenCV majors are outside the pin.

## Measurable acceptance case

On the frozen oblique synthetic view of the versioned board, with the two supplied endpoints of a segment whose board length is 60.0 mm by construction:

1. The reported distance differs from 60.0 mm by at most 1.0 mm.
2. The sensitivity interval contains 60.0 mm.
3. The same command on a view with too few or too poorly spread markers reports calibration failure and does not include a numerical dimension.

The reference length is `sqrt((150 - 90)^2 + (85 - 85)^2) = 60` mm once the board coordinates below are fixed in P02. P01's probe used a different 60 mm placement, `(40, 70)` to `(100, 70)`, and the same numeric bar.

## Boundaries carried forward

One planar board, one declared camera or distortion setup, two points. No depth, no non-planar parts, no safety-critical tolerance, no uncontrolled phone AR. Bench photos are optional and stay out of the repository until their rights are recorded.
