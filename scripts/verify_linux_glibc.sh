#!/usr/bin/env bash
set -euo pipefail

package="${1:-}"
maximum="${2:-2.31}"
if [[ -z "$package" || ! -d "$package" ]]; then
  echo "usage: $0 PACKAGE_ROOT [MAX_GLIBC_VERSION]" >&2
  exit 2
fi

highest="$(
  while IFS= read -r -d '' candidate; do
    if readelf -h "$candidate" >/dev/null 2>&1; then
      readelf --version-info "$candidate" 2>/dev/null \
        | grep -o 'GLIBC_[0-9][0-9.]*' || true
    fi
  done < <(find "$package" -type f -print0)
)"
highest="$(printf '%s\n' "$highest" | sed '/^$/d' | sort -Vu | tail -n 1)"
if [[ -z "$highest" ]]; then
  echo "No GLIBC symbol requirements found under $package" >&2
  exit 1
fi

maximum_symbol="GLIBC_$maximum"
newest="$(printf '%s\n%s\n' "$highest" "$maximum_symbol" \
  | sort -Vu | tail -n 1)"
if [[ "$newest" != "$maximum_symbol" ]]; then
  echo "Package requires $highest; maximum allowed is $maximum_symbol" >&2
  exit 1
fi

echo "Verified Linux package GLIBC baseline: $highest <= $maximum_symbol"
