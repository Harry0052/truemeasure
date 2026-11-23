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
