# Third-party notices

TrueMeasure does not vendor these libraries. The Ubuntu 24.04 packages below are the pinned builds.

| Component | Version observed | License | Role |
| --- | --- | --- | --- |
| OpenCV, including the contrib ArUco module | 4.6.0+dfsg-13.1ubuntu1 | Apache-2.0 | Marker rendering in the fixture generator, marker detection, homography, and image decode |
| nlohmann/json | 3.11.3-1 | MIT | Request, board, and result JSON |

OpenCV source and the Apache-2.0 terms: https://github.com/opencv/opencv

nlohmann/json source and the MIT terms: https://github.com/nlohmann/json

Marker bits in `fixtures/images/` are produced by OpenCV `drawMarker` for `DICT_4X4_50`. No separate marker artwork is copied into the repository. The generated fixture files are part of this MIT-licensed project. Their checksums are in `fixtures/manifest.json`.
