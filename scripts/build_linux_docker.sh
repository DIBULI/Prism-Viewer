#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
image="prism-linux-host-build:ubuntu22"

docker build -t "${image}" \
  -f "${root}/docker/linux-host-build.Dockerfile" "${root}"
docker run --rm \
  --user "$(id -u):$(id -g)" \
  --volume "${root}:/src" \
  "${image}"
