#!/bin/sh
# Arguments are separate argv entries; never evaluated as shell code.
set -eu
parent=$1 target=$2 staged=$3 backup=$4 relative=$5 work=$6
exec >>"$work/install.log" 2>&1
cd "$work"
failed_package=$(dirname "$staged")/failed-package
test -d "$target" && test ! -L "$target" && test -d "$staged"
test ! -e "$backup" && test -x "$staged/$relative"
printf ready >"$work/ready"
count=0
while kill -0 "$parent" 2>/dev/null; do
  test ! -e "$work/cancelled" || exit 10
  count=$((count+1)); test "$count" -le 60 || exit 10
  sleep 1
done
test ! -e "$work/cancelled" || exit 10
if ! mv "$target" "$backup"; then "$target/$relative" & exit 11; fi
if ! mv "$staged" "$target"; then mv "$backup" "$target"; "$target/$relative" & exit 11; fi
"$target/$relative" --viewer-update-ack "$work/started" &
child=$!
child_identity=$(ps -p "$child" -o lstart= 2>/dev/null || true)
count=0
while test ! -f "$work/started"; do
  if ! kill -0 "$child" 2>/dev/null || test "$count" -ge 30; then
    # child belongs to this helper; wait reaps it before rollback.
    current_identity=$(ps -p "$child" -o lstart= 2>/dev/null || true)
    if test -n "$child_identity" && test "$current_identity" = "$child_identity"; then
      kill -KILL "$child" 2>/dev/null || true
    fi
    wait "$child" 2>/dev/null || true
    mv "$target" "$failed_package"
    mv "$backup" "$target"
    "$target/$relative" &
    printf rollback >"$work/result"
    exit 12
  fi
  count=$((count+1));sleep 1
done
printf installed >"$work/result"
# Retain the old package as a recovery copy; never remove user files.
