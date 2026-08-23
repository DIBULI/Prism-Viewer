#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -lt 3 ] || [ "$#" -gt 4 ]; then
  echo "usage: $0 PACKAGE_ROOT OUTPUT.AppImage APPIMAGETOOL [ARCH]" >&2
  exit 2
fi

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
package_root="$(realpath "$1")"
output="$(realpath -m "$2")"
appimagetool="$(realpath "$3")"
if [ "$#" -eq 4 ]; then
  appimage_arch="$4"
else
  case "$(uname -m)" in
    x86_64) appimage_arch=x86_64 ;;
    aarch64 | arm64) appimage_arch=aarch64 ;;
    *)
      echo "unsupported AppImage architecture: $(uname -m)" >&2
      exit 1
      ;;
  esac
fi

for required in \
  "$package_root/bin/prism-viewer" \
  "$package_root/lib/libprism_usb_sdk.so" \
  "$package_root/lib/libcrypto.so.3" \
  "$package_root/plugins/platforms/libqxcb.so" \
  "$package_root/plugins/platforms/libqoffscreen.so" \
  "$root/packaging/AppRun" \
  "$root/packaging/prism-viewer.desktop" \
  "$root/branding/prism-mark-256.png"; do
  if [ ! -f "$required" ]; then
    echo "required AppImage input is missing: $required" >&2
    exit 1
  fi
done
if [ ! -x "$appimagetool" ]; then
  echo "appimagetool is missing or not executable: $appimagetool" >&2
  exit 1
fi

temp_root="$(mktemp -d "${TMPDIR:-/tmp}/prism-viewer-appimage.XXXXXX")"
trap 'rm -rf "$temp_root"' EXIT
appdir="$temp_root/Prism-Viewer.AppDir"
mkdir -p "$appdir/usr" "$(dirname "$output")"
cp -a "$package_root"/. "$appdir/usr"/
install -m 0755 "$root/packaging/AppRun" "$appdir/AppRun"
install -m 0644 "$root/packaging/prism-viewer.desktop" \
  "$appdir/prism-viewer.desktop"
install -m 0644 "$root/branding/prism-mark-256.png" \
  "$appdir/prism-viewer.png"

ARCH="$appimage_arch" APPIMAGE_EXTRACT_AND_RUN=1 \
  "$appimagetool" "$appdir" "$output"
chmod 0755 "$output"
test -s "$output"
