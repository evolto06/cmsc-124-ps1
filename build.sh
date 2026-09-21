#!/usr/bin/env bash
# build.sh: Create one clean release build.
set -e
cmake -E remove_directory build
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
