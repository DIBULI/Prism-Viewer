#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
app_bundle="${1:-}"
runtime_dir="${2:-${root}/third_party/Prism-SDK/runtime/macos-arm64}"
if [[ -z "${app_bundle}" || ! -d "${app_bundle}" ]]; then
  echo "Usage: $0 /path/to/Prism-Viewer.app [SDK-runtime-directory]" >&2
  exit 2
fi

frameworks="${app_bundle}/Contents/Frameworks"
licenses="${app_bundle}/Contents/Resources/licenses"
mkdir -p "${frameworks}" "${licenses}"

# macdeployqt follows the SDK's @rpath dependency and can replace the bundled
# minOS-13 libusb with a newer package-manager copy. Restore the audited SDK
# runtime pair after Qt deployment so packaging is independent of the build
# host's installed libusb.
for library in libprism_usb_sdk.dylib libusb-1.0.0.dylib; do
  source_file="${runtime_dir}/${library}"
  destination="${frameworks}/${library}"
  if [[ ! -f "${source_file}" ]]; then
    echo "Bundled SDK runtime is missing: ${source_file}" >&2
    exit 1
  fi
  [[ ! -e "${destination}" ]] || chmod u+w "${destination}"
  cp -f "${source_file}" "${destination}"
done

license_source="${runtime_dir}/libusb-COPYING.txt"
if [[ ! -s "${license_source}" ]]; then
  echo "Bundled libusb license is missing: ${license_source}" >&2
  exit 1
fi
[[ ! -e "${licenses}/libusb-COPYING.txt" ]] || \
  chmod u+w "${licenses}/libusb-COPYING.txt"
cp -f "${license_source}" "${licenses}/libusb-COPYING.txt"
