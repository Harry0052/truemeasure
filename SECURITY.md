# Security

TrueMeasure reads local images and JSON and writes only the paths given on the command line. The viewer binds to 127.0.0.1.

Report a vulnerability through GitHub private vulnerability reporting:

https://github.com/Harry0052/truemeasure/security/advisories/new

Please include the version or commit, the fixture or input class that triggers the issue, and whether a crafted image or JSON file is involved. Do not include personal data or live credentials in the report.

Image size limits, the JSON size limit, and the viewer request-body limit are the current resource bounds. They are described in `docs/contracts.md`.
