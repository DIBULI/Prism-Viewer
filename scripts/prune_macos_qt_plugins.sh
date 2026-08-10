#!/usr/bin/env bash
set -euo pipefail

app_bundle="${1:-}"
qt_prefix="${2:-}"
if [[ -z "${app_bundle}" || ! -d "${app_bundle}" || -z "${qt_prefix}" ]]; then
  echo "Usage: $0 /path/to/Prism-Viewer.app /path/to/Qt" >&2
  exit 2
fi

sql_drivers="${app_bundle}/Contents/PlugIns/sqldrivers"
sqlite_driver="${sql_drivers}/libqsqlite.dylib"
sqlite_source=""
for qtpaths_name in qtpaths6 qtpaths; do
  qtpaths="${qt_prefix}/bin/${qtpaths_name}"
  if [[ -x "${qtpaths}" ]]; then
    queried_plugins="$("${qtpaths}" --query QT_INSTALL_PLUGINS 2>/dev/null || true)"
    if [[ -f "${queried_plugins}/sqldrivers/libqsqlite.dylib" ]]; then
      sqlite_source="${queried_plugins}/sqldrivers/libqsqlite.dylib"
      break
    fi
  fi
done
if [[ -z "${sqlite_source}" ]]; then
  for candidate in \
      "${qt_prefix}/plugins/sqldrivers/libqsqlite.dylib" \
      "${qt_prefix}/share/qt/plugins/sqldrivers/libqsqlite.dylib" \
      "${qt_prefix}/lib/qt6/plugins/sqldrivers/libqsqlite.dylib"; do
    if [[ -f "${candidate}" ]]; then
      sqlite_source="${candidate}"
      break
    fi
  done
fi
if [[ -z "${sqlite_source}" ]]; then
  echo "Qt SQLite driver was not found under ${qt_prefix}" >&2
  exit 1
fi

# Prism Viewer exports ROS bags through SQLite and does not use the optional
# ODBC, PostgreSQL, Mimer, MySQL, or InterBase drivers. Qt's deployment tool
# may copy all of them, including references to package-manager libraries that
# are unavailable on another Mac. Keep only the required SQLite plugin.
mkdir -p "${sql_drivers}"
[[ ! -e "${sqlite_driver}" ]] || chmod u+w "${sqlite_driver}"
cp -f "${sqlite_source}" "${sqlite_driver}"
while IFS= read -r -d '' plugin; do
  [[ "${plugin}" == "${sqlite_driver}" ]] && continue
  rm -f "${plugin}"
done < <(find "${sql_drivers}" -maxdepth 1 -type f -name '*.dylib' -print0)
