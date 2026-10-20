#!/usr/bin/env bash
# Full build: toolchain + sources, dependencies, OCP, zip.
set -e
d=$(dirname "$0")
"$d/fetch-sources.sh"
"$d/build-deps.sh"
"$d/build-ocp.sh"
"$d/package.sh" "$@"
