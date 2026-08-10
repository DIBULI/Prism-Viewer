#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${root}/build-linux"

cmake_args=(
  -S "${root}"
  -B "${build_dir}"
  -DCMAKE_BUILD_TYPE=Release
  -DBUILD_TESTING=ON
)

# CMake caches the absolute source directory. Reuse this one build directory
# after a workspace move by clearing only generated configuration state.
rm -f "${build_dir}/CMakeCache.txt"
rm -rf "${build_dir}/CMakeFiles"

cmake "${cmake_args[@]}"
cmake --build "${build_dir}" --parallel "$(nproc)"
ctest --test-dir "${build_dir}" --output-on-failure
