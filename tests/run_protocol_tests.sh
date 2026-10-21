#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."
mkdir -p /tmp/brmesh-tests
g++ -std=c++17 -Wall -Wextra -Werror \
  tests/brmesh_protocol_test.cpp \
  components/brmesh/brmesh_protocol.cpp \
  -o /tmp/brmesh-tests/brmesh_protocol_test
/tmp/brmesh-tests/brmesh_protocol_test
