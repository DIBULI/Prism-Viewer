#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
image="prism-linux-host-build:ubuntu22"
sdk_prefix="${1:-${PRISM_USB_SDK_PREFIX:-}}"

if [[ -z "${sdk_prefix}" ]]; then
  echo "Usage: $0 /path/to/installed/prism-host-sdk" >&2
  echo "The SDK prefix must contain lib/cmake/PrismUsbSdk." >&2
  exit 2
fi
sdk_prefix="$(cd "${sdk_prefix}" && pwd)"
if [[ ! -f "${sdk_prefix}/lib/cmake/PrismUsbSdk/PrismUsbSdkConfig.cmake" ]]; then
  echo "Prism Host SDK CMake package not found under ${sdk_prefix}" >&2
  exit 2
fi

docker build -t "${image}" \
  -f "${root}/docker/linux-host-build.Dockerfile" "${root}"
docker run --rm \
  --user "$(id -u):$(id -g)" \
  --volume "${root}:/src" \
  --volume "${sdk_prefix}:/opt/prism-sdk:ro" \
  --env PRISM_USB_SDK_PREFIX=/opt/prism-sdk \
  "${image}"
