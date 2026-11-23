# Grokbot progress

## P01 — Research overlap and confirm the MVP

**Phase and status:** P01 PASS

**Files changed:**

- `docs/overlap.md`
- `docs/project-contract.md`
- `docs/research/p01_opencv_probe.py`
- `docs/grokbot-progress.md`

**Commands and results:**

- GitHub repository metadata and primary docs were read for OpenCV, WebPlotDigitizer 4.7 (`4c341ec`, AGPL-3.0), `vpcodebuilder/object-measurement` (MIT), and `amy-tabb/CASS` (MIT). Unlicensed hits were recorded and not copied.
- Installed Ubuntu 24.04 `python3-opencv` 4.6.0+dfsg-13.1ubuntu1 and ran the shared-fixture probe.
- Probe result: full-board homography distance 60.0023 mm (error 0.0023 mm) with no interval and no withhold; two top-edge markers still returned 59.9751 mm; a two-point scale bar returned 55.073 mm (error 4.927 mm).
- Re-run command: `python3 docs/research/p01_opencv_probe.py` (requires python3-opencv 4.6).

**Artifact paths:**

- `docs/overlap.md`
- `docs/project-contract.md`
- `docs/research/p01_opencv_probe.py`

**Open issues:**

- WebPlotDigitizer was compared from documentation plus an observed scale-bar analogue, not by driving its GUI.
- Real bench photos are not available and are not required for the synthetic acceptance case.

**Next phase:** P02

## P02 — Create the repository and data contracts

**Phase and status:** P02 PASS

**Files changed:**

- `CMakeLists.txt`, `.clang-format`, `.gitignore`
- `include/truemeasure/`, `src/`, `tests/`
- `tools/generate_fixtures.py`
- `fixtures/boards/`, `fixtures/images/`, `fixtures/requests/`, `fixtures/manifest.json`, `fixtures/oracle/reference.md`
- `docs/contracts.md`, `README.md`

**Commands and results:**

- `python3 tools/generate_fixtures.py` wrote the versioned board and views. The generator's own homography was within 0.03 mm of 60.0 mm on the success views.
- `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++ && cmake --build build` succeeded with OpenCV 4.6.0 and nlohmann/json 3.11.3.
- `./build/truemeasure check --request fixtures/requests/oblique_60mm.json` exited 0 with status `accepted` and decoded 1600x1200.
- `./build/truemeasure check --request fixtures/requests/nonfinite.json` exited 3 with category `nonfinite_point`.

**Artifact paths:**

- `fixtures/manifest.json`
- `docs/contracts.md`
- `fixtures/oracle/reference.md`

**Open issues:**

- None for the fixture contract. Measurement behavior is recorded in P03.

**Next phase:** P03

## P03 — Build the working core

**Phase and status:** P03 PASS

**Files changed:**

- `src/engine.cpp`, `tests/test_main.cpp`

**Commands and results:**

- `./build/truemeasure measure --request fixtures/requests/oblique_60mm.json --json-out out/oblique_60mm.json --annotate out/oblique_60mm.png`
- Observed status `measured`, distance 60.0 mm, interval 59.2 mm to 60.8 mm, label `60.0 mm [59.2, 60.8] mm`. Absolute error at the published rounding is 0.0 mm. The interval contains 60.0 mm.
- `./build/truemeasure measure --request fixtures/requests/top_row_60mm.json` exited 2 with category `markers_ill_spread` and `distance_mm` null.
- `ctest --test-dir build` passed the success views, the failure views, click and scale expansion, and JSON reproducibility.

**Artifact paths:**

- `out/oblique_60mm.json`
- `out/oblique_60mm.png`

**Open issues:**

- None in the core acceptance case.

**Next phase:** P04

## P04 — Ship the user workflow and reports

**Phase and status:** P04 PASS

**Files changed:**

- `src/cli.cpp`, `src/annotate.cpp`, `src/viewer.cpp`, `share/viewer.html`, `tests/viewer_smoke.py`

**Commands and results:**

- The demo command above writes JSON to stdout and to `--json-out`, a human summary to stderr, and an annotation whose label is `60.0 mm [59.2, 60.8] mm`.
- `truemeasure --help` exits 0 and documents the exit codes. Withheld input exits 2. Rejected input exits 3.
- `ctest` test `viewer` loaded the page, fetched the PNG, measured the fixture endpoints, and confirmed a missing attestation returns a null distance.
- Browser pass at http://127.0.0.1:8765/: a click and arrow keys update the live status, measuring without the attestation reports `coplanarity_not_attested`, and attested points near the segment report `60.3 mm [59.5, 61.0] mm`. A 390 px viewport keeps the image, controls, focus ring, and result in one column.

**Artifact paths:**

- `out/oblique_60mm.json`
- `out/oblique_60mm.png`
- `share/viewer.html`

**Open issues:**

- None in the local workflow.

**Next phase:** P05

## P05 — Verify correctness, security, and performance

**Phase and status:** P05 PASS

**Files changed:**

- `docs/validation.md`
- sanitizer coverage in `CMakeLists.txt` and the unit tests

**Commands and results:**

- `clang-format --dry-run -Werror` on the C++ sources passed.
- `ctest --test-dir build` passed `unit` and `viewer`.
- `cmake -S . -B build-san -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=g++ -DTRUEMEASURE_SANITIZERS=ON && cmake --build build-san && ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 ctest --test-dir build-san --output-on-failure` passed in 1.20 seconds.
- The oblique measurement took 0.061 seconds on a 52144 byte, 1600×1200 PNG. The enforced budget is 2.0 seconds.
- Every acceptance row in `docs/validation.md` was observed. Bad inputs publish no distance. Undeclared distortion is `calibration_residual`.

**Artifact paths:**

- `docs/validation.md`

**Open issues:**

- No bench photographs. The claim is limited to the synthetic set, and the report says so.

**Next phase:** P06

## P06 — Prepare a reproducible open-source release

**Phase and status:** P06 PASS

**Files changed:**

- `README.md`, `CONTRIBUTING.md`, `SECURITY.md`, `CHANGELOG.md`, `THIRD_PARTY.md`
- `.github/workflows/ci.yml`

**Commands and results:**

- A clean copy of the tree at `/tmp/tm-clean`, excluding build output, followed the README configure, build, and `ctest` commands. Both tests passed.
- The README demo in that copy wrote `out/oblique_60mm.json` with label `60.0 mm [59.2, 60.8] mm`, matching stdout.
- Tracked inputs are generated fixtures, source, and docs. No credentials or proprietary binaries were found.
- Hosted CI is the P07 check. The workflow uses Ubuntu 24.04, read-only contents plus artifact upload, and the pinned commits `actions/checkout@3d3c42e5aac5ba805825da76410c181273ba90b1` and `actions/upload-artifact@cf430e030ddbb5b0abf93d22962f4752f3646cd9`.

**Artifact paths:**

- `.github/workflows/ci.yml`
- `/tmp/tm-clean/out/oblique_60mm.json` from the clean-room run

**Open issues:**

- GitHub Actions has not run yet. P07 watches that result.

**Next phase:** P07
