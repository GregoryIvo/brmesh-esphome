#include "brmesh.h"
#include "brmesh_protocol.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <inttypes.h>
#include <utility>

#ifdef USE_ESP32
#include <esp_bt.h>
#include <esp_gap_ble_api.h>
#endif

namespace esphome {
namespace brmesh {

static const char *const TAG = "brmesh";
static const char *const COMPONENT_VERSION = "2026-09-16 continuous-airtime";

void BrmeshHub::setup() {
#ifdef USE_ESP32
  this->pref_ = global_preferences->make_preference<StoredDevices>(fnv1_hash("brmesh_devices"), true);
  this->load_pairings_();
  // BLEFastconHelper seeds sSendSeq from the wall clock so a restarted sender never replays the
  // sequence numbers the bulbs saw last. Do the same for the temporary-group token.
  const uint32_t seed = random_uint32();
  this->send_sequence_ = static_cast<uint8_t>(seed & 0xFF);
  if (this->send_sequence_ == 0)
    this->send_sequence_ = 1;
  this->group_token_ = static_cast<uint16_t>((seed >> 8) & 0xFFFF);
  this->ble_ready_ = true;
#else
  this->mark_failed();
#endif
}

bool BrmeshHub::is_ready() const {
#ifdef USE_ESP32
  return this->ble_ready_ && !this->ble_reset_pending_ && esp32_ble::global_ble != nullptr &&
         esp32_ble::global_ble->is_active();
#else
  return false;
#endif
}

uint32_t BrmeshHub::get_command_airtime_ms() const {
  uint32_t airtime = this->command_airtime_ms_;
  if (airtime == 0)
    airtime = static_cast<uint32_t>(this->redundancy_) * this->command_interval_ms_;
  return std::max(airtime, MIN_ADVERTISEMENT_MS);
}

void BrmeshHub::loop() {
#ifdef USE_ESP32
  const bool active = esp32_ble::global_ble != nullptr && esp32_ble::global_ble->is_active();
  if (!active) {
    this->handle_ble_inactive_();
    return;
  }
  if (!this->ble_was_active_) {
    this->ble_was_active_ = true;
    this->apply_tx_power_();
    if (this->ble_reset_pending_) {
      this->ble_reset_pending_ = false;
      ESP_LOGI(TAG, "BLE stack is active again after recovery reset");
      this->notify_transmit_dropped_(true);
    }
  }
#endif
  this->process_send_queue_();
  this->process_state_timeouts_();

  const uint32_t now = millis();
#ifdef USE_ESP32
  if (this->scan_wanted_())
    this->start_scan_();
  else
    this->stop_scan_();
#endif

  if (!this->boot_work_scheduled_ && (!this->lights_.empty() || !this->group_lights_.empty()))
    this->schedule_boot_work_();

  if (this->state_poll_interval_ms_ > 0 && (!this->lights_.empty() || !this->group_lights_.empty()) &&
      now - this->last_state_poll_ms_ >= this->state_poll_interval_ms_) {
    this->last_state_poll_ms_ = now;
    this->query_states(this->registered_addresses_());
  }

  if (this->pairing_mode_) {
    if (this->pairing_timeout_ms_ > 0 && now - this->pairing_started_ms_ >= this->pairing_timeout_ms_) {
      ESP_LOGI(TAG, "BRmesh pairing timed out");
      this->stop_pairing();
    } else if (now - this->last_pairing_scan_ms_ >= PAIRING_SCAN_INTERVAL_MS) {
      this->last_pairing_scan_ms_ = now;
      this->send_pairing_scan_request_();
    }
  }
}

void BrmeshHub::handle_ble_inactive_() {
#ifdef USE_ESP32
  if (this->ble_was_active_) {
    ESP_LOGW(TAG, "BLE stack went inactive; abandoning %u queued packet(s)", this->send_queue_.size());
    this->ble_was_active_ = false;
  }
  this->tx_power_set_ = false;
  this->scan_started_ = false;
  this->scan_pending_ = false;
  this->scan_stop_pending_ = false;
  this->tx_state_ = TxState::IDLE;
  this->active_advertisement_.clear();
  if (!this->send_queue_.empty())
    this->send_queue_.clear();
  if (this->ble_reset_pending_ && static_cast<int32_t>(millis() - this->ble_reset_at_ms_) >= 0) {
    // enable() is ignored until esp32_ble has finished dismantling, so keep asking each loop.
    esp32_ble::global_ble->enable();
  }
#endif
}

void BrmeshHub::request_ble_reset_() {
#ifdef USE_ESP32
  if (esp32_ble::global_ble == nullptr || this->ble_reset_pending_)
    return;
  ESP_LOGW(TAG, "Repeated BLE transmit failures; restarting the BLE stack");
  this->ble_reset_pending_ = true;
  this->ble_reset_at_ms_ = millis() + 1000;
  this->tx_state_ = TxState::IDLE;
  this->active_advertisement_.clear();
  esp32_ble::global_ble->disable();
#endif
}

void BrmeshHub::dump_config() {
  ESP_LOGCONFIG(TAG, "BRmesh Hub:");
  ESP_LOGCONFIG(TAG, "  Component version: %s", COMPONENT_VERSION);
  ESP_LOGCONFIG(TAG, "  Command airtime: %" PRIu32 " ms (continuous advertising per packet)",
                this->get_command_airtime_ms());
  ESP_LOGCONFIG(TAG, "  Membership airtime: %" PRIu32 " ms per window", MEMBERSHIP_AIRTIME_MS);
  ESP_LOGCONFIG(TAG, "  Group refresh interval: %" PRIu32 " ms (0 = before every command)",
                this->group_refresh_interval_ms_);
  ESP_LOGCONFIG(TAG, "  Group state: optimistic unless bulbs report state");
  ESP_LOGCONFIG(TAG, "  Listen for bulb frames: %s", YESNO(this->scan_wanted_()));
  ESP_LOGCONFIG(TAG, "  Query after write: %s", YESNO(this->query_after_write_));
  ESP_LOGCONFIG(TAG, "  Query redundancy: %u", this->query_redundancy_);
  ESP_LOGCONFIG(TAG, "  State query on boot: %s", YESNO(this->state_query_on_boot_));
  ESP_LOGCONFIG(TAG, "  State poll interval: %" PRIu32 " ms", this->state_poll_interval_ms_);
  ESP_LOGCONFIG(TAG, "  State timeout: %" PRIu32 " ms", this->state_timeout_ms_);
  ESP_LOGCONFIG(TAG, "  All group refresh on boot: %s", YESNO(this->all_group_refresh_on_boot_));
  ESP_LOGCONFIG(TAG, "  Recover BLE after repeated failures: %s", YESNO(this->recover_ble_));
  ESP_LOGCONFIG(TAG, "  Retry dropped writes: %s", YESNO(this->retry_dropped_writes_));
  ESP_LOGCONFIG(TAG, "  Relayed frames heard: %" PRIu32, this->relayed_frames_);
  ESP_LOGCONFIG(TAG, "  Cached devices: %u", this->stored_.count);
  for (uint16_t i = 0; i < this->stored_.count; i++) {
    const auto &device = this->stored_.devices[i];
    ESP_LOGCONFIG(TAG, "    - addr=%u did=%s type=%u", device.address, did_hex_(device.did).c_str(), device.type);
  }
  ESP_LOGCONFIG(TAG, "  Pairing pool: %s", this->pairing_addresses_.empty() ? "registered light addresses"
                                                                            : "pairing_addresses");
  const auto addresses = this->registered_addresses_();
  ESP_LOGCONFIG(TAG, "  Registered light count: %u", addresses.size());
  for (uint16_t address : addresses)
    ESP_LOGCONFIG(TAG, "    - addr=%u (0x%04X) high=%u low=%u", address, address, address >> 8, address & 0xFF);
  ESP_LOGCONFIG(TAG, "  Registered grouped light count: %u", this->group_lights_.size());
  for (const auto &group_light : this->group_lights_)
    ESP_LOGCONFIG(TAG, "    - grouped addrs=%u", group_light.addresses.size());
}

float BrmeshHub::get_setup_priority() const { return setup_priority::AFTER_BLUETOOTH; }

void BrmeshHub::set_key_hex(const std::string &key) {
  auto parsed = parse_hex_(key);
  if (parsed.size() != 4) {
    ESP_LOGE(TAG, "Invalid key length; expected exactly 4 bytes");
    return;
  }
  for (size_t i = 0; i < 4; i++)
    this->key_[i] = parsed[i];
}

void BrmeshHub::send_light_state(uint16_t address, bool state, float brightness, float red, float green, float blue,
                                 bool has_rgb, bool has_color_temperature, float color_temperature,
                                 uintptr_t owner) {
  const uint8_t high = address >> 8;
  const uint8_t level = state ? scale_brightness_(brightness) : 0;

  uint8_t cold = 0;
  uint8_t warm = 0;
  if (has_color_temperature && state) {
    float ct = clamp(color_temperature, 0.0f, 1.0f);
    warm = static_cast<uint8_t>(roundf(ct * 255.0f));
    cold = static_cast<uint8_t>(255 - warm);
  }

  this->cancel_owner_(owner);
  auto payload = BrmeshProtocol::build_light_payload(address, state, level, scale_color_(red), scale_color_(green),
                                                     scale_color_(blue), has_rgb, has_color_temperature, cold, warm);
  std::function<void()> after_write;
  if (this->query_after_write_)
    after_write = [this, address]() { this->query_state(address); };
  this->send_command_(5, payload.data(), payload.size(), this->key_.data(), true, high, 0,
                      this->get_command_airtime_ms(), 3, owner, after_write, address > 256);
}

void BrmeshHub::send_temp_group_light_state(const std::vector<uint16_t> &addresses, bool state, float brightness,
                                            float red, float green, float blue, bool has_rgb,
                                            bool has_color_temperature, float color_temperature, uintptr_t owner) {
  if (addresses.empty())
    return;

  // Newest request from this entity wins, exactly like the app stopping its previous advertisement.
  this->cancel_owner_(owner);
  // The phone app or another gateway may have re-used temporary group 253 since our last write.
  const bool refreshed = this->refresh_all_group_(addresses, 3, owner);

  const uint8_t level = state ? scale_brightness_(brightness) : 0;
  uint8_t cold = 0;
  uint8_t warm = 0;
  if (has_color_temperature && state) {
    float ct = clamp(color_temperature, 0.0f, 1.0f);
    warm = static_cast<uint8_t>(roundf(ct * 255.0f));
    cold = static_cast<uint8_t>(255 - warm);
  }

  auto command = BrmeshProtocol::build_group_light_command(state, level, scale_color_(red), scale_color_(green),
                                                           scale_color_(blue), has_rgb, has_color_temperature, cold,
                                                           warm);
  auto payload = BrmeshProtocol::build_group_control_payload(GROUP_CONTROL_TYPE, TEMP_GROUP_ID, command);
  ESP_LOGD(TAG, "BRmesh group light write count=%u state=%s level=%u rgb=%s ct=%s refresh=%s command=%s payload=%s",
           addresses.size(), ONOFF(state), level, YESNO(has_rgb), YESNO(has_color_temperature), YESNO(refreshed),
           BrmeshProtocol::format_hex(command).c_str(), BrmeshProtocol::format_hex(payload).c_str());
  std::function<void()> after_write;
  if (this->query_after_write_)
    after_write = [this, addresses]() { this->query_states(addresses); };
  this->send_command_(5, payload.data(), payload.size(), this->key_.data(), true, 0, 0,
                      this->get_command_airtime_ms(), 3, owner, after_write);
}

void BrmeshHub::send_scene_control(uint16_t address, uint8_t jiffies, const std::vector<uint8_t> &scene,
                                   uintptr_t owner) {
  if (scene.empty())
    return;
  this->cancel_owner_(owner);
  auto payload = BrmeshProtocol::build_scene_control_payload(address & 0xFF, jiffies, scene);
  ESP_LOGD(TAG, "BRmesh mode addr=%u jiffies=%u scene=%s", address, jiffies,
           BrmeshProtocol::format_hex(scene).c_str());
  this->send_command_(5, payload.data(), payload.size(), this->key_.data(), true, address >> 8, 0,
                      this->get_command_airtime_ms(), 3, owner, nullptr, address > 256);
}

void BrmeshHub::send_temp_group_scene_control(const std::vector<uint16_t> &addresses, uint8_t jiffies,
                                              const std::vector<uint8_t> &scene, uintptr_t owner) {
  if (addresses.empty() || scene.empty())
    return;
  this->cancel_owner_(owner);
  this->refresh_all_group_(addresses, 3, owner);
  auto payload = BrmeshProtocol::build_group_scene_control_payload(TEMP_GROUP_ID, jiffies, scene);
  ESP_LOGD(TAG, "BRmesh group mode count=%u jiffies=%u scene=%s payload=%s", addresses.size(), jiffies,
           BrmeshProtocol::format_hex(scene).c_str(), BrmeshProtocol::format_hex(payload).c_str());
  this->send_command_(5, payload.data(), payload.size(), this->key_.data(), true, 0, 0,
                      this->get_command_airtime_ms(), 3, owner);
}

void BrmeshHub::send_group_command(uint16_t type, uint8_t group_id, const std::vector<uint8_t> &command) {
  auto payload = BrmeshProtocol::build_group_control_payload(type, group_id, command);
  this->send_inner_payload_(payload.data(), payload.size(), 0);
}

void BrmeshHub::send_group_on_off(uint16_t type, uint8_t group_id, bool state) {
  const std::vector<uint8_t> command{static_cast<uint8_t>(state ? 0x80 : 0x00)};
  this->send_group_command(type, group_id, command);
}

void BrmeshHub::send_temp_group_on_off(const std::vector<uint16_t> &addresses, bool state) {
  if (addresses.empty())
    return;
  this->publish_group_on_off_(addresses, state);
  this->refresh_all_group_(addresses, 3, 0);

  const std::vector<uint8_t> command{static_cast<uint8_t>(state ? 0x80 : 0x00)};
  auto payload = BrmeshProtocol::build_group_control_payload(GROUP_CONTROL_TYPE, TEMP_GROUP_ID, command);
  this->send_command_(5, payload.data(), payload.size(), this->key_.data(), true, 0, 0,
                      this->get_command_airtime_ms(), 3, 0, [this, addresses]() {
                        if (this->query_after_write_)
                          this->query_states(addresses);
                      });
}

void BrmeshHub::register_light(uint16_t address, BrmeshLight *light) {
  for (auto &registered_light : this->lights_) {
    if (registered_light.address == address) {
      registered_light.light = light;
      this->boot_work_scheduled_ = false;
      return;
    }
  }
  this->lights_.push_back({address, light});
  this->boot_work_scheduled_ = false;
}

void BrmeshHub::register_group_light(const std::vector<uint16_t> &addresses, BrmeshLight *light) {
  if (addresses.empty())
    return;

  std::vector<uint16_t> sorted = addresses;
  std::sort(sorted.begin(), sorted.end());
  const uint32_t hash = this->hash_addresses_(sorted);
  for (auto &registered_light : this->group_lights_) {
    if (registered_light.hash == hash) {
      registered_light.addresses = sorted;
      registered_light.light = light;
      return;
    }
  }
  this->group_lights_.push_back({sorted, hash, light});
  this->boot_work_scheduled_ = false;
}

void BrmeshHub::publish_report(const BrmeshLightReport &report) {
  this->clear_pending_state_query_(report.address);
  if (this->pairing_mode_)
    this->confirm_pairing_(report.address, "status reply");
  // Identical consecutive reports are NOT filtered here: a bulb that keeps reporting ON after an OFF
  // command looks identical to its previous report, and that is exactly the straggler we want to see.
  // Lights only publish to Home Assistant when their aggregate actually changes.
  this->remember_report_(report);
  bool delivered = false;
  for (auto &registered_light : this->lights_) {
    if (registered_light.address == report.address && registered_light.light != nullptr) {
      registered_light.light->apply_report(report);
      delivered = true;
    }
  }
  for (auto &group : this->group_lights_) {
    if (group.light == nullptr)
      continue;
    if (std::binary_search(group.addresses.begin(), group.addresses.end(), report.address)) {
      group.light->apply_report(report);
      delivered = true;
    }
  }
  if (!delivered)
    ESP_LOGD(TAG, "No registered BRmesh light for state report addr=%u", report.address);
}

void BrmeshHub::query_state(uint16_t address) { this->query_states(std::vector<uint16_t>{address}); }

void BrmeshHub::query_states(const std::vector<uint16_t> &addresses) {
  if (addresses.empty())
    return;
  for (const auto &batch : BrmeshProtocol::build_address_batches(addresses)) {
    for (const auto &window : batch.windows) {
      if (window.empty())
        continue;
      auto payload = BrmeshProtocol::build_query_state_payload(window, 0x0F);
      ESP_LOGD(TAG, "Querying BRmesh state high=%u payload=%s", batch.high,
               BrmeshProtocol::format_hex(payload).c_str());
      const size_t before = this->send_queue_.size();
      this->send_command_(5, payload.data(), payload.size(), this->key_.data(), true, batch.high, 0,
                          QUERY_AIRTIME_MS * std::max<uint8_t>(this->query_redundancy_, 1), 1, 0);
      if (this->send_queue_.size() == before)
        continue;
      std::vector<uint16_t> query_addresses;
      for (uint16_t low : window)
        query_addresses.push_back((static_cast<uint16_t>(batch.high) << 8) | low);
      this->send_queue_.back().on_start = [this, query_addresses]() {
        this->mark_state_query_pending_(query_addresses);
      };
    }
  }
}

void BrmeshHub::send_raw_payload(const std::vector<uint8_t> &payload, uint8_t sequence) {
  if (payload.empty() || payload.size() > 18) {
    ESP_LOGW(TAG, "Ignoring raw payload with invalid length: %u", payload.size());
    return;
  }
  this->send_inner_payload_(payload.data(), payload.size(), 0, sequence);
}

void BrmeshHub::send_raw_payload_hex(const std::string &payload_hex, uint8_t sequence) {
  this->send_raw_payload(parse_hex_(payload_hex), sequence);
}

void BrmeshHub::send_scene(uint8_t scene) {
  const uint8_t payload[] = {0xB2, scene, 0x00, 0x00, 0x00, 0x00};
  this->send_inner_payload_(payload, sizeof(payload));
}

void BrmeshHub::start_pairing() {
  this->pairing_mode_ = true;
  this->pairing_started_ms_ = millis();
  this->last_pairing_scan_ms_ = this->pairing_started_ms_;
  this->pairing_candidates_.clear();
  uint16_t next = 0;
  if (this->allocate_pairing_address_(&next)) {
    ESP_LOGI(TAG, "BRmesh pairing mode enabled; power-cycle the new light now (next free address: %u)", next);
  } else {
    ESP_LOGW(TAG, "BRmesh pairing mode enabled but every configured address is already bound; add addresses to "
                  "your YAML or clear the pairing cache");
  }
#ifdef USE_ESP32
  this->start_scan_();
#endif
  this->send_pairing_scan_request_();
}

void BrmeshHub::stop_pairing() {
  if (!this->pairing_mode_)
    return;
  this->pairing_mode_ = false;
  this->pairing_candidates_.clear();
  ESP_LOGI(TAG, "BRmesh pairing mode disabled; %u device(s) cached", this->stored_.count);
}

void BrmeshHub::clear_pairings() {
  this->stored_ = {};
  this->stored_.magic = STORAGE_MAGIC;
  this->stored_.version = STORAGE_VERSION;
  this->save_pairings_();
  ESP_LOGI(TAG, "Cleared cached BRmesh pairings");
}

void BrmeshHub::enroll_device(uint16_t address, const std::string &did_hex, const std::string &device_key_hex,
                              uint16_t type, uint16_t group_id) {
  auto did = parse_hex_(did_hex);
  auto device_key = parse_hex_(device_key_hex);
  if (address == 0 || did.size() != 6 || device_key.size() != 4) {
    ESP_LOGW(TAG, "Invalid manual enrollment; expected address > 0, 6-byte DID, and 4-byte device key");
    return;
  }

  StoredDevice device{};
  device.address = address;
  device.type = type;
  device.group_id = group_id;
  device.high = address >> 8;
  memcpy(device.did, did.data(), sizeof(device.did));
  memcpy(device.key, device_key.data(), sizeof(device.key));
  if (this->upsert_pairing_(device)) {
    this->save_pairings_();
    ESP_LOGI(TAG, "Cached BRmesh device addr=%u did=%s type=%u group=%u", address, did_hex.c_str(), type, group_id);
  }
  this->send_discovery_response_(device);
}

#ifdef USE_ESP32
void BrmeshHub::gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {
  // BLE 4.2 controllers (classic ESP32) reject non-connectable advertising faster than 100 ms; BLE 5
  // controllers (ESP32-C3/S3) accept 20 ms. Start fast and fall back once if the controller refuses.
  esp_ble_adv_params_t adv_params = {
      .adv_int_min = static_cast<uint16_t>(this->adv_interval_slow_ ? 0xA0 : 0x20),
      .adv_int_max = static_cast<uint16_t>(this->adv_interval_slow_ ? 0xB0 : 0x40),
      .adv_type = ADV_TYPE_NONCONN_IND,
      .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
      .peer_addr = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
      .peer_addr_type = BLE_ADDR_TYPE_PUBLIC,
      .channel_map = ADV_CHNL_ALL,
      .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
  };

  switch (event) {
    case ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT:
      if (this->tx_state_ != TxState::CONFIGURING)
        break;
      if (param->adv_data_raw_cmpl.status != ESP_BT_STATUS_SUCCESS) {
        this->fail_advertisement_();
        break;
      }
      this->tx_state_ = TxState::STARTING;
      this->tx_deadline_ms_ = millis() + GAP_TIMEOUT_MS;
      if (esp_ble_gap_start_advertising(&adv_params) != ESP_OK)
        this->fail_advertisement_();
      break;
    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
      if (this->tx_state_ != TxState::STARTING)
        break;
      if (param->adv_start_cmpl.status != ESP_BT_STATUS_SUCCESS) {
        if (!this->adv_interval_slow_) {
          this->adv_interval_slow_ = true;
          ESP_LOGW(TAG, "Controller rejected 20 ms advertising interval; falling back to 100 ms");
        }
        this->fail_advertisement_();
        break;
      }
      this->tx_state_ = TxState::ADVERTISING;
      if (this->send_queue_.empty()) {
        this->tx_deadline_ms_ = millis();
        break;
      }
      this->tx_deadline_ms_ = millis() + std::max(MIN_ADVERTISEMENT_MS, this->send_queue_.front().airtime_ms);
      if (auto on_start = std::move(this->send_queue_.front().on_start)) {
        this->send_queue_.front().on_start = nullptr;
        on_start();
      }
      break;
    case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
      if (this->tx_state_ == TxState::RECOVERING) {
        this->tx_state_ = TxState::BACKOFF;
        this->tx_deadline_ms_ = millis() + 250;
      } else if (this->tx_state_ == TxState::STOPPING) {
        if (param->adv_stop_cmpl.status == ESP_BT_STATUS_SUCCESS)
          this->finish_advertisement_();
        else
          this->fail_advertisement_();
      }
      break;
    case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT:
      if (!this->scan_pending_)
        break;
      if (param->scan_param_cmpl.status != ESP_BT_STATUS_SUCCESS || esp_ble_gap_start_scanning(0) != ESP_OK) {
        this->scan_pending_ = false;
        this->scan_retry_at_ms_ = millis() + GAP_TIMEOUT_MS;
        ESP_LOGW(TAG, "BRmesh scan configuration failed; will retry");
      }
      break;
    case ESP_GAP_BLE_SCAN_START_COMPLETE_EVT:
      if (!this->scan_pending_)
        break;
      this->scan_pending_ = false;
      this->scan_started_ = param->scan_start_cmpl.status == ESP_BT_STATUS_SUCCESS;
      this->scan_retry_at_ms_ = millis() + GAP_TIMEOUT_MS;
      if (!this->scan_started_)
        ESP_LOGW(TAG, "BRmesh scan start failed; will retry");
      break;
    case ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT:
      if (!this->scan_stop_pending_)
        break;
      this->scan_stop_pending_ = false;
      this->scan_started_ = false;
      break;
    default:
      break;
  }
}

void BrmeshHub::gap_scan_event_handler(const esp32_ble::BLEScanResult &scan_result) {
  const uint8_t length = scan_result.adv_data_len + scan_result.scan_rsp_len;
  auto manufacturer = BrmeshProtocol::extract_brmesh_manufacturer(scan_result.ble_adv, length);
  if (manufacturer.empty())
    return;
  if (this->pairing_mode_)
    this->log_pairing_candidate_(scan_result.ble_adv, length, scan_result.rssi);
  this->handle_manufacturer_data(manufacturer, scan_result.rssi);
}
#endif

void BrmeshHub::handle_manufacturer_data(const std::vector<uint8_t> &manufacturer, int8_t rssi) {
  auto frame = BrmeshProtocol::decode_frame(manufacturer, this->key_.data());
  if (!frame.valid) {
    ESP_LOGD(TAG, "BRmesh rx undecoded rssi=%d reason=%s mfg=%s", rssi, frame.reason.c_str(),
             BrmeshProtocol::format_hex(manufacturer).c_str());
    return;
  }

  uint16_t address = 0;
  if (frame.command_type == BrmeshProtocol::COMMAND_TYPE_UPLOAD && !frame.payload.empty())
    address = (static_cast<uint16_t>(frame.high) << 8) | frame.payload[0];
  if (this->is_repeated_frame_(frame, address))
    return;

  if (frame.relayed)
    this->relayed_frames_++;
  ESP_LOGD(TAG, "BRmesh rx type=%u high=%u seq=%u fwd=%u enc=%s legacy=%s relayed=%s rssi=%d payload=%s",
           frame.command_type, frame.high, frame.sequence, frame.forward, YESNO(frame.encrypted),
           YESNO(frame.legacy), YESNO(frame.relayed), rssi, BrmeshProtocol::format_hex(frame.payload).c_str());

  switch (frame.command_type) {
    case BrmeshProtocol::COMMAND_TYPE_UPLOAD: {
      auto decoded_state = BrmeshProtocol::decode_state_from_frame(frame);
      if (!decoded_state.valid) {
        ESP_LOGD(TAG, "BRmesh upload frame ignored: %s", decoded_state.reason.c_str());
        break;
      }
      ESP_LOGI(TAG, "BRmesh device upload addr=%u subtype=%u data=%s", decoded_state.address,
               decoded_state.state_type, BrmeshProtocol::format_hex(decoded_state.payload).c_str());
      BrmeshLightReport report{};
      if (this->decoded_state_to_report_(decoded_state, &report))
        this->publish_report(report);
      break;
    }
    case BrmeshProtocol::COMMAND_TYPE_CONTROL: {
      // Either a bulb relaying a control frame (forward bit) or a device issuing one. payload[0] & 0x0f:
      // 4 = heartbeat (address, group, version) sent by a bulb after it joins, 11 = scene control.
      auto heartbeat = BrmeshProtocol::decode_heartbeat(frame);
      if (heartbeat.valid) {
        ESP_LOGI(TAG, "BRmesh heartbeat addr=%u group=%u rssi=%d", heartbeat.address, heartbeat.group, rssi);
        this->handle_heartbeat_(heartbeat);
      } else if (!frame.payload.empty()) {
        ESP_LOGD(TAG, "BRmesh control frame on air subtype=%u first=0x%02X", frame.payload[0] & 0x0F,
                 frame.payload[0]);
      }
      break;
    }
    case BrmeshProtocol::COMMAND_TYPE_DISCOVERY: {
      auto discovery = BrmeshProtocol::decode_discovery(frame);
      if (discovery.valid)
        this->handle_discovery_(discovery, rssi);
      break;
    }
    default:
      break;
  }
}

bool BrmeshHub::is_repeated_frame_(const BrmeshProtocol::DecodedFrame &frame, uint16_t address) {
  // Bulbs repeat every advertisement many times and neighbours relay it; only act on the first copy.
  // Bulbs do not necessarily bump the sequence number for every status upload, so the payload is part
  // of the key: an identical repeat is dropped, a changed status with the same sequence passes.
  const uint32_t now = millis();
  uint32_t payload_hash = 2166136261UL;
  for (uint8_t byte : frame.payload) {
    payload_hash ^= byte;
    payload_hash *= 16777619UL;
  }
  this->seen_frames_.erase(std::remove_if(this->seen_frames_.begin(), this->seen_frames_.end(),
                                          [now](const SeenFrame &seen) { return now - seen.seen_at_ms > 1500; }),
                           this->seen_frames_.end());
  for (auto &seen : this->seen_frames_) {
    if (seen.command_type == frame.command_type && seen.sequence == frame.sequence && seen.address == address &&
        seen.payload_hash == payload_hash) {
      seen.seen_at_ms = now;
      return true;
    }
  }
  if (this->seen_frames_.size() >= 32)
    this->seen_frames_.erase(this->seen_frames_.begin());
  this->seen_frames_.push_back({frame.command_type, frame.sequence, address, payload_hash, now});
  return false;
}

uint8_t BrmeshHub::next_sequence_(uint8_t sequence) {
  if (sequence != 0)
    return sequence;
  this->send_sequence_++;
  if (this->send_sequence_ == 0)
    this->send_sequence_ = 1;
  return this->send_sequence_;
}

void BrmeshHub::send_inner_payload_(const uint8_t *data, uint8_t length, uint8_t high, uint8_t sequence) {
  this->send_command_(5, data, length, this->key_.data(), true, high, sequence, 0);
}

void BrmeshHub::send_command_(uint8_t command_type, const uint8_t *data, uint8_t length, const uint8_t *key,
                              bool encrypt_body, uint8_t high, uint8_t sequence, uint32_t airtime_ms) {
  this->send_command_(command_type, data, length, key, encrypt_body, high, sequence, airtime_ms, 2, 0);
}

void BrmeshHub::send_command_(uint8_t command_type, const uint8_t *data, uint8_t length, const uint8_t *key,
                              bool encrypt_body, uint8_t high, uint8_t sequence, uint32_t airtime_ms,
                              uint8_t priority, uintptr_t owner, std::function<void()> on_complete,
                              bool force_new_protocol) {
  if (length > 18) {
    ESP_LOGW(TAG, "Payload too long: %u", length);
    return;
  }

  const uint8_t actual_sequence = this->next_sequence_(sequence);
  auto body = this->package_ble_fastcon_body_(data, length, high, actual_sequence, command_type, key, encrypt_body,
                                              force_new_protocol);
  const bool new_protocol = body.size() == 22;
  auto adv_payload = new_protocol ? BrmeshProtocol::build_new_protocol_advertisement(body)
                                  : BrmeshProtocol::build_legacy_advertisement(body);
  if (airtime_ms == 0)
    airtime_ms = this->get_command_airtime_ms();

  this->enqueue_advertisement_(adv_payload, airtime_ms, priority, high, new_protocol, owner, std::move(on_complete));
  ESP_LOGD(TAG, "Queued BRmesh tx type=%u high=%u seq=%u priority=%u airtime=%" PRIu32 "ms new=%s payload=%s adv=%s",
           command_type, high, actual_sequence, priority, airtime_ms, YESNO(new_protocol),
           BrmeshProtocol::format_hex(std::vector<uint8_t>(data, data + length)).c_str(),
           BrmeshProtocol::format_hex(adv_payload).c_str());
}

void BrmeshHub::enqueue_advertisement_(std::vector<uint8_t> raw_advertisement, uint32_t airtime_ms, uint8_t priority,
                                       uint8_t high, bool new_protocol, uintptr_t owner,
                                       std::function<void()> on_complete) {
  QueuedAdvertisement item;
  item.raw_advertisement = std::move(raw_advertisement);
  item.airtime_ms = std::max(airtime_ms, MIN_ADVERTISEMENT_MS);
  item.priority = priority;
  item.high = high;
  item.new_protocol = new_protocol;
  item.owner = owner;
  item.next_send_ms = millis();
  item.on_complete = std::move(on_complete);
  this->send_queue_.push_back(std::move(item));
}

void BrmeshHub::cancel_owner_(uintptr_t owner) {
  if (owner == 0 || this->send_queue_.empty())
    return;
  const bool front_in_flight = this->tx_state_ == TxState::CONFIGURING || this->tx_state_ == TxState::STARTING ||
                               this->tx_state_ == TxState::ADVERTISING || this->tx_state_ == TxState::STOPPING;
  size_t removed = 0;
  for (size_t i = 0; i < this->send_queue_.size();) {
    auto &item = this->send_queue_[i];
    if (item.owner != owner) {
      i++;
      continue;
    }
    if (i == 0 && front_in_flight) {
      // Already on air: let it stop as soon as possible and never run its completion work.
      item.cancelled = true;
      item.on_complete = nullptr;
      if (this->tx_state_ == TxState::ADVERTISING)
        this->tx_deadline_ms_ = millis();
      i++;
      continue;
    }
    this->send_queue_.erase(this->send_queue_.begin() + i);
    removed++;
  }
  if (removed > 0)
    ESP_LOGD(TAG, "Superseded %u queued packet(s) with a newer request", removed);
}

void BrmeshHub::process_send_queue_() {
  if (!this->is_ready())
    return;
  const uint32_t now = millis();
  if (this->tx_state_ != TxState::IDLE) {
    if (static_cast<int32_t>(now - this->tx_deadline_ms_) < 0)
      return;
    if (this->tx_state_ == TxState::BACKOFF) {
      this->tx_state_ = TxState::IDLE;
    } else if (this->tx_state_ == TxState::RECOVERING) {
      this->tx_state_ = TxState::BACKOFF;
      this->tx_deadline_ms_ = now + 250;
      return;
    } else if (this->tx_state_ == TxState::ADVERTISING) {
      this->tx_state_ = TxState::STOPPING;
      this->tx_deadline_ms_ = now + GAP_TIMEOUT_MS;
#ifdef USE_ESP32
      if (esp_ble_gap_stop_advertising() != ESP_OK)
        this->fail_advertisement_();
#endif
      return;
    } else {
      ESP_LOGW(TAG, "BRmesh BLE operation timed out");
      this->fail_advertisement_();
      return;
    }
  }
  if (this->send_queue_.empty())
    return;

  // Strict FIFO: a temporary-group membership packet and its control packet must stay adjacent.
  auto &item = this->send_queue_.front();
  if (item.cancelled) {
    this->send_queue_.erase(this->send_queue_.begin());
    return;
  }
  if (static_cast<int32_t>(now - item.next_send_ms) < 0)
    return;
  ESP_LOGD(TAG, "Sending BRmesh tx high=%u airtime=%" PRIu32 "ms queue=%u new=%s", item.high, item.airtime_ms,
           this->send_queue_.size(), YESNO(item.new_protocol));
  this->advertise_(item.raw_advertisement);
}

void BrmeshHub::finish_advertisement_() {
  this->tx_state_ = TxState::IDLE;
  this->tx_failures_ = 0;
  this->dropped_bursts_ = 0;
  this->status_clear_warning();
  this->active_advertisement_.clear();
  if (this->send_queue_.empty())
    return;
  QueuedAdvertisement item = std::move(this->send_queue_.front());
  this->send_queue_.erase(this->send_queue_.begin());
  if (!item.cancelled && item.on_complete)
    item.on_complete();
}

void BrmeshHub::drop_queue_(const char *why) {
  ESP_LOGW(TAG, "%s; dropping %u queued packet(s) (state is unconfirmed)", why, this->send_queue_.size());
  this->send_queue_.clear();
  this->last_refresh_valid_ = false;
}

void BrmeshHub::fail_advertisement_() {
  this->status_set_warning();
  bool reset_requested = false;
  if (++this->tx_failures_ >= MAX_TX_FAILURES) {
    // A failed membership packet invalidates every dependent group command, so the burst is discarded
    // without completion callbacks. Lights re-issue their latest requested state once BLE recovers.
    this->drop_queue_("BRmesh BLE failed three times");
    this->tx_failures_ = 0;
    if (this->recover_ble_ && ++this->dropped_bursts_ >= DROPPED_BURSTS_BEFORE_BLE_RESET) {
      this->dropped_bursts_ = 0;
      this->request_ble_reset_();
      reset_requested = true;
    }
    this->notify_transmit_dropped_();
  } else {
    ESP_LOGW(TAG, "BRmesh BLE transmit failed; recovering before retry %u", this->tx_failures_);
  }
  if (reset_requested)
    return;
  this->tx_state_ = TxState::RECOVERING;
  this->tx_deadline_ms_ = millis() + GAP_TIMEOUT_MS;
#ifdef USE_ESP32
  if (esp_ble_gap_stop_advertising() != ESP_OK) {
    this->tx_state_ = TxState::BACKOFF;
    this->tx_deadline_ms_ = millis() + 250;
  }
#endif
}

void BrmeshHub::notify_transmit_dropped_(bool after_ble_reset) {
  if (!this->retry_dropped_writes_)
    return;
  for (auto &registered_light : this->lights_) {
    if (registered_light.light != nullptr)
      registered_light.light->on_transmit_dropped(after_ble_reset);
  }
  for (auto &group : this->group_lights_) {
    if (group.light != nullptr)
      group.light->on_transmit_dropped(after_ble_reset);
  }
}

void BrmeshHub::process_state_timeouts_() {
  if (this->pending_state_queries_.empty())
    return;
  const uint32_t now = millis();
  for (auto it = this->pending_state_queries_.begin(); it != this->pending_state_queries_.end();) {
    if (static_cast<int32_t>(now - it->expires_at_ms) >= 0) {
      ESP_LOGD(TAG, "BRmesh state query timed out addr=%u", it->address);
      it = this->pending_state_queries_.erase(it);
    } else {
      ++it;
    }
  }
}

void BrmeshHub::mark_state_query_pending_(const std::vector<uint16_t> &addresses) {
  if (this->state_timeout_ms_ == 0)
    return;
  const uint32_t expires_at = millis() + this->state_timeout_ms_;
  for (uint16_t address : addresses) {
    if (address == 0)
      continue;
    auto it = std::find_if(this->pending_state_queries_.begin(), this->pending_state_queries_.end(),
                           [address](const PendingStateQuery &query) { return query.address == address; });
    if (it == this->pending_state_queries_.end()) {
      this->pending_state_queries_.push_back({address, expires_at});
    } else {
      it->expires_at_ms = expires_at;
    }
  }
}

void BrmeshHub::clear_pending_state_query_(uint16_t address) {
  this->pending_state_queries_.erase(
      std::remove_if(this->pending_state_queries_.begin(), this->pending_state_queries_.end(),
                     [address](const PendingStateQuery &query) { return query.address == address; }),
      this->pending_state_queries_.end());
}

std::vector<uint8_t> BrmeshHub::package_ble_fastcon_body_(const uint8_t *data, uint8_t length, uint8_t high,
                                                          uint8_t sequence, uint8_t command_type, const uint8_t *key,
                                                          bool encrypt_body, bool force_new_protocol) {
  std::vector<uint8_t> input(data, data + length);
  return BrmeshProtocol::package_fastcon_body(input, high, sequence, command_type, key, encrypt_body,
                                             force_new_protocol);
}

std::vector<uint16_t> BrmeshHub::registered_addresses_() const {
  std::vector<uint16_t> addresses;
  addresses.reserve(this->lights_.size());
  for (const auto &registered_light : this->lights_) {
    if (registered_light.address != 0)
      addresses.push_back(registered_light.address);
  }
  for (const auto &group : this->group_lights_)
    addresses.insert(addresses.end(), group.addresses.begin(), group.addresses.end());
  std::sort(addresses.begin(), addresses.end());
  addresses.erase(std::unique(addresses.begin(), addresses.end()), addresses.end());
  return addresses;
}

uint32_t BrmeshHub::hash_addresses_(const std::vector<uint16_t> &addresses) const {
  std::vector<uint16_t> sorted = addresses;
  std::sort(sorted.begin(), sorted.end());
  uint32_t hash = 2166136261UL;
  for (uint16_t address : sorted) {
    hash ^= address & 0xFF;
    hash *= 16777619UL;
    hash ^= address >> 8;
    hash *= 16777619UL;
  }
  return hash;
}

void BrmeshHub::publish_group_on_off_(const std::vector<uint16_t> &addresses, bool state) {
  std::vector<uint16_t> sorted = addresses;
  std::sort(sorted.begin(), sorted.end());
  const uint32_t hash = this->hash_addresses_(sorted);
  for (auto &registered_light : this->group_lights_) {
    if (registered_light.hash == hash && registered_light.addresses == sorted && registered_light.light != nullptr)
      registered_light.light->apply_group_on_off(state);
  }
}

bool BrmeshHub::refresh_all_group_(const std::vector<uint16_t> &addresses, uint8_t priority, uintptr_t owner) {
  if (addresses.empty())
    return false;
  const uint32_t hash = this->hash_addresses_(addresses);
  const uint32_t now = millis();
  if (this->group_refresh_interval_ms_ > 0 && this->last_refresh_valid_ && this->last_refresh_hash_ == hash &&
      now - this->last_refresh_done_ms_ < this->group_refresh_interval_ms_) {
    ESP_LOGD(TAG, "Temporary group membership still fresh; skipping refresh");
    return false;
  }
  // Distinct even when multiple requests arrive in the same millisecond.
  uint16_t token = ++this->group_token_;
  if (token == 0)
    token = ++this->group_token_;

  std::vector<std::vector<uint8_t>> payloads;
  std::vector<uint8_t> highs;
  for (const auto &batch : BrmeshProtocol::build_address_batches(addresses)) {
    for (const auto &window : batch.windows) {
      if (window.empty())
        continue;
      payloads.push_back(BrmeshProtocol::build_temp_group_payload(TEMP_GROUP_ID, window, token));
      highs.push_back(batch.high);
    }
  }
  this->last_refresh_valid_ = false;
  for (size_t i = 0; i < payloads.size(); i++) {
    std::function<void()> on_complete;
    if (i + 1 == payloads.size()) {
      on_complete = [this, hash]() {
        this->last_refresh_hash_ = hash;
        this->last_refresh_done_ms_ = millis();
        this->last_refresh_valid_ = true;
      };
    }
    this->send_command_(5, payloads[i].data(), payloads[i].size(), this->key_.data(), true, highs[i], 0,
                        MEMBERSHIP_AIRTIME_MS, priority, owner, on_complete);
  }
  return !payloads.empty();
}

void BrmeshHub::schedule_boot_work_() {
  this->boot_work_scheduled_ = true;
  this->last_state_poll_ms_ = millis();
  const auto addresses = this->registered_addresses_();
  if (this->all_group_refresh_on_boot_)
    this->refresh_all_group_(addresses, 0, 0);
  if (this->state_query_on_boot_)
    this->query_states(addresses);
}

bool BrmeshHub::decoded_state_to_report_(const BrmeshProtocol::DecodedState &state, BrmeshLightReport *report) const {
  // Frame framing (address, sub type, data) is taken from the native parser. The meaning of the data bytes
  // for a light is NOT confirmed by a capture yet; this mirrors the app's own light command layout
  // [power|level, blue, red, green, warm, cold] because devices echo command formats elsewhere in this
  // protocol. Treat the result as a best-effort hint until docs/brmesh-state-frame-notes.md has a fixture.
  if (!state.valid || report == nullptr || state.state_type != 0 || state.payload.empty())
    return false;

  const uint8_t *data = state.payload.data();
  const size_t length = state.payload.size();
  *report = BrmeshLightReport{};
  report->address = state.address;
  report->state = data[0] != 0;
  report->brightness = data[0] & 0x7F;
  if (length >= 4 && (data[1] | data[2] | data[3]) != 0) {
    report->has_rgb = true;
    report->blue = data[1];
    report->red = data[2];
    report->green = data[3];
  } else if (length >= 6 && (data[4] | data[5]) != 0) {
    report->has_color_temperature = true;
    report->warm = data[4];
    report->cold = data[5];
  }
  return true;
}

void BrmeshHub::remember_report_(const BrmeshLightReport &report) {
  auto it = std::find_if(this->last_reports_.begin(), this->last_reports_.end(),
                         [&report](const BrmeshLightReport &known) { return known.address == report.address; });
  if (it == this->last_reports_.end())
    this->last_reports_.push_back(report);
  else
    *it = report;
}

bool BrmeshHub::scan_wanted_() const { return this->listen_ || this->pairing_mode_ || this->state_features_enabled_(); }

void BrmeshHub::send_pairing_scan_request_() {
  // BLEFastconHelper.sendStartScan(): command type 0, 12 zero bytes, null key.
  const uint8_t payload[12] = {0};
  this->send_command_(0, payload, sizeof(payload), nullptr, false, 0, 0, 1500);
}

std::string BrmeshHub::did_hex_(const uint8_t *did) {
  return BrmeshProtocol::format_hex(std::vector<uint8_t>(did, did + 6));
}

BrmeshHub::StoredDevice *BrmeshHub::find_pairing_by_did_(const std::array<uint8_t, 6> &did) {
  for (uint16_t i = 0; i < this->stored_.count; i++) {
    if (memcmp(this->stored_.devices[i].did, did.data(), 6) == 0)
      return &this->stored_.devices[i];
  }
  return nullptr;
}

bool BrmeshHub::address_bound_(uint16_t address) const {
  for (uint16_t i = 0; i < this->stored_.count; i++) {
    if (this->stored_.devices[i].address == address)
      return true;
  }
  return false;
}

bool BrmeshHub::allocate_pairing_address_(uint16_t *address) const {
  // Pool: pairing_addresses if configured, else every address a light entity was declared for. Lowest
  // address that no cached device owns wins. ESPHome cannot add entities at runtime, so the YAML must
  // already contain the address a new bulb will get.
  std::vector<uint16_t> pool = this->pairing_addresses_.empty() ? this->registered_addresses_() : this->pairing_addresses_;
  std::sort(pool.begin(), pool.end());
  for (uint16_t candidate : pool) {
    if (candidate == 0 || this->address_bound_(candidate))
      continue;
    if (address != nullptr)
      *address = candidate;
    return true;
  }
  return false;
}

void BrmeshHub::handle_discovery_(const BrmeshProtocol::Discovery &discovery, int8_t rssi) {
  const std::string did = did_hex_(discovery.did.data());
  if (!this->pairing_mode_) {
    ESP_LOGD(TAG, "BRmesh discovery from did=%s type=%u rssi=%d (pairing mode is off)", did.c_str(),
             discovery.device_type, rssi);
    return;
  }
  const uint32_t now = millis();
  auto candidate = std::find_if(this->pairing_candidates_.begin(), this->pairing_candidates_.end(),
                                [&discovery](const PairingCandidate &c) { return c.did == discovery.did; });
  StoredDevice *stored = this->find_pairing_by_did_(discovery.did);
  if (candidate == this->pairing_candidates_.end()) {
    PairingCandidate fresh{};
    fresh.did = discovery.did;
    if (stored != nullptr) {
      fresh.address = stored->address;
      ESP_LOGI(TAG, "BRmesh known bulb did=%s announcing again; re-sending address %u", did.c_str(), stored->address);
    } else {
      uint16_t address = 0;
      if (!this->allocate_pairing_address_(&address)) {
        ESP_LOGW(TAG, "BRmesh bulb did=%s type=%u wants to pair but no configured address is free", did.c_str(),
                 discovery.device_type);
        fresh.confirmed = true;  // remember it so the warning is not repeated every 3 s
        this->pairing_candidates_.push_back(fresh);
        return;
      }
      fresh.address = address;
      StoredDevice device{};
      device.address = address;
      device.type = discovery.device_type;
      device.group_id = 0;
      device.high = address >> 8;
      memcpy(device.did, discovery.did.data(), sizeof(device.did));
      memcpy(device.key, discovery.key.data(), sizeof(device.key));
      if (!this->upsert_pairing_(device))
        return;
      this->save_pairings_();
      stored = this->find_pairing_by_did_(discovery.did);
      ESP_LOGI(TAG, "BRmesh new bulb did=%s type=%u rssi=%d assigned address %u", did.c_str(),
               discovery.device_type, rssi, address);
    }
    this->pairing_candidates_.push_back(fresh);
    candidate = this->pairing_candidates_.end() - 1;
  }
  if (candidate->confirmed || stored == nullptr)
    return;
  if (candidate->responses >= PAIRING_MAX_RESPONSES) {
    ESP_LOGW(TAG, "BRmesh bulb did=%s did not confirm address %u after %u responses", did.c_str(),
             candidate->address, candidate->responses);
    candidate->confirmed = true;
    return;
  }
  if (candidate->responses > 0 && now - candidate->last_response_ms < PAIRING_RESPONSE_INTERVAL_MS)
    return;
  candidate->responses++;
  candidate->last_response_ms = now;
  // The bulb's own key from the discovery frame may differ from an older cache entry; keep it current.
  memcpy(stored->key, discovery.key.data(), sizeof(stored->key));
  this->send_discovery_response_(*stored);
}

void BrmeshHub::handle_heartbeat_(const BrmeshProtocol::Heartbeat &heartbeat) {
  if (this->pairing_mode_)
    this->confirm_pairing_(heartbeat.address, "heartbeat");
}

void BrmeshHub::confirm_pairing_(uint16_t address, const char *how) {
  for (auto &candidate : this->pairing_candidates_) {
    if (candidate.address != address || candidate.confirmed)
      continue;
    candidate.confirmed = true;
    const std::string did = did_hex_(candidate.did.data());
    ESP_LOGI(TAG, "BRmesh bulb did=%s paired as address %u (confirmed by %s)", did.c_str(), address, how);
    for (auto &callback : this->device_paired_callbacks_)
      callback(address, did);
    // Give Home Assistant its first real state.
    if (this->state_features_enabled_())
      this->query_state(address);
    return;
  }
}

void BrmeshHub::send_discovery_response_(const StoredDevice &device) {
  // BLEFastconHelper.sendDiscRes: command type 2, body encrypted with the BULB's key, 3 s on air.
  std::vector<uint8_t> payload;
  this->package_discovery_response_(device, &payload);
  const bool is_new = payload.size() > 12;
  this->send_command_(2, payload.data(), payload.size(), device.key, true, is_new ? device.address / 256 : 0, 0,
                      std::max<uint32_t>(this->get_command_airtime_ms(), 1500), 3, 0, nullptr, is_new);
  ESP_LOGI(TAG, "Sent BRmesh discovery response did=%s addr=%u", did_hex_(device.did).c_str(), device.address);
}

void BrmeshHub::package_discovery_response_(const StoredDevice &device, std::vector<uint8_t> *payload) {
  std::array<uint8_t, 6> did{};
  memcpy(did.data(), device.did, 6);
  *payload = BrmeshProtocol::build_discovery_response(did, device.address, device.type, this->key_.data(),
                                                      device.group_id & 0xFF);
}

#ifdef USE_ESP32
void BrmeshHub::apply_tx_power_() {
  if (this->tx_power_set_)
    return;
  this->tx_power_set_ = true;
  // The app advertises with ADVERTISE_TX_POWER_HIGH; use the strongest level every ESP32 variant accepts.
  if (esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_P9) != ESP_OK)
    ESP_LOGW(TAG, "Could not raise BLE advertising TX power");
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, ESP_PWR_LVL_P9);
}

void BrmeshHub::start_scan_() {
  if (this->scan_started_ || this->scan_pending_ || this->scan_stop_pending_ || esp32_ble::global_ble == nullptr ||
      !esp32_ble::global_ble->is_active())
    return;
  if (static_cast<int32_t>(millis() - this->scan_retry_at_ms_) < 0)
    return;

  // 50 % duty cycle: enough to catch bulb replies (they repeat for hundreds of ms) without flooding
  // ESPHome's BLE event queue, which would drop the advertising callbacks the transmitter waits on.
  static esp_ble_scan_params_t scan_params = {
      .scan_type = BLE_SCAN_TYPE_PASSIVE,
      .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
      .scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL,
      .scan_interval = 0x60,
      .scan_window = 0x30,
      .scan_duplicate = BLE_SCAN_DUPLICATE_DISABLE,
  };
  this->scan_pending_ = true;
  this->scan_retry_at_ms_ = millis() + GAP_TIMEOUT_MS;
  esp_err_t err = esp_ble_gap_set_scan_params(&scan_params);
  if (err != ESP_OK) {
    this->scan_pending_ = false;
    ESP_LOGW(TAG, "esp_ble_gap_set_scan_params failed: %s", esp_err_to_name(err));
  }
}

void BrmeshHub::stop_scan_() {
  if (!this->scan_started_ || this->scan_stop_pending_ || this->scan_pending_)
    return;
  this->scan_stop_pending_ = true;
  if (esp_ble_gap_stop_scanning() != ESP_OK) {
    this->scan_stop_pending_ = false;
    return;
  }
  ESP_LOGD(TAG, "BRmesh scanning stopped; nothing is listening for bulb frames");
}
#endif

void BrmeshHub::load_pairings_() {
  StoredDevices stored{};
  if (!this->pref_.load(&stored) || stored.magic != STORAGE_MAGIC || stored.version != STORAGE_VERSION ||
      stored.count > 32) {
    this->stored_ = {};
    this->stored_.magic = STORAGE_MAGIC;
    this->stored_.version = STORAGE_VERSION;
    return;
  }
  this->stored_ = stored;
  for (uint16_t i = 0; i < this->stored_.count; i++)
    this->next_pairing_address_ = std::max<uint16_t>(this->next_pairing_address_, this->stored_.devices[i].address + 1);
}

void BrmeshHub::save_pairings_() {
  this->pref_.save(&this->stored_);
  global_preferences->sync();
}

bool BrmeshHub::upsert_pairing_(const StoredDevice &device) {
  if (auto *existing = this->find_pairing_(device.address)) {
    *existing = device;
    return true;
  }
  if (this->stored_.count >= 32) {
    ESP_LOGW(TAG, "Pairing cache is full; cannot cache addr=%u", device.address);
    return false;
  }
  this->stored_.devices[this->stored_.count++] = device;
  this->next_pairing_address_ = std::max<uint16_t>(this->next_pairing_address_, device.address + 1);
  return true;
}

BrmeshHub::StoredDevice *BrmeshHub::find_pairing_(uint16_t address) {
  for (uint16_t i = 0; i < this->stored_.count; i++) {
    if (this->stored_.devices[i].address == address)
      return &this->stored_.devices[i];
  }
  return nullptr;
}

void BrmeshHub::log_pairing_candidate_(const uint8_t *adv, uint8_t length, int8_t rssi) {
  char hex[63]{};
  const uint8_t limit = std::min<uint8_t>(length, 31);
  for (uint8_t i = 0; i < limit; i++)
    snprintf(hex + (i * 2), sizeof(hex) - (i * 2), "%02X", adv[i]);
  ESP_LOGI(TAG, "BRmesh pairing candidate rssi=%d adv=%s", rssi, hex);
}

bool BrmeshHub::has_brmesh_manufacturer_(const uint8_t *adv, uint8_t length) {
  for (uint8_t i = 0; i + 3 < length; i++) {
    if (adv[i] == 0xFF && adv[i + 1] == 0xF0 && adv[i + 2] == 0xFF)
      return true;
  }
  return false;
}

void BrmeshHub::advertise_(const std::vector<uint8_t> &raw_advertisement) {
#ifdef USE_ESP32
  // Keep a stable buffer until GAP confirms completion of the entire burst.
  this->active_advertisement_ = raw_advertisement;
  this->tx_state_ = TxState::CONFIGURING;
  this->tx_deadline_ms_ = millis() + GAP_TIMEOUT_MS;
  esp_err_t err = esp_ble_gap_config_adv_data_raw(this->active_advertisement_.data(),
                                                 this->active_advertisement_.size());
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "esp_ble_gap_config_adv_data_raw failed: %s", esp_err_to_name(err));
    this->fail_advertisement_();
  }
#endif
}

uint8_t BrmeshHub::scale_brightness_(float value) {
  if (value <= 0.0f)
    return 0;
  return clamp<uint8_t>(static_cast<uint8_t>(roundf(value * 127.0f)), 1, 127);
}

uint8_t BrmeshHub::scale_color_(float value) {
  return clamp<uint8_t>(static_cast<uint8_t>(roundf(value * 255.0f)), 0, 255);
}

std::vector<uint8_t> BrmeshHub::parse_hex_(const std::string &hex) { return BrmeshProtocol::parse_hex(hex); }

// ---------------------------------------------------------------------------------------------------------
// BrmeshLight
// ---------------------------------------------------------------------------------------------------------

light::LightTraits BrmeshLight::get_traits() {
  auto traits = light::LightTraits();
  // Home Assistant only accepts BRIGHTNESS as a light's sole color mode; listing it next to RGB /
  // COLOR_TEMPERATURE makes HA drop it and then reject any state published in it ("unsupported color
  // mode brightness"). RGB already implies dimming.
  if (this->supports_cold_warm_) {
    traits.set_supported_color_modes({light::ColorMode::RGB, light::ColorMode::COLOR_TEMPERATURE});
  } else {
    traits.set_supported_color_modes({light::ColorMode::RGB});
  }
  traits.set_min_mireds(153);
  traits.set_max_mireds(500);
  return traits;
}

void BrmeshLight::setup_state(light::LightState *state) {
  this->state_ = state;
  if (this->parent_ != nullptr && this->addresses_.empty())
    this->parent_->register_light(this->address_, this);
  if (this->parent_ != nullptr && !this->addresses_.empty())
    this->parent_->register_group_light(this->addresses_, this);
}

void BrmeshLight::write_state(light::LightState *state) {
  this->state_ = state;
  this->last_write_ms_ = millis();
  this->pending_values_ = state->current_values;
  this->last_values_ = this->pending_values_;
  this->has_last_write_ = true;
  this->retry_budget_ = 1;
  this->write_pending_ = true;
}

void BrmeshLight::loop() {
  if (!this->write_pending_ || this->parent_ == nullptr || !this->parent_->is_ready())
    return;
  // Rate limit like BLEFastconHelper's 100 ms throttle, but keep (not drop) the newest value.
  const uint32_t now = millis();
  if (this->last_dispatch_ms_ != 0 && now - this->last_dispatch_ms_ < MIN_DISPATCH_INTERVAL_MS)
    return;
  this->last_dispatch_ms_ = now;
  this->write_pending_ = false;
  this->dispatch_(this->pending_values_);
  // A normal command ends the bulb-side animation; while an effect is selected, re-arm it right after so
  // a brightness change from Home Assistant does not silently stop the mode.
  if (this->mode_active_ && this->pending_values_.get_state() > 0.0f)
    this->dispatch_mode_();
}

void BrmeshLight::start_mode(const std::vector<uint8_t> &scene, uint8_t jiffies) {
  this->mode_scene_ = scene;
  this->mode_jiffies_ = jiffies;
  this->mode_active_ = true;
  if (this->parent_ == nullptr || !this->parent_->is_ready()) {
    // dispatched from loop() once BLE is up, right after the pending normal write
    this->write_pending_ = this->has_last_write_;
    return;
  }
  this->dispatch_mode_();
}

void BrmeshLight::stop_mode() {
  if (!this->mode_active_)
    return;
  this->mode_active_ = false;
  // ESPHome writes the light state after stopping an effect; make sure that happens even if the values
  // did not change, so the bulbs leave the animation.
  if (this->has_last_write_) {
    this->pending_values_ = this->last_values_;
    this->write_pending_ = true;
  }
}

void BrmeshLight::dispatch_mode_() {
  if (this->parent_ == nullptr || this->mode_scene_.empty())
    return;
  // Owner 0 on purpose: the mode packet must queue behind (not cancel) the normal write just dispatched.
  if (!this->addresses_.empty())
    this->parent_->send_temp_group_scene_control(this->addresses_, this->mode_jiffies_, this->mode_scene_, 0);
  else
    this->parent_->send_scene_control(this->address_, this->mode_jiffies_, this->mode_scene_, 0);
}

void BrmeshLight::dispatch_(const light::LightColorValues &values) {
  // During a transition state is fractional. Any nonzero value is ON.
  const bool binary = values.get_state() > 0.0f;
  const float brightness = values.get_state() * values.get_brightness();
  // BRMesh has separate level and RGB fields; as_rgb() already multiplies by
  // brightness and would dim the result twice when the bulb applies its level.
  const float red = values.get_red() * values.get_color_brightness();
  const float green = values.get_green() * values.get_color_brightness();
  const float blue = values.get_blue() * values.get_color_brightness();
  const float color_temperature = (values.get_color_temperature() - 153.0f) / (500.0f - 153.0f);
  const auto color_mode = values.get_color_mode();
  const bool has_rgb = color_mode == light::ColorMode::RGB;
  const bool has_color_temperature = this->supports_cold_warm_ && color_mode == light::ColorMode::COLOR_TEMPERATURE;
  const uintptr_t owner = reinterpret_cast<uintptr_t>(this);
  if (!this->addresses_.empty()) {
    this->parent_->send_temp_group_light_state(this->addresses_, binary, brightness, red, green, blue, has_rgb,
                                              has_color_temperature, color_temperature, owner);
  } else {
    this->parent_->send_light_state(this->address_, binary, brightness, red, green, blue, has_rgb,
                                    has_color_temperature, color_temperature, owner);
  }
}

void BrmeshLight::on_transmit_dropped(bool after_ble_reset) {
  if (!this->has_last_write_ || this->write_pending_)
    return;
  // One retry per user request, plus one more attempt once the BLE stack has been restarted.
  if (this->retry_budget_ == 0 && !after_ble_reset) {
    ESP_LOGW(TAG, "BRmesh light write was dropped twice; Home Assistant state may not match the bulbs");
    return;
  }
  if (this->retry_budget_ > 0)
    this->retry_budget_--;
  ESP_LOGW(TAG, "BRmesh light write was dropped; re-sending the last requested state%s",
           after_ble_reset ? " after the BLE restart" : "");
  this->pending_values_ = this->last_values_;
  this->write_pending_ = true;
  this->last_dispatch_ms_ = 0;
}

bool BrmeshLight::values_differ_(const light::LightColorValues &a, const light::LightColorValues &b) const {
  auto differs = [](float x, float y, float tolerance) { return fabsf(x - y) > tolerance; };
  if ((a.get_state() > 0.0f) != (b.get_state() > 0.0f))
    return true;
  if (a.get_state() <= 0.0f)
    return false;  // both off: brightness/color are remembered, not displayed
  if (differs(a.get_brightness(), b.get_brightness(), 0.5f / 127.0f) || a.get_color_mode() != b.get_color_mode())
    return true;
  if (a.get_color_mode() == light::ColorMode::COLOR_TEMPERATURE)
    return differs(a.get_color_temperature(), b.get_color_temperature(), 1.0f);
  if (a.get_color_mode() == light::ColorMode::RGB) {
    return differs(a.get_red(), b.get_red(), 0.5f / 255.0f) || differs(a.get_green(), b.get_green(), 0.5f / 255.0f) ||
           differs(a.get_blue(), b.get_blue(), 0.5f / 255.0f);
  }
  return false;
}

void BrmeshLight::publish_values_(const light::LightColorValues &values) {
  if (this->state_ == nullptr)
    return;
  // Seventeen bulbs answering one query would otherwise publish seventeen identical updates.
  if (!this->values_differ_(values, this->state_->remote_values))
    return;
  this->state_->current_values = values;
  this->state_->remote_values = values;
  this->state_->publish_state();
}

void BrmeshLight::apply_report(const BrmeshLightReport &report) {
  if (this->state_ == nullptr)
    return;

  auto values = this->state_->remote_values;
  const BrmeshLightReport *effective = &report;
  bool on = report.state;
  uint8_t brightness = report.brightness;
  if (!this->addresses_.empty()) {
    // Grouped entity. Every member received the same command, so only reports that arrived after the
    // last write are evidence about the current state; older ones describe the previous command and
    // must not outvote fresh ones (that is what made Home Assistant flap between values).
    const uint32_t now = millis();
    auto it = std::find_if(this->members_.begin(), this->members_.end(),
                           [&report](const MemberState &member) { return member.address == report.address; });
    if (it == this->members_.end()) {
      this->members_.push_back({report.address, report, now});
    } else {
      it->report = report;
      it->received_ms = now;
    }
    on = false;
    brightness = 0;
    const MemberState *latest_on = nullptr;
    size_t fresh = 0;
    for (const auto &member : this->members_) {
      if (this->last_write_ms_ != 0 && static_cast<int32_t>(member.received_ms - this->last_write_ms_) < 0)
        continue;
      fresh++;
      if (!member.report.state)
        continue;
      on = true;
      if (latest_on == nullptr || static_cast<int32_t>(member.received_ms - latest_on->received_ms) >= 0)
        latest_on = &member;
    }
    if (fresh == 0)
      return;  // nothing newer than our own request: keep the optimistic state
    if (latest_on != nullptr) {
      effective = &latest_on->report;
      brightness = latest_on->report.brightness;
    }
  }

  values.set_state(on ? 1.0f : 0.0f);
  if (on && brightness > 0)
    values.set_brightness(brightness / 127.0f);
  if (on && effective->has_rgb) {
    values.set_color_mode(light::ColorMode::RGB);
    values.set_red(effective->red / 255.0f);
    values.set_green(effective->green / 255.0f);
    values.set_blue(effective->blue / 255.0f);
    values.set_color_brightness(1.0f);
  }
  if (on && effective->has_color_temperature) {
    values.set_color_mode(light::ColorMode::COLOR_TEMPERATURE);
    const uint16_t total = effective->cold + effective->warm;
    if (total > 0) {
      const float warm_ratio = effective->warm / static_cast<float>(total);
      values.set_color_temperature(153.0f + ((500.0f - 153.0f) * warm_ratio));
    }
  }
  // A reply without colour information (level only) keeps whatever colour mode the entity already has;
  // BRIGHTNESS is not a mode Home Assistant accepts for this light.
  this->publish_values_(values);
}

void BrmeshLight::apply_group_on_off(bool state) {
  // A button action is the newest intent for this group; forget any older native write.
  this->write_pending_ = false;
  this->has_last_write_ = false;
  if (this->state_ == nullptr)
    return;

  auto values = this->state_->remote_values;
  values.set_state(state);
  if (state && values.get_brightness() <= 0.0f)
    values.set_brightness(1.0f);
  this->publish_values_(values);
}

}  // namespace brmesh
}  // namespace esphome
