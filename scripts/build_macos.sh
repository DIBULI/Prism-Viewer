#!/usr/bin/env bash
set -euo pipefail

sdk_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${sdk_root}/build-macos"
viewer_app="${build_dir}/Prism Viewer.app"
sdk_prefix="${1:-${PRISM_USB_SDK_PREFIX:-}}"

brew_bin="$(command -v brew || true)"
if [[ -z "${brew_bin}" && -x /opt/homebrew/bin/brew ]]; then
  brew_bin="/opt/homebrew/bin/brew"
fi
if [[ -z "${brew_bin}" ]]; then
  echo "Missing Homebrew. Install it from https://brew.sh first." >&2
  exit 2
fi
export PATH="$(dirname "${brew_bin}"):${PATH}"

for tool in cmake pkg-config; do
  if ! command -v "${tool}" >/dev/null 2>&1; then
    echo "Missing ${tool}. Install Homebrew, then run:" >&2
    echo "  brew install cmake pkgconf qtbase qtcharts libusb openssl@3" >&2
    exit 2
  fi
done

homebrew_prefix="$("${brew_bin}" --prefix)"
qtbase_prefix="$("${brew_bin}" --prefix qtbase)"
qtcharts_prefix="$("${brew_bin}" --prefix qtcharts)"
libusb_prefix="$("${brew_bin}" --prefix libusb)"
openssl_prefix="$("${brew_bin}" --prefix openssl@3)"
parallel_jobs="$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"

cmake_prefix_path="${qtbase_prefix};${qtcharts_prefix};${openssl_prefix}"
deploy_sdk_args=()
if [[ -n "${sdk_prefix}" ]]; then
  sdk_prefix="$(cd "${sdk_prefix}" && pwd)"
  if [[ ! -f "${sdk_prefix}/lib/cmake/PrismUsbSdk/PrismUsbSdkConfig.cmake" ]]; then
    echo "Prism Host SDK CMake package not found under ${sdk_prefix}" >&2
    exit 2
  fi
  cmake_prefix_path="${sdk_prefix};${cmake_prefix_path}"
  deploy_sdk_args=(-libpath="${sdk_prefix}/lib")
fi

export PKG_CONFIG_PATH="${libusb_prefix}/lib/pkgconfig:${openssl_prefix}/lib/pkgconfig${PKG_CONFIG_PATH:+:${PKG_CONFIG_PATH}}"

cmake -S "${sdk_root}" -B "${build_dir}" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="${cmake_prefix_path}" \
  -DOPENSSL_ROOT_DIR="${openssl_prefix}" \
  -DQT_ADDITIONAL_PACKAGES_PREFIX_PATH="${qtcharts_prefix}" \
  -DBUILD_TESTING=ON
cmake --build "${build_dir}" --parallel "${parallel_jobs}"
ctest --test-dir "${build_dir}" --output-on-failure

deploy_viewer_bundle() {
  "${qtbase_prefix}/bin/macdeployqt" "${viewer_app}" \
    -always-overwrite \
    "${deploy_sdk_args[@]}" \
    -libpath="${homebrew_prefix}/lib" 2>&1
}

deployment_output="$(deploy_viewer_bundle)"
if printf '%s\n' "${deployment_output}" | grep -q '^ERROR:'; then
  # Homebrew installs Qt modules in separate prefixes. The first pass may copy
  # a plugin before its framework has reached the bundle; a second scan then
  # resolves the dependency from Contents/Frameworks.
  first_deployment_output="${deployment_output}"
  deployment_output="$(deploy_viewer_bundle)"
  if printf '%s\n' "${deployment_output}" | grep -q '^ERROR:'; then
    printf '%s\n%s\n' "${first_deployment_output}" "${deployment_output}"
    echo "macdeployqt reported an unresolved dependency" >&2
    exit 3
  fi
fi
printf '%s\n' "${deployment_output}"

# Older development builds copied the SDK dylib beside the executable. The
# deploy step places the canonical runtime in Contents/Frameworks; remove only
# that obsolete generated duplicate before signing the bundle.
rm -f "${viewer_app}/Contents/MacOS/libprism_usb_sdk.dylib"
if [[ ! -f "${viewer_app}/Contents/Frameworks/libprism_usb_sdk.dylib" ]]; then
  echo "macdeployqt did not bundle libprism_usb_sdk.dylib" >&2
  exit 3
fi

codesign --force --deep --sign - "${viewer_app}"
codesign --verify --deep --strict "${viewer_app}"

echo "macOS Viewer is ready: ${viewer_app}"
echo "Launch it with: open \"${viewer_app}\""
