#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${root}/build-macos"
stage_dir="${build_dir}/stage"
app_bundle="${stage_dir}/Prism-Viewer.app"

brew_bin=""
if command -v brew >/dev/null 2>&1; then
  brew_bin="$(command -v brew)"
elif [[ -x /opt/homebrew/bin/brew ]]; then
  brew_bin=/opt/homebrew/bin/brew
elif [[ -x /usr/local/bin/brew ]]; then
  brew_bin=/usr/local/bin/brew
fi

cmake_bin="${CMAKE_COMMAND:-}"
if [[ -z "${cmake_bin}" ]] && command -v cmake >/dev/null 2>&1; then
  cmake_bin="$(command -v cmake)"
fi
if [[ -z "${cmake_bin}" && -n "${brew_bin}" ]]; then
  cmake_prefix="$("${brew_bin}" --prefix cmake 2>/dev/null || true)"
  if [[ -x "${cmake_prefix}/bin/cmake" ]]; then
    cmake_bin="${cmake_prefix}/bin/cmake"
  fi
fi
if [[ -z "${cmake_bin}" || ! -x "${cmake_bin}" ]]; then
  echo "CMake was not found. Install it with: brew install cmake" >&2
  exit 1
fi
ctest_bin="$(dirname "${cmake_bin}")/ctest"
if [[ ! -x "${ctest_bin}" ]]; then
  echo "ctest was not found beside ${cmake_bin}" >&2
  exit 1
fi

qt_prefix="${QT_ROOT:-}"
if [[ -z "${qt_prefix}" ]]; then
  qtpaths_bin=""
  for candidate in qtpaths6 qtpaths; do
    if command -v "${candidate}" >/dev/null 2>&1; then
      qtpaths_bin="$(command -v "${candidate}")"
      break
    fi
  done
  if [[ -z "${qtpaths_bin}" && -n "${brew_bin}" ]]; then
    for formula in qt qtbase; do
      formula_prefix="$("${brew_bin}" --prefix "${formula}" 2>/dev/null || true)"
      for candidate in qtpaths6 qtpaths; do
        if [[ -x "${formula_prefix}/bin/${candidate}" ]]; then
          qtpaths_bin="${formula_prefix}/bin/${candidate}"
          break 2
        fi
      done
    done
  fi
  if [[ -z "${qtpaths_bin}" ]]; then
    echo "Qt was not found. Install it with: brew install qtbase qtcharts qtdeclarative qtsvg" >&2
    exit 1
  fi
  qt_prefix="$("${qtpaths_bin}" --query QT_INSTALL_PREFIX)"
fi

macdeployqt="${qt_prefix}/bin/macdeployqt"
if [[ ! -x "${macdeployqt}" && -n "${brew_bin}" ]]; then
  qtbase_prefix="$("${brew_bin}" --prefix qtbase 2>/dev/null || true)"
  macdeployqt="${qtbase_prefix}/bin/macdeployqt"
fi
if [[ ! -x "${macdeployqt}" ]]; then
  echo "macdeployqt was not found under ${qt_prefix}" >&2
  exit 1
fi

cmake_args=(
  -S "${root}"
  -B "${build_dir}"
  -DCMAKE_BUILD_TYPE=Release
  -DCMAKE_OSX_ARCHITECTURES=arm64
  -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0
  -DCMAKE_PREFIX_PATH="${qt_prefix}"
  -DBUILD_TESTING=ON
)

# CMake caches the absolute source directory. Reuse this one build directory
# after a workspace move by clearing only generated configuration state.
rm -f "${build_dir}/CMakeCache.txt"
rm -rf "${build_dir}/CMakeFiles"

"${cmake_bin}" "${cmake_args[@]}"
"${cmake_bin}" --build "${build_dir}" --parallel
"${ctest_bin}" --test-dir "${build_dir}" --output-on-failure

"${cmake_bin}" -E remove_directory "${stage_dir}"
"${cmake_bin}" --install "${build_dir}" --prefix "${stage_dir}"
"${macdeployqt}" "${app_bundle}" -always-overwrite
"${root}/scripts/prune_macos_qt_plugins.sh" \
  "${app_bundle}" "${qt_prefix}"
"${root}/scripts/deploy_macos_offscreen_plugin.sh" \
  "${app_bundle}" "${qt_prefix}"
"${root}/scripts/restore_macos_sdk_runtime.sh" "${app_bundle}"
"${root}/scripts/fixup_macos_bundle.sh" "${app_bundle}"
codesign --force --deep --sign - "${app_bundle}"
"${root}/scripts/verify_macos_bundle.sh" "${app_bundle}"
QT_QPA_PLATFORM=offscreen \
  "${app_bundle}/Contents/MacOS/Prism-Viewer" \
  --imu-only-recorder-self-test "${build_dir}/package-smoke-imu-only"

echo "Viewer app is ready at ${app_bundle}"
