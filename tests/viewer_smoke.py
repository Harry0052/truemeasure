#!/usr/bin/env python3
"""Exercise the local viewer over loopback: page, image, and the measure API."""

import json
import subprocess
import sys
import time
import urllib.request
from pathlib import Path


def main() -> None:
    cli, root = sys.argv[1], Path(sys.argv[2])
    request = root / "fixtures" / "requests" / "oblique_60mm.json"
    payload = json.loads(request.read_text(encoding="utf-8"))
    process = subprocess.Popen(
        [cli, "view", "--request", str(request), "--port", "0"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    try:
        url = ""
        deadline = time.time() + 10
        while time.time() < deadline:
            line = process.stdout.readline() if process.stdout else ""
            if line.startswith("http://"):
                url = line.strip()
                break
            if process.poll() is not None:
                raise SystemExit(process.stderr.read() if process.stderr else "viewer exited")
        if not url:
            raise SystemExit("viewer did not print a URL")
        page = urllib.request.urlopen(url, timeout=5).read().decode("utf-8")
        required = ["aria-live", "Point A", "Measure", "I attest", "Arrow"]
        missing = [item for item in required if item not in page]
        if missing:
            raise SystemExit(f"viewer page missing {missing}")
        image = urllib.request.urlopen(url + "sample.png", timeout=5).read()
        if not image.startswith(b"\x89PNG"):
            raise SystemExit("sample image is not a PNG")
        body = json.dumps(
            {"coplanar_attestation": True, "points_px": payload["points_px"]}
        ).encode("utf-8")
        measured = json.load(
            urllib.request.urlopen(
                urllib.request.Request(url + "measure", data=body, method="POST"), timeout=10
            )
        )
        if measured["status"] != "measured" or abs(measured["distance_mm"] - 60.0) > 1.0:
            raise SystemExit(f"unexpected viewer result {measured['status']} {measured.get('label')}")
        if not (measured["sensitivity_interval_mm"]["low"] <= 60.0 <= measured["sensitivity_interval_mm"]["high"]):
            raise SystemExit("viewer interval missed 60 mm")
        refused_body = json.dumps(
            {"coplanar_attestation": False, "points_px": payload["points_px"]}
        ).encode("utf-8")
        refused = json.load(
            urllib.request.urlopen(
                urllib.request.Request(url + "measure", data=refused_body, method="POST"), timeout=10
            )
        )
        if refused["status"] != "withheld" or refused["distance_mm"] is not None:
            raise SystemExit("viewer reported a dimension without coplanarity attestation")
        print(url, measured["label"])
    finally:
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()


if __name__ == "__main__":
    main()
