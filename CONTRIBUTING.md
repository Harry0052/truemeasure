# Contributing

The supported build is the one in the README: Ubuntu 24.04, g++ 13, CMake, Ninja, OpenCV 4.6.x with ArUco, and nlohmann/json 3.11.3.

Before sending a change:

```bash
clang-format --dry-run -Werror src/*.cpp include/truemeasure/*.hpp tests/test_main.cpp
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++
cmake --build build
ctest --test-dir build --output-on-failure
```

A sanitizer build is available with `-DTRUEMEASURE_SANITIZERS=ON`. Use a separate build directory.

Regenerate fixtures only with `python3 tools/generate_fixtures.py` on OpenCV 4.6, and commit the images together with `fixtures/manifest.json`. Do not fetch photographs into the tree. Do not add credentials, access tokens, or personal data. Bench photos need an explicit redistribution note before they can be added.

Keep measurement code independent of file paths and the viewer. New withhold reasons need a fixture, an oracle row, and a null distance.
