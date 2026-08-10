#!/usr/bin/env bash
set -euo pipefail

app_bundle="${1:-}"
if [[ -z "${app_bundle}" || ! -d "${app_bundle}" ]]; then
  echo "Usage: $0 /path/to/Prism-Viewer.app" >&2
  exit 2
fi

frameworks="${app_bundle}/Contents/Frameworks"
viewer="${app_bundle}/Contents/MacOS/Prism-Viewer"
license="${app_bundle}/Contents/Resources/licenses/libusb-COPYING.txt"
for required in \
    "${viewer}" \
    "${frameworks}/libprism_usb_sdk.dylib" \
    "${frameworks}/libusb-1.0.0.dylib"; do
  if [[ ! -f "${required}" ]]; then
    echo "Required macOS bundle file is missing: ${required}" >&2
    exit 1
  fi
  if [[ " $(lipo -archs "${required}") " != *" arm64 "* ]]; then
    echo "Required macOS bundle file does not contain arm64 code: ${required}" >&2
    exit 1
  fi
  while IFS= read -r minimum_version; do
    if ! awk -v version="${minimum_version}" \
        'BEGIN { exit(version + 0 <= 13.0 ? 0 : 1) }'; then
      echo "${required} requires macOS ${minimum_version}; maximum is 13.0" >&2
      exit 1
    fi
  done < <(vtool -show-build "${required}" |
             awk '$1 == "minos" { print $2 }')
done
if [[ ! -s "${license}" ]]; then
  echo "Bundled libusb license is missing: ${license}" >&2
  exit 1
fi
offscreen_plugin="${app_bundle}/Contents/PlugIns/platforms/libqoffscreen.dylib"
if [[ ! -f "${offscreen_plugin}" ]]; then
  echo "Bundled Qt offscreen plugin is missing: ${offscreen_plugin}" >&2
  exit 1
fi

invalid_dependencies=""
while IFS= read -r -d '' candidate; do
  if [[ "$(file -b "${candidate}")" != *Mach-O* ]]; then
    continue
  fi
  if [[ " $(lipo -archs "${candidate}") " != *" arm64 "* ]]; then
    invalid_dependencies+="${candidate}: no arm64 image"$'\n'
  fi
  while IFS= read -r dependency; do
    case "${dependency}" in
      @* | /System/Library/* | /usr/lib/*)
        ;;
      *)
        invalid_dependencies+="${candidate}: ${dependency}"$'\n'
        ;;
    esac
  done < <(otool -L "${candidate}" | tail -n +2 | awk '{ print $1 }')
  while IFS= read -r runtime_path; do
    case "${runtime_path}" in
      @* | /System/Library/* | /usr/lib/*)
        ;;
      *)
        invalid_dependencies+="${candidate}: LC_RPATH ${runtime_path}"$'\n'
        ;;
    esac
  done < <(otool -l "${candidate}" |
             awk '$1 == "cmd" && $2 == "LC_RPATH" {
                    getline; getline; print $2
                  }')
done < <(find "${app_bundle}" -type f -print0)

if [[ -n "${invalid_dependencies}" ]]; then
  echo "Bundle contains non-relocatable dependencies or rpaths:" >&2
  printf '%s' "${invalid_dependencies}" >&2
  exit 1
fi

if ! otool -l "${viewer}" |
    grep -A2 LC_RPATH |
    grep -F '@executable_path/../Frameworks' >/dev/null; then
  echo "Viewer executable is missing its bundled Frameworks rpath" >&2
  exit 1
fi

codesign --verify --deep --strict "${app_bundle}"
echo "Verified relocatable arm64 macOS app bundle: ${app_bundle}"
