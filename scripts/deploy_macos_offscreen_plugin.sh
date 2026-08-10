#!/usr/bin/env bash
set -euo pipefail

app_bundle="${1:-}"
qt_prefix="${2:-}"
if [[ -z "${app_bundle}" || ! -d "${app_bundle}" || -z "${qt_prefix}" ]]; then
  echo "Usage: $0 /path/to/Prism-Viewer.app /path/to/Qt" >&2
  exit 2
fi

plugin_source=""
for qtpaths_name in qtpaths6 qtpaths; do
  qtpaths="${qt_prefix}/bin/${qtpaths_name}"
  if [[ -x "${qtpaths}" ]]; then
    queried_plugins="$("${qtpaths}" --query QT_INSTALL_PLUGINS 2>/dev/null || true)"
    if [[ -f "${queried_plugins}/platforms/libqoffscreen.dylib" ]]; then
      plugin_source="${queried_plugins}/platforms/libqoffscreen.dylib"
      break
    fi
  fi
done
if [[ -z "${plugin_source}" ]]; then
  for candidate in \
      "${qt_prefix}/plugins/platforms/libqoffscreen.dylib" \
      "${qt_prefix}/share/qt/plugins/platforms/libqoffscreen.dylib" \
      "${qt_prefix}/lib/qt6/plugins/platforms/libqoffscreen.dylib"; do
    if [[ -f "${candidate}" ]]; then
      plugin_source="${candidate}"
      break
    fi
  done
fi
if [[ -z "${plugin_source}" ]]; then
  echo "Qt offscreen platform plugin was not found under ${qt_prefix}" >&2
  exit 1
fi

plugin_destination="${app_bundle}/Contents/PlugIns/platforms"
mkdir -p "${plugin_destination}"
[[ ! -e "${plugin_destination}/libqoffscreen.dylib" ]] || \
  chmod u+w "${plugin_destination}/libqoffscreen.dylib"
cp -f "${plugin_source}" "${plugin_destination}/libqoffscreen.dylib"
