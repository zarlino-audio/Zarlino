#!/bin/bash
set -e

rm -rf build
mkdir -p build
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug
