#!/bin/sh
set -e
cd "$(dirname "$0")"
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
echo "Built: build/Polyhymnia"