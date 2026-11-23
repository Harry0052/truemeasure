# Overlap review

Access date for every external source below: 2026-11-23.

Decision: **proceed** with a separate TrueMeasure repository. No examined tool already covers the acceptance case. The viable dependency path is OpenCV's Apache-2.0 ArUco detector as a primitive, with marker artwork generated locally rather than downloaded.

## Acceptance case held constant

- Input: one oblique synthetic photograph of a planar board with known fiducials and a 60.0 mm segment in that plane.
- Expected artifact: a distance in millimetres, a sensitivity interval that contains 60.0 mm, and no numerical distance when markers are missing or too poorly spread to support the measurement.
- Metric: absolute error at most 1.0 mm on the good view, and every known bad calibration example withholds the dimension.

## Capability matrix

| Tool | Planar distance from a photo | Perspective, not a single scale bar | Redundant fiducials and a valid region | Sensitivity interval | Withholds a bad calibration | Machine-readable provenance | License fit for an MIT product |
| --- | --- | --- | --- | --- | --- | --- | --- |
| OpenCV ArUco + `findHomography` | Primitive only | Yes, if the caller builds it | Caller must add this | No | Only when the algebra cannot form a homography | No | Apache-2.0, usable as a library |
| WebPlotDigitizer 4.7 | Scale-bar or chart axes, not a shop measurement | Map mode is a two-point scale | No | No | No | Manual export | AGPL-3.0 frontend; not incorporated |
| vpcodebuilder/object-measurement | Yes, one reference quadrilateral | Homography of that one quad | Warns on a steep angle, still returns a number | No | No | No | MIT, Android app, different product |
| amy-tabb/CASS | Indirect: user measures a warped image | Yes, warp to the ArUco plane | Pattern required for the warp | No | Not part of the documented contract | `results.txt` scaling factor | MIT, last pushed 2020, different product |
| TrueMeasure (proposed) | Yes, two operator points | Yes | Yes | Yes, from declared bounds | Yes | JSON result plus annotation | MIT, this repository |

## Probes

### OpenCV ArUco

- Primary documentation: [ArUco detection tutorial](https://docs.opencv.org/4.x/d5/dae/tutorial_aruco_detection.html)
- Source: [opencv/opencv](https://github.com/opencv/opencv), Apache-2.0. Repository metadata on the access date shows the default branch `5.x` (last push 2026-11-23).
- Version actually run: Ubuntu 24.04 package `libopencv` / `python3-opencv` **4.6.0+dfsg-13.1ubuntu1**. In this version ArUco is in the contrib module (`opencv2/aruco.hpp`). Dataset terms: none. Marker bits were drawn by `cv2.aruco.drawMarker` for dictionary `DICT_4X4_50`.
- Evidence: **observed run** of `docs/research/p01_opencv_probe.py`.

Observed result on the shared oblique fixture:

| Case | Numerical distance | Absolute error | Interval | Withheld |
| --- | --- | --- | --- | --- |
| All 6 markers | 60.0023 mm | 0.0023 mm | none reported | no |
| Two top-edge markers | 59.9751 mm | 0.0249 mm | none | no |
| One marker, segment outside it | 60.2327 mm | 0.2327 mm | none | no |
| Three collinear corners | no homography (`findHomography` raises, needs 4 points) | — | none | only because the fit is algebraically impossible |
| Top-edge corners only (collinear in the plane) | empty homography | — | none | only for a singular fit |

The good view meets the 1 mm error bar. The primitive does not meet the rest of the acceptance case: a two-marker or one-marker fit still emits a confident distance, and nothing describes click, scale, or residual uncertainty. Those checks have to live in the application.

A two-point scale bar on the same image, using marker 0's pixel width as 30 mm, returned **55.073 mm** (absolute error **4.927 mm**). That is the same calibration model as a map scale bar. It fails the 1 mm bar as soon as the photo is oblique.

### WebPlotDigitizer

- Primary documentation: [Digitize charts](https://automeris.io/docs/digitize/)
- Source: [automeris-io/WebPlotDigitizer](https://github.com/automeris-io/WebPlotDigitizer), **AGPL-3.0**, release **v4.7** (published 2024-02-13, commit `4c341ec`).
- Dataset terms: not applicable. The closed-source Automeris AI Assist service was not used.
- Evidence: **documentation**. An observed run was not attempted. The program is a GUI, it has no headless planar-metrology API, and the AGPL-3.0 license is not compatible with copying it into this MIT repository.

What the documentation supports: chart axes are calibrated from known axis values, and maps or microscope images are calibrated from the two ends of a scale bar. That scale mode maps pixels by one factor. It does not estimate a plane homography, does not report a sensitivity interval, and does not withhold a result when the reference is oblique or incomplete. The observed scale-bar analogue above is the quantitative comparison on the shared fixture.

### Closest discovered projects

Search queries used:

- `planar object measure from photo fiducial homography uncertainty abstention`
- `ArUco ruler measurement perspective valid region annotation reproducibility`
- `single-image planar metrology scale calibration uncertainty fiducial click error`

Closest credible product match: [vpcodebuilder/object-measurement](https://github.com/vpcodebuilder/object-measurement). GitHub metadata: MIT license, Java, default branch `master`, last push 2026-09-09, one star. No version tag was published in the repository object used here. Evidence: **documentation** (README). It finds one reference quadrilateral, builds a homography, and measures a box on that plane. It tells the operator to keep the object coplanar and warns when marker diagonals disagree by more than 25 percent. It still returns a measurement after that warning, does not emit a sensitivity interval, and does not write a traceable JSON record. It is an Android application, so the shared fixture was not run through it. Integrating TrueMeasure into that app would not produce the acceptance artifact without a new measurement policy.

Closest C++ ArUco workflow: [amy-tabb/CASS](https://github.com/amy-tabb/CASS). MIT license, last push 2020-03-09. Evidence: **documentation**. CASS calibrates from an ArUco pattern, undistorts, and warps the image into the pattern plane at a user-selected pixels-per-millimetre factor. The operator then measures pixels on the warped image. It does not accept two points and return a bounded distance, and it does not define an abstention contract. Cloning and building it was not a useful comparison: the missing artifact is specified by its own README.

Other discovery hits were not reuse candidates:

- [dauken85/Homogenous-WOBJ-calibation](https://github.com/dauken85/Homogenous-WOBJ-calibation) maps pixels through an ArUco homography and reports reprojection error. GitHub reports **no license**. Not copied or run.
- [hrlpavan/industrial-laser-measure](https://github.com/hrlpavan/industrial-laser-measure) documents a laser-line uncertainty formula. GitHub reports **no license**. It is not a two-point photo workflow. Not copied or run.
- RulerNet ([paper](https://arxiv.org/html/2507.07077v2)) estimates scale from a ruler in the wild. It needs model weights and does not produce a withheld, traceable shop measurement. Not run.

Background that informs the uncertainty design, not code we ship:

- Criminisi, Reid, Zisserman, *Single View Metrology* (1999), [PDF](https://www.robots.ox.ac.uk/~vgg/publications/1999/Criminisi99/criminisi99.pdf). First-order propagation of image-point uncertainty for planar measurements.
- Farid and Popescu, *Metric Measurements on a Plane from a Single Image*, [PDF](https://hfarid.org/downloads/publications/tr06.pdf).

## Features that already match

OpenCV already matches the parts that should not be reimplemented: dictionary decoding, marker corner localization, and a planar homography solver. WebPlotDigitizer already matches a manual two-point scale workflow for fronto-parallel scale bars. object-measurement already matches "one reference quadrilateral plus a homography" for a mobile camera. None of them match the combination this repository is for: redundant board landmarks, a declared valid region, a repeatable sensitivity interval, operator coplanarity attestation, and a hard withhold when those checks fail.

## Remaining uncertainties

- The probe image is noiseless and the warp is an exact homography, so a poorly spread marker set can still land within 1 mm. Abstention has to be based on geometry (spread, residual, valid region), not on the luck of a synthetic error.
- Real bench photographs are out of scope until an owned set exists. The optional bench path stays unwired.
- OpenCV 4.7 moved ArUco into the main tree and changed the detector API. The supported pin for this project is the Ubuntu 24.04 OpenCV 4.6 contrib package that was probed, not the upstream `5.x` default branch.
- WebPlotDigitizer was not executed. The scale-bar failure is from an observed analogue, not from that GUI.

## Decision

**Proceed.** Build TrueMeasure as a small C++20 application that calls OpenCV 4.6 ArUco and adds the calibration record those primitives do not keep. Do not vendor WebPlotDigitizer. Do not fork object-measurement or CASS; their workflows answer different questions and still would need the abstention policy written here. Do not copy the unlicensed repositories.

Fixture provenance path: generate every marker with OpenCV's dictionary renderer, keep the generator seed and checksums in the tree, and do not fetch images at run time.
