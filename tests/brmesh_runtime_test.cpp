#include "brmesh.h"
#include <algorithm>
#include <iostream>
#include <string>
using namespace esphome;
using namespace esphome::brmesh;

class TestHub : public BrmeshHub {
 public:
  void initialize() {
    ble_ready_ = true; state_query_on_boot_ = false; state_poll_interval_ms_ = 0; all_group_refresh_on_boot_ = false;
    query_after_write_ = false; command_airtime_ms_ = 1500; group_refresh_interval_ms_ = 0;
  }
  void enqueue(uint8_t value, uint32_t airtime = 500, std::function<void()> callback = {}, uintptr_t owner = 0) {
    enqueue_advertisement_({value}, airtime, 3, 0, false, owner, callback);
  }
  void event(esp_gap_ble_cb_event_t event, int status = 0) {
    esp_ble_gap_cb_param_t p{};
    p.adv_data_raw_cmpl.status = p.adv_start_cmpl.status = p.adv_stop_cmpl.status = status;
    p.scan_param_cmpl.status = p.scan_start_cmpl.status = p.scan_stop_cmpl.status = status;
    gap_event_handler(event, &p);
  }
  // Runs one packet through configure/start/airtime/stop.
  void complete_one() {
    process_send_queue_(); event(ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT);
    event(ESP_GAP_BLE_ADV_START_COMPLETE_EVT); test_now += front_airtime(); process_send_queue_();
    event(ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT);
  }
  void drain() { for (unsigned n = 0; queued() && n < 200; n++) complete_one(); }
  bool scan_started() const { return scan_started_; }
  bool idle() const { return tx_state_ == TxState::IDLE; }
  bool advertising() const { return tx_state_ == TxState::ADVERTISING; }
  bool reset_pending() const { return ble_reset_pending_; }
  using BrmeshHub::start_scan_;
  using BrmeshHub::stop_scan_;
  using BrmeshHub::loop;
  size_t pending_queries() const { return pending_state_queries_.size(); }
  using BrmeshHub::process_state_timeouts_;
  size_t queued() const { return send_queue_.size(); }
  uint32_t front_airtime() const { return send_queue_.empty() ? 0 : send_queue_.front().airtime_ms; }
  bool front_cancelled() const { return !send_queue_.empty() && send_queue_.front().cancelled; }
  std::vector<uint8_t> payload(size_t index) const {
    const auto &raw = send_queue_.at(index).raw_advertisement;
    auto mfg = BrmeshProtocol::extract_brmesh_manufacturer(raw.data(), raw.size());
    return BrmeshProtocol::decode_fastcon_frame_for_logging(mfg, key_.data()).payload;
  }
  using BrmeshHub::process_send_queue_;
  using BrmeshHub::handle_manufacturer_data;
  void set_pairing(bool on) { pairing_mode_ = on; }
  size_t cached() const { return stored_.count; }
  uint16_t cached_address(size_t i) const { return stored_.devices[i].address; }
  std::vector<uint8_t> cached_key(size_t i) const { return {stored_.devices[i].key, stored_.devices[i].key + 4}; }
  std::vector<uint8_t> raw_payload_with_key(size_t index, const uint8_t *key) const {
    const auto &raw = send_queue_.at(index).raw_advertisement;
    auto mfg = BrmeshProtocol::extract_brmesh_manufacturer(raw.data(), raw.size());
    return BrmeshProtocol::decode_fastcon_frame_for_logging(mfg, key).payload;
  }
  uint8_t queued_type(size_t index) const {
    const auto &raw = send_queue_.at(index).raw_advertisement;
    auto mfg = BrmeshProtocol::extract_brmesh_manufacturer(raw.data(), raw.size());
    return BrmeshProtocol::decode_fastcon_frame_for_logging(mfg, key_.data()).command_type;
  }
};

static int failures = 0;
void check(bool condition, const char *name) { std::cout << (condition ? "PASS " : "FAIL ") << name << '\n'; failures += !condition; }
void reset() { test_now = 100; fake_ble::calls.clear(); fake_ble::packets.clear(); fake_ble::config_result = ESP_OK; esp32_ble::instance = {}; }

int main() {
  // --- continuous airtime ---------------------------------------------------------------------------------
  reset(); TestHub hub; hub.initialize(); hub.enqueue(1, 1500); hub.process_send_queue_();
  check(fake_ble::calls == std::vector<std::string>{"config"}, "wait for raw-data completion before starting advertising");
  hub.event(ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT); hub.event(ESP_GAP_BLE_ADV_START_COMPLETE_EVT);
  test_now += 1499; hub.process_send_queue_();
  check(fake_ble::calls.back() == "start" && hub.advertising(), "packet stays on air for its whole airtime");
  test_now += 1; hub.process_send_queue_();
  check(fake_ble::calls.back() == "stop" && hub.queued() == 1, "stop is requested once airtime elapsed");
  hub.event(ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT);
  check(hub.queued() == 0 && hub.idle(), "one config/start/stop cycle transmits the packet");
  check(fake_ble::packets.size() == 1, "no repeated stop/start cycles for one packet");

  reset(); TestHub airtime; airtime.initialize();
  airtime.send_temp_group_light_state({1, 2}, true, 1, 1, 1, 1, false, false, 0);
  check(airtime.queued() == 2, "group write contains membership followed by control");
  check(airtime.front_airtime() == 450, "membership window airs 3 x 150 ms like the app");
  airtime.complete_one();
  check(airtime.front_airtime() == 1500, "control packet uses the configured command airtime");

  // --- ordering ---------------------------------------------------------------------------------------------
  reset(); TestHub order; order.initialize(); unsigned completions = 0;
  order.enqueue(1, 300, [&]() { completions++; }); order.enqueue(0, 300);
  order.process_send_queue_(); order.event(ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT);
  order.event(ESP_GAP_BLE_ADV_START_COMPLETE_EVT);
  test_now += 299; order.process_send_queue_();
  check(fake_ble::calls.back() == "start" && completions == 0, "newer packet cannot interrupt an unrelated packet");
  test_now++; order.process_send_queue_(); order.event(ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT);
  check(completions == 1 && order.queued() == 1, "completion runs exactly once after the stop callback");
  order.drain();
  check(fake_ble::packets == std::vector<std::vector<uint8_t>>{{1}, {0}}, "FIFO order is preserved");

  // --- supersede --------------------------------------------------------------------------------------------
  reset(); TestHub super; super.initialize(); BrmeshLight bulb; light::LightState state;
  bulb.set_parent(&super); bulb.set_addresses({1, 2}); bulb.setup_state(&state);
  state.current_values.set_brightness(0.5f); bulb.write_state(&state); bulb.loop();
  check(super.queued() == 2, "light write dispatches immediately when the hub is idle");
  super.process_send_queue_(); super.event(ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT); super.event(ESP_GAP_BLE_ADV_START_COMPLETE_EVT);
  test_now += 300; state.current_values.set_brightness(0.9f); bulb.write_state(&state); bulb.loop();
  check(super.queued() == 3 && super.front_cancelled(), "newer write cancels queued packets and shortens the in-flight one");
  super.process_send_queue_();
  check(fake_ble::calls.back() == "stop", "cancelled in-flight packet is stopped at once");
  super.event(ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT);
  check(super.queued() == 2 && super.payload(0)[0] == 0x45, "fresh membership precedes the newer control packet");
  {
    auto command = BrmeshProtocol::build_group_light_command(true, 114, 255, 255, 255, true, false, 0, 0);
    check(super.payload(1) == BrmeshProtocol::build_group_control_payload(43050, 253, command), "latest brightness wins");
  }
  test_now += 10; state.current_values.set_brightness(0.2f); bulb.write_state(&state); bulb.loop();
  check(super.queued() == 2, "writes inside the dispatch rate limit are held, not queued");
  test_now += 250; bulb.loop();
  check(super.queued() == 2 && super.payload(1)[4] == 0x80 + 25, "held write replaces the queued command after the rate limit");

  // --- membership refresh interval -------------------------------------------------------------------------
  reset(); TestHub refresh; refresh.initialize();
  refresh.send_temp_group_light_state({1, 2}, true, 1, 1, 1, 1, false, false, 0);
  auto first_membership = refresh.payload(0); refresh.drain();
  refresh.send_temp_group_light_state({1, 2}, false, 1, 1, 1, 1, false, false, 0);
  check(refresh.queued() == 2 && refresh.payload(0)[0] == 0x45, "same group is refreshed again by default");
  check(first_membership != refresh.payload(0), "refresh token changes for every refresh");
  refresh.drain();
  refresh.send_temp_group_light_state({1, 110, 257}, true, 1, 1, 1, 1, false, false, 0);
  check(refresh.queued() == 4, "all address windows precede group control");
  check(refresh.payload(0)[0] == 0x45 && refresh.payload(1)[0] == 0x45 && refresh.payload(2)[0] == 0x45 && refresh.payload(3)[0] == 0x43, "group control follows every membership window");
  refresh.drain();
  refresh.set_group_refresh_interval(10000);
  refresh.send_temp_group_light_state({1, 2}, true, 1, 1, 1, 1, false, false, 0); refresh.drain();
  test_now += 2000; refresh.send_temp_group_light_state({1, 2}, false, 1, 1, 1, 1, false, false, 0);
  check(refresh.queued() == 1 && refresh.payload(0)[0] == 0x43, "recent membership for the same set is reused inside the interval");
  refresh.drain(); refresh.send_temp_group_light_state({3}, true, 1, 1, 1, 1, false, false, 0);
  check(refresh.queued() == 2 && refresh.payload(0)[0] == 0x45, "a different address set always refreshes membership");

  // --- failures, retry, BLE reset ---------------------------------------------------------------------------
  reset(); TestHub errors; errors.initialize(); unsigned done = 0; errors.enqueue(1, 300, [&]() { done++; });
  errors.process_send_queue_(); errors.event(ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT);
  errors.event(ESP_GAP_BLE_ADV_START_COMPLETE_EVT, 1);
  check(errors.queued() == 1 && done == 0, "asynchronous start failure does not count as a sent command");
  errors.event(ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT); test_now += 250; errors.complete_one();
  check(done == 1 && errors.queued() == 0, "failed start recovers and retries without resetting ESP32");

  reset(); TestHub timeout; timeout.initialize(); timeout.enqueue(1, 300); timeout.process_send_queue_();
  test_now += 2000; timeout.process_send_queue_();
  check(fake_ble::calls.back() == "stop" && timeout.queued() == 1, "missing GAP callback triggers recovery without dropping the packet");
  timeout.event(ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT); test_now += 250; timeout.complete_one();
  check(timeout.queued() == 0, "queue resumes after missing callback");

  reset(); TestHub repeated; repeated.initialize(); BrmeshLight retry_light; light::LightState retry_state;
  retry_light.set_parent(&repeated); retry_light.set_addresses({1}); retry_light.setup_state(&retry_state);
  retry_light.write_state(&retry_state); retry_light.loop();
  unsigned false_completion = 0; repeated.enqueue(0, 300, [&]() { false_completion++; });
  fake_ble::config_result = 1;
  for (unsigned i = 0; i < 3; i++) { repeated.process_send_queue_(); repeated.event(ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT); test_now += 250; }
  check(repeated.queued() == 0 && false_completion == 0, "repeated BLE failure drops dependent burst without claiming completion");
  check(retry_light.has_pending_write() && !repeated.reset_pending(), "light re-arms its last write after the first dropped burst");
  fake_ble::config_result = ESP_OK; test_now += 300; retry_light.loop();
  check(repeated.queued() == 2, "re-armed write is transmitted again");
  fake_ble::config_result = 1;
  for (unsigned i = 0; i < 3; i++) { repeated.process_send_queue_(); repeated.event(ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT); test_now += 250; }
  check(repeated.reset_pending() && esp32_ble::instance.disables == 1, "second dropped burst in a row restarts the BLE stack");
  check(!retry_light.has_pending_write(), "no third attempt is queued while BLE is down");
  fake_ble::config_result = ESP_OK; repeated.loop();
  check(esp32_ble::instance.enables == 0, "BLE re-enable waits for the reset delay");
  test_now += 1100; repeated.loop();
  check(esp32_ble::instance.enables == 1, "BLE stack is re-enabled after the delay");
  repeated.loop();
  check(!repeated.reset_pending() && retry_light.has_pending_write(), "BLE recovery grants one more attempt for the last write");
  retry_light.loop(); repeated.drain();
  check(repeated.queued() == 0, "the retry after a BLE reset transmits normally");

  // --- rollover ---------------------------------------------------------------------------------------------
  reset(); TestHub rollover; rollover.initialize(); test_now = UINT32_MAX - 70; rollover.enqueue(1, 100);
  rollover.process_send_queue_(); rollover.event(ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT); rollover.event(ESP_GAP_BLE_ADV_START_COMPLETE_EVT);
  test_now += 99; rollover.process_send_queue_();
  check(fake_ble::calls.back() == "start", "airtime deadline survives millis rollover");
  test_now++; rollover.process_send_queue_(); rollover.event(ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT);
  check(rollover.queued() == 0, "rollover transmission completes after full airtime");

  // --- scanning only when needed ---------------------------------------------------------------------------
  reset(); TestHub scanning; scanning.initialize(); scanning.loop();
  check(std::find(fake_ble::calls.begin(), fake_ble::calls.end(), "scan_params") == fake_ble::calls.end(), "no scan is started while nothing listens for bulb frames");
  scanning.set_pairing(true); scanning.loop();
  check(fake_ble::calls.back() == "scan_params", "pairing mode starts scanning and waits for parameter completion");
  scanning.event(ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT);
  check(fake_ble::calls.back() == "scan_start" && !scanning.scan_started(), "scan start request is not treated as confirmed scanning");
  scanning.event(ESP_GAP_BLE_SCAN_START_COMPLETE_EVT, 1); test_now += 2000; scanning.start_scan_();
  check(fake_ble::calls.back() == "scan_params", "failed scan start is retried");
  scanning.event(ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT); scanning.event(ESP_GAP_BLE_SCAN_START_COMPLETE_EVT);
  check(scanning.scan_started(), "successful scan callback confirms scanning");
  scanning.set_pairing(false); scanning.loop();
  check(fake_ble::calls.back() == "scan_stop", "scanning stops when pairing ends and no state feature is on");
  scanning.event(ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT);
  check(!scanning.scan_started(), "scan stop callback clears the scanning flag");
  test_now += 2000; scanning.set_listen(true); scanning.loop();
  check(fake_ble::calls.back() == "scan_params", "listen option keeps scanning on");

  // --- button actions ---------------------------------------------------------------------------------------
  reset(); TestHub button; button.initialize(); BrmeshLight group_output; light::LightState group_state;
  group_output.set_parent(&button); group_output.set_addresses({1, 2}); group_output.setup_state(&group_state);
  group_output.write_state(&group_state);
  button.send_temp_group_on_off({1, 2}, false); button.drain(); group_output.loop();
  check(button.queued() == 0 && group_state.remote_values.get_state() == 0, "all-off cancels older pending native light write");

  // --- state queries and reports ----------------------------------------------------------------------------
  reset(); TestHub query; query.initialize(); query.enqueue(1, 300); query.query_state(1);
  check(query.pending_queries() == 0, "state-query timeout does not begin while waiting in the transmit queue");
  query.complete_one();
  query.process_send_queue_(); query.event(ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT); query.event(ESP_GAP_BLE_ADV_START_COMPLETE_EVT);
  check(query.pending_queries() == 1, "state-query timeout begins when its advertisement starts");
  query.publish_report({1, true, 127, false, 0, 0, 0, false, 0, 0});
  test_now += 300; query.process_send_queue_(); query.event(ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT);
  check(query.pending_queries() == 0, "early state reply is not re-marked pending at completion");

  reset(); TestHub reports; reports.initialize(); reports.set_key_hex("41424344");
  BrmeshLight single; light::LightState single_state; single.set_parent(&reports); single.set_address(6); single.setup_state(&single_state);
  BrmeshLight grouped; light::LightState grouped_state; grouped.set_parent(&reports); grouped.set_addresses({5, 6, 7}); grouped.setup_state(&grouped_state);
  grouped_state.remote_values.set_state(1); grouped_state.current_values.set_state(1);
  const std::array<uint8_t, 4> key{{0x41, 0x42, 0x43, 0x44}};
  uint8_t upload_seq = 0x50;
  auto upload_with_seq = [&](uint8_t addr, uint8_t seq, std::vector<uint8_t> data) {
    auto body = BrmeshProtocol::package_fastcon_body(BrmeshProtocol::build_upload_body(addr, 0, data), 0, seq, 4, key.data(), true);
    auto adv = BrmeshProtocol::build_new_protocol_advertisement(body);
    reports.handle_manufacturer_data(BrmeshProtocol::extract_brmesh_manufacturer(adv.data(), adv.size()), -60);
  };
  auto upload = [&](uint8_t addr, std::vector<uint8_t> data) {
    auto body = BrmeshProtocol::package_fastcon_body(BrmeshProtocol::build_upload_body(addr, 0, data), 0, ++upload_seq, 4, key.data(), true);
    auto adv = BrmeshProtocol::build_new_protocol_advertisement(body);
    reports.handle_manufacturer_data(BrmeshProtocol::extract_brmesh_manufacturer(adv.data(), adv.size()), -60);
  };
  upload(6, {0x80 | 0x40, 0, 0, 0, 0, 0});
  check(single_state.published == 1 && single_state.remote_values.get_state() == 1 && single_state.remote_values.get_brightness() > 0.49f && single_state.remote_values.get_brightness() < 0.51f, "type 4 upload frame updates the matching single light");
  check(grouped_state.published == 1 && grouped_state.remote_values.get_state() == 1, "member report also reaches the grouped light");
  upload(6, {0x80 | 0x40, 0, 0, 0, 0, 0});
  check(single_state.published == 1, "repeated identical report does not publish again");
  upload(5, {0x00, 0, 0, 0, 0, 0}); upload(7, {0x00, 0, 0, 0, 0, 0});
  check(grouped_state.remote_values.get_state() == 1, "group stays on while one member reports on");
  upload(6, {0x00, 0, 0, 0, 0, 0});
  check(grouped_state.remote_values.get_state() == 0, "group turns off once every member reported off");
  check(single_state.remote_values.get_brightness() > 0.49f, "OFF report preserves brightness for next ON");
  upload_with_seq(6, 0x77, {0x80 | 0x10, 0, 0, 0, 0, 0});
  const unsigned before_same_seq = single_state.published;
  upload_with_seq(6, 0x77, {0x80 | 0x10, 0, 0, 0, 0, 0});
  check(single_state.published == before_same_seq, "identical repeat with same sequence is still ignored");
  upload_with_seq(6, 0x77, {0x80 | 0x20, 0, 0, 0, 0, 0});
  check(single_state.published == before_same_seq + 1 && single_state.remote_values.get_brightness() > 0.24f, "changed status with an unchanged sequence number is applied");

  // --- stale member reports must not outvote fresh ones ------------------------------------------------
  upload(5, {0x45, 0, 0, 0, 0xff, 0}); upload(6, {0x45, 0, 0, 0, 0xff, 0}); upload(7, {0x45, 0, 0, 0, 0xff, 0});
  check(grouped_state.remote_values.get_brightness() > 0.54f && grouped_state.remote_values.get_brightness() < 0.55f, "group shows the reported level");
  test_now += 1000; grouped_state.current_values.set_brightness(0.4f); grouped.write_state(&grouped_state); grouped.loop();
  const unsigned published_before = grouped_state.published;
  test_now += 1000; upload(5, {0x33, 0, 0, 0, 0xff, 0});
  check(grouped_state.remote_values.get_brightness() > 0.40f && grouped_state.remote_values.get_brightness() < 0.41f, "one fresh report after a write wins over stale higher reports");
  check(grouped_state.published == published_before + 1, "fresh report publishes once");
  upload(6, {0x33, 0, 0, 0, 0xff, 0}); upload(7, {0x33, 0, 0, 0, 0xff, 0});
  check(grouped_state.published == published_before + 1, "identical follow-up reports do not publish again");
  test_now += 1000; grouped_state.current_values.set_state(0); grouped.write_state(&grouped_state); grouped.loop();
  grouped_state.remote_values.set_state(0);
  const unsigned published_after_off = grouped_state.published;
  upload(6, {0x00, 0, 0, 0, 0xff, 0});
  check(grouped_state.remote_values.get_state() == 0 && grouped_state.published == published_after_off, "stale ON members do not flip the group back on after an OFF write");
  upload(7, {0x33, 0, 0, 0, 0xff, 0});
  check(grouped_state.remote_values.get_state() == 1, "a fresh ON report after an OFF write is honest feedback about a straggler");

  // --- bulb-side effects -----------------------------------------------------------------------------------
  reset(); TestHub modes; modes.initialize(); BrmeshLight mode_light; light::LightState mode_state;
  mode_light.set_parent(&modes); mode_light.set_addresses({1, 2}); mode_light.setup_state(&mode_state); mode_state.output = &mode_light;
  mode_light.write_state(&mode_state); mode_light.loop(); modes.drain();
  BrmeshModeEffect effect("Three color flash"); effect.init_internal(&mode_state);
  effect.set_scene({0xC2, 0x04, 0x01}); effect.set_jiffies(200);
  effect.start();
  check(modes.queued() == 2 && modes.payload(0)[0] == 0x45, "starting an effect refreshes membership first");
  check(modes.queued() == 2 && modes.payload(1) == BrmeshProtocol::build_group_scene_control_payload(253, 200, {0xC2, 0x04, 0x01}), "effect sends the group mode packet");
  modes.drain();
  test_now += 1000; mode_state.current_values.set_brightness(0.3f); mode_light.write_state(&mode_state); mode_light.loop();
  check(modes.queued() == 4 && modes.payload(1)[0] == 0x93 && modes.payload(3)[0] == 0x5C, "brightness change during an effect re-sends the mode after the normal command");
  modes.drain();
  effect.stop(); test_now += 1000; mode_light.loop();
  check(modes.queued() == 2 && modes.payload(1)[0] == 0x93, "stopping the effect sends the ordinary state back");
  modes.drain(); test_now += 1000; mode_state.current_values.set_brightness(0.6f); mode_light.write_state(&mode_state); mode_light.loop();
  check(modes.queued() == 2, "no mode packet follows a normal write once the effect is stopped");

  // --- pairing without the app --------------------------------------------------------------------------
  reset(); TestHub pair; pair.initialize(); pair.set_key_hex("41424344");
  BrmeshLight pool_light; light::LightState pool_state; pool_light.set_parent(&pair); pool_light.set_addresses({1, 2, 3}); pool_light.setup_state(&pool_state);
  std::vector<std::pair<uint16_t, std::string>> paired;
  pair.add_on_device_paired_callback([&](uint16_t a, std::string d) { paired.push_back({a, d}); });
  pair.start_pairing(); pair.loop();
  check(pair.queued() >= 1 && pair.queued_type(0) == 0, "pairing mode sends the discovery scan request");
  check(pair.raw_payload_with_key(0, BrmeshProtocol::DEFAULT_KEY.data()) == std::vector<uint8_t>(12, 0), "scan request body is twelve zero bytes under the default key");
  pair.drain();
  auto discovery_from = [&](const char *did_hex, const char *key_hex) {
    std::vector<uint8_t> body(16, 0);
    body[0] = 0x10; body[1] = 0x21; body[2] = 0xFF;
    auto did = BrmeshProtocol::parse_hex(did_hex); auto key = BrmeshProtocol::parse_hex(key_hex);
    for (size_t i = 0; i < 6; i++) body[4 + i] = did[i];
    body[10] = 0x2c; body[11] = 0xaa;
    for (size_t i = 0; i < 4; i++) body[12 + i] = key[i];
    for (size_t i = 0; i < 4; i++) body[i] ^= BrmeshProtocol::DEFAULT_KEY[i];
    std::vector<uint8_t> mfg{0xF0, 0xFF}; mfg.insert(mfg.end(), body.begin(), body.end());
    pair.handle_manufacturer_data(mfg, -70);
  };
  discovery_from("a1b2c3d4e5f6", "11223344");
  check(pair.cached() == 1 && pair.cached_address(0) == 1, "first new bulb gets the lowest free configured address");
  check(pair.queued() == 1 && pair.queued_type(0) == 2, "discovery response is queued right away");
  {
    const uint8_t bulb_key[4] = {0x11, 0x22, 0x33, 0x44};
    check(pair.raw_payload_with_key(0, bulb_key) == BrmeshProtocol::parse_hex("a1b2c3d4e5f6010141424344"), "discovery response is encrypted with the bulb's own key and carries the mesh key");
  }
  test_now += 100; discovery_from("a1b2c3d4e5f6", "11223344");
  check(pair.queued() == 1, "repeated discovery inside the response interval does not queue another response");
  test_now += 2500; discovery_from("a1b2c3d4e5f6", "11223344");
  check(pair.queued() == 2, "discovery repeated later re-sends the response");
  pair.drain();
  {
    auto hb = BrmeshProtocol::package_fastcon_body(BrmeshProtocol::parse_hex("040100"), 0, 0x55, 5, BrmeshProtocol::parse_hex("41424344").data(), true);
    auto adv = BrmeshProtocol::build_legacy_advertisement(hb);
    pair.handle_manufacturer_data(BrmeshProtocol::extract_brmesh_manufacturer(adv.data(), adv.size()), -70);
  }
  check(paired.size() == 1 && paired[0].first == 1 && paired[0].second == "a1b2c3d4e5f6", "heartbeat from the new address fires on_device_paired");
  test_now += 3000; discovery_from("a1b2c3d4e5f6", "11223344");
  check(pair.queued() == 0, "a confirmed bulb that keeps announcing is not answered again");
  discovery_from("0a0b0c0d0e0f", "55667788"); discovery_from("1a1b1c1d1e1f", "99aabbcc");
  check(pair.cached() == 3 && pair.cached_address(1) == 2 && pair.cached_address(2) == 3, "further bulbs take the next free addresses");
  pair.drain(); discovery_from("2a2b2c2d2e2f", "deadbeef");
  check(pair.cached() == 3 && pair.queued() == 0, "no response when every configured address is taken");
  pair.stop_pairing(); test_now += 5000; discovery_from("3a3b3c3d3e3f", "01020304");
  check(pair.cached() == 3, "discovery frames are ignored outside pairing mode");

  return failures ? 1 : 0;
}
