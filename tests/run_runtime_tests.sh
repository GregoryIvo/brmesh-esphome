#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
build_dir=$(mktemp -d "${TMPDIR:-/tmp}/brmesh-runtime.XXXXXX")
trap 'rm -rf "$build_dir"' EXIT
c++ -std=c++17 -Wall -Wextra -Werror -Wno-unused-parameter -DUSE_ESP32 \
  -Itests/stubs -Icomponents/brmesh \
  tests/brmesh_runtime_test.cpp components/brmesh/brmesh.cpp components/brmesh/brmesh_protocol.cpp \
  -o "$build_dir/brmesh_runtime_test"
"$build_dir/brmesh_runtime_test"
