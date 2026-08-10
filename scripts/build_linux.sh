#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${root}/build-linux"
sdk_prefix="${1:-${PRISM_USB_SDK_PREFIX:-}}"

cmake_args=(
  -S "${root}"
  -B "${build_dir}"
  -DCMAKE_BUILD_TYPE=Release
  -DBUILD_TESTING=ON
)
if [[ -n "${sdk_prefix}" ]]; then
  sdk_prefix="$(cd "${sdk_prefix}" && pwd)"
  if [[ ! -f "${sdk_prefix}/lib/cmake/PrismUsbSdk/PrismUsbSdkConfig.cmake" ]]; then
    echo "Prism Host SDK CMake package not found under ${sdk_prefix}" >&2
    exit 2
  fi
  cmake_args+=("-DPRISM_USB_SDK_ROOT=${sdk_prefix}")
fi

# CMake caches the absolute source directory. Reuse this one build directory
# after a workspace move by clearing only generated configuration state.
rm -f "${build_dir}/CMakeCache.txt"
rm -rf "${build_dir}/CMakeFiles"

cmake "${cmake_args[@]}"
cmake --build "${build_dir}" --parallel "$(nproc)"
ctest --test-dir "${build_dir}" --output-on-failure
