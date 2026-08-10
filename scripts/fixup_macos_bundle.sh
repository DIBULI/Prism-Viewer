#!/usr/bin/env bash
set -euo pipefail

app_bundle="${1:-}"
if [[ -z "${app_bundle}" || ! -d "${app_bundle}" ]]; then
  echo "Usage: $0 /path/to/Prism-Viewer.app" >&2
  exit 2
fi

# macdeployqt rewrites load paths, but Homebrew Qt frameworks can retain their
# build-time LC_RPATH entries. They are unnecessary once every dependency is in
# Contents/Frameworks and make an otherwise self-contained bundle depend on the
# build machine's directory layout.
while IFS= read -r -d '' candidate; do
  if [[ "$(file -b "${candidate}")" != *Mach-O* ]]; then
    continue
  fi

  install_id="$(otool -D "${candidate}" 2>/dev/null |
                  tail -n +2 | head -n 1 || true)"
  case "${install_id}" in
    "" | @* | /System/Library/* | /usr/lib/*)
      ;;
    *)
      framework_relative="${candidate#${app_bundle}/Contents/Frameworks/}"
      if [[ "${framework_relative}" != "${candidate}" &&
            "${framework_relative}" == *.framework/Versions/*/* ]]; then
        relocated_id="@rpath/${framework_relative}"
      else
        relocated_id="@rpath/$(basename "${candidate}")"
      fi
      install_name_tool -id "${relocated_id}" "${candidate}"
      ;;
  esac

  while IFS= read -r runtime_path; do
    [[ -z "${runtime_path}" ]] && continue
    case "${runtime_path}" in
      @* | /System/Library/* | /usr/lib/*)
        ;;
      *)
        install_name_tool -delete_rpath "${runtime_path}" "${candidate}"
        ;;
    esac
  done < <(otool -l "${candidate}" |
             awk '$1 == "cmd" && $2 == "LC_RPATH" {
                    getline; getline; print $2
                  }')
done < <(find "${app_bundle}" -type f -print0)
