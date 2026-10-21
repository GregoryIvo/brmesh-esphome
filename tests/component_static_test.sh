#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

failures=0

expect_file_contains() {
  local name="$1"
  local file="$2"
  local pattern="$3"

  if grep -q -F -- "$pattern" "$file"; then
    printf 'PASS %s\n' "$name"
  else
    printf 'FAIL %s: expected %s in %s\n' "$name" "$pattern" "$file"
    failures=$((failures + 1))
  fi
}

expect_file_contains "raw automation accepts native 18 byte payloads" \
  "components/brmesh/__init__.py" \
  "if len(value) > 36:"

expect_file_contains "query after write defaults off until bulb replies are verified" \
  "components/brmesh/__init__.py" \
  "cv.Optional(CONF_QUERY_AFTER_WRITE, default=False): cv.boolean"

expect_file_contains "command airtime option exists" \
  "components/brmesh/__init__.py" \
  "cv.Optional(CONF_COMMAND_AIRTIME): cv.positive_time_period_milliseconds"

expect_file_contains "default airtime is 5 x 300 ms" \
  "components/brmesh/__init__.py" \
  'cv.Optional(CONF_COMMAND_INTERVAL, default="300ms"): cv.positive_time_period_milliseconds'

expect_file_contains "BLE recovery is on by default" \
  "components/brmesh/__init__.py" \
  "cv.Optional(CONF_RECOVER_BLE, default=True): cv.boolean"

expect_file_contains "query redundancy defaults to one packet" \
  "components/brmesh/__init__.py" \
  "cv.Optional(CONF_QUERY_REDUNDANCY, default=1): cv.int_range(min=1, max=20)"

expect_file_contains "state query on boot defaults off" \
  "components/brmesh/__init__.py" \
  "cv.Optional(CONF_STATE_QUERY_ON_BOOT, default=False): cv.boolean"

expect_file_contains "state poll interval defaults off" \
  "components/brmesh/__init__.py" \
  "cv.Optional(CONF_STATE_POLL_INTERVAL, default=\"0s\"): cv.positive_time_period_milliseconds"

expect_file_contains "state timeout defaults to ten seconds" \
  "components/brmesh/__init__.py" \
  "cv.Optional(CONF_STATE_TIMEOUT, default=\"10s\"): cv.positive_time_period_milliseconds"

expect_file_contains "all group refresh on boot defaults off" \
  "components/brmesh/__init__.py" \
  "cv.Optional(CONF_ALL_GROUP_REFRESH_ON_BOOT, default=False): cv.boolean"

expect_file_contains "temporary group action is exposed" \
  "components/brmesh/__init__.py" \
  '"brmesh.temp_group_on_off", BrmeshTempGroupOnOffAction, BRMESH_TEMP_GROUP_ON_OFF_SCHEMA, synchronous=True'

expect_file_contains "grouped lights register with the hub" \
  "components/brmesh/brmesh.cpp" \
  "this->parent_->register_group_light(this->addresses_, this);"

expect_file_contains "temporary group button actions update matching grouped light state" \
  "components/brmesh/brmesh.cpp" \
  "this->publish_group_on_off_(addresses, state);"

expect_file_contains "grouped state bridge compares exact sorted addresses" \
  "components/brmesh/brmesh.cpp" \
  "registered_light.addresses == sorted"

expect_file_contains "individual write queries after command completion" \
  "components/brmesh/brmesh.cpp" \
  "after_write = [this, address]() { this->query_state(address); }"

expect_file_contains "decoded reports publish state directly" \
  "components/brmesh/brmesh.cpp" \
  "this->state_->publish_state();"

expect_file_contains "upload frames are routed through the native layout decoder" \
  "components/brmesh/brmesh.cpp" \
  "BrmeshProtocol::decode_state_from_frame(frame)"

expect_file_contains "scanning is conditional" \
  "components/brmesh/brmesh.cpp" \
  "if (this->scan_wanted_())"

expect_file_contains "state timeout is processed in hub loop" \
  "components/brmesh/brmesh.cpp" \
  "this->process_state_timeouts_();"

expect_file_contains "queries mark pending state after advertising starts" \
  "components/brmesh/brmesh.cpp" \
  "this->mark_state_query_pending_(query_addresses);"

expect_file_contains "lights publish only when the aggregate changes" \
  "components/brmesh/brmesh.cpp" \
  "if (!this->values_differ_(values, this->state_->remote_values))"

if sed -n '/void BrmeshLight::apply_report/,/^}/p' "components/brmesh/brmesh.cpp" | grep -n "perform("; then
  printf 'FAIL decoded report application should not perform a LightCall\n'
  failures=$((failures + 1))
else
  printf 'PASS decoded report application avoids LightCall feedback writes\n'
fi

if grep -n "delay(" "components/brmesh/brmesh.cpp"; then
  printf 'FAIL command paths should not call delay() directly\n'
  failures=$((failures + 1))
else
  printf 'PASS command paths do not call delay() directly\n'
fi

if ((failures > 0)); then
  exit 1
fi
