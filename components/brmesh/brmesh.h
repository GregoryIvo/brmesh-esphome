#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include "esphome/core/preferences.h"
#include "esphome/components/esp32_ble/ble.h"
#include "esphome/components/light/light_output.h"
#include "esphome/components/light/light_effect.h"
#include "esphome/components/light/light_state.h"
#include "brmesh_protocol.h"

namespace esphome {
namespace brmesh {

class BrmeshLight;

struct BrmeshLightReport {
  uint16_t address;
  bool state;
  uint8_t brightness;
  bool has_rgb;
  uint8_t red;
  uint8_t green;
  uint8_t blue;
  bool has_color_temperature;
  uint8_t cold;
  uint8_t warm;
};

class BrmeshHub : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override;

  void set_key_hex(const std::string &key);
  void set_redundancy(uint8_t redundancy) { this->redundancy_ = redundancy; }
  void set_command_interval(uint32_t interval_ms) { this->command_interval_ms_ = interval_ms; }
  // Total continuous advertising time for one control packet. 0 = redundancy * command_interval.
  void set_command_airtime(uint32_t airtime_ms) { this->command_airtime_ms_ = airtime_ms; }
  void set_group_refresh_interval(uint32_t interval_ms) { this->group_refresh_interval_ms_ = interval_ms; }
  void set_query_after_write(bool query_after_write) { this->query_after_write_ = query_after_write; }
  void set_query_redundancy(uint8_t query_redundancy) { this->query_redundancy_ = query_redundancy; }
  void set_state_query_on_boot(bool state_query_on_boot) { this->state_query_on_boot_ = state_query_on_boot; }
  void set_state_poll_interval(uint32_t state_poll_interval_ms) { this->state_poll_interval_ms_ = state_poll_interval_ms; }
  void set_state_timeout(uint32_t state_timeout_ms) { this->state_timeout_ms_ = state_timeout_ms; }
  void set_all_group_refresh_on_boot(bool all_group_refresh_on_boot) {
    this->all_group_refresh_on_boot_ = all_group_refresh_on_boot;
  }
  void set_listen(bool listen) { this->listen_ = listen; }
  void set_recover_ble(bool recover_ble) { this->recover_ble_ = recover_ble; }
  void set_retry_dropped_writes(bool retry) { this->retry_dropped_writes_ = retry; }
  void set_pairing_addresses(const std::vector<uint16_t> &addresses) { this->pairing_addresses_ = addresses; }
  void set_pairing_timeout(uint32_t timeout_ms) { this->pairing_timeout_ms_ = timeout_ms; }
  void add_on_device_paired_callback(std::function<void(uint16_t, std::string)> &&callback) {
    this->device_paired_callbacks_.push_back(std::move(callback));
  }
  bool is_pairing() const { return this->pairing_mode_; }
  bool is_transmitting() const { return !this->send_queue_.empty(); }
  bool is_ready() const;
  bool get_query_after_write() const { return this->query_after_write_; }
  uint32_t get_command_airtime_ms() const;

  // owner: opaque tag used to supersede older packets from the same entity (0 = never superseded).
  void send_light_state(uint16_t address, bool state, float brightness, float red, float green, float blue,
                        bool has_rgb, bool has_color_temperature, float color_temperature, uintptr_t owner = 0);
  void send_temp_group_light_state(const std::vector<uint16_t> &addresses, bool state, float brightness, float red,
                                   float green, float blue, bool has_rgb, bool has_color_temperature,
                                   float color_temperature, uintptr_t owner = 0);
  // Bulb-side animation ("mode") for one light or for a temporary group of lights.
  void send_scene_control(uint16_t address, uint8_t jiffies, const std::vector<uint8_t> &scene, uintptr_t owner = 0);
  void send_temp_group_scene_control(const std::vector<uint16_t> &addresses, uint8_t jiffies,
                                     const std::vector<uint8_t> &scene, uintptr_t owner = 0);
  void send_group_command(uint16_t type, uint8_t group_id, const std::vector<uint8_t> &command);
  void send_group_on_off(uint16_t type, uint8_t group_id, bool state);
  void send_temp_group_on_off(const std::vector<uint16_t> &addresses, bool state);
  void register_light(uint16_t address, BrmeshLight *light);
  void register_group_light(const std::vector<uint16_t> &addresses, BrmeshLight *light);
  void publish_report(const BrmeshLightReport &report);
  void query_state(uint16_t address);
  void query_states(const std::vector<uint16_t> &addresses);
  void send_raw_payload(const std::vector<uint8_t> &payload, uint8_t sequence = 0);
  void send_raw_payload_hex(const std::string &payload_hex, uint8_t sequence = 0);
  void send_scene(uint8_t scene);
  void start_pairing();
  void stop_pairing();
  void clear_pairings();
  void enroll_device(uint16_t address, const std::string &did_hex, const std::string &device_key_hex,
                     uint16_t type = 0, uint16_t group_id = 0);
  void handle_manufacturer_data(const std::vector<uint8_t> &manufacturer, int8_t rssi);
#ifdef USE_ESP32
  void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param);
  void gap_scan_event_handler(const esp32_ble::BLEScanResult &scan_result);
#endif

 protected:
  struct StoredDevice {
    uint16_t address;
    uint16_t type;
    uint16_t group_id;
    uint8_t high;
    uint8_t did[6];
    uint8_t key[4];
  };

  struct StoredDevices {
    uint32_t magic;
    uint16_t version;
    uint16_t count;
    StoredDevice devices[32];
  };

  struct RegisteredLight {
    uint16_t address;
    BrmeshLight *light;
  };

  struct RegisteredGroupLight {
    std::vector<uint16_t> addresses;
    uint32_t hash;
    BrmeshLight *light;
  };

  struct QueuedAdvertisement {
    std::vector<uint8_t> raw_advertisement;
    uint32_t airtime_ms{0};
    uint32_t next_send_ms{0};
    uint8_t priority{0};
    uint8_t high{0};
    bool new_protocol{false};
    bool cancelled{false};
    uintptr_t owner{0};
    std::function<void()> on_complete{};
    std::function<void()> on_start{};
  };

  struct PendingStateQuery {
    uint16_t address{0};
    uint32_t expires_at_ms{0};
  };

  struct PairingCandidate {
    std::array<uint8_t, 6> did{};
    uint16_t address{0};
    uint32_t last_response_ms{0};
    uint8_t responses{0};
    bool confirmed{false};
  };

  struct SeenFrame {
    uint8_t command_type{0};
    uint8_t sequence{0};
    uint16_t address{0};
    uint32_t payload_hash{0};
    uint32_t seen_at_ms{0};
  };

  static constexpr uint8_t DEFAULT_KEY[4] = {0x5E, 0x36, 0x7B, 0xC4};
  static constexpr uint8_t DEFAULT_FASTCON_ADDRESS[3] = {0xC1, 0xC2, 0xC3};
  static constexpr const char *BEACON_UUID = "87b99b2c-90fd-11e9-bc42-526af7764f64";
  static constexpr uint32_t STORAGE_MAGIC = 0x42524D53UL;  // BRMS
  static constexpr uint16_t STORAGE_VERSION = 1;
  // ScanDeviceActivity: discovery scan every 3 s, discovery response repeated until a heartbeat arrives.
  static constexpr uint32_t PAIRING_SCAN_INTERVAL_MS = 3000;
  static constexpr uint32_t PAIRING_RESPONSE_INTERVAL_MS = 2000;
  static constexpr uint8_t PAIRING_MAX_RESPONSES = 15;
  static constexpr uint8_t TEMP_GROUP_ID = 253;
  static constexpr uint16_t GROUP_CONTROL_TYPE = 43050;
  // BLEFastconHelper.broadcastTempGroupId(253, addrs, 0, 150, 3): each membership window airs 3 x 150 ms.
  static constexpr uint32_t MEMBERSHIP_AIRTIME_MS = 450;
  // BLEFastconHelper.queryDeviceStatusWithAddrs(addrs, 300, ...): one 300 ms advertisement per window.
  static constexpr uint32_t QUERY_AIRTIME_MS = 300;

  std::array<uint8_t, 4> key_{{0xA1, 0xA2, 0xA3, 0xA4}};
  uint8_t redundancy_{5};
  uint32_t command_interval_ms_{300};
  uint32_t command_airtime_ms_{0};
  uint32_t group_refresh_interval_ms_{0};
  bool query_after_write_{false};
  uint8_t query_redundancy_{1};
  bool state_query_on_boot_{false};
  uint32_t state_poll_interval_ms_{0};
  uint32_t state_timeout_ms_{10000};
  bool all_group_refresh_on_boot_{false};
  bool listen_{false};
  bool recover_ble_{true};
  bool retry_dropped_writes_{true};
  uint8_t send_sequence_{1};
  bool ble_ready_{false};
  bool pairing_mode_{false};
  bool tx_power_set_{false};
  bool adv_interval_slow_{false};
#ifdef USE_ESP32
  bool scan_started_{false};
  bool scan_pending_{false};
  bool scan_stop_pending_{false};
  uint32_t scan_retry_at_ms_{0};
#endif
  uint32_t last_pairing_scan_ms_{0};
  uint32_t pairing_started_ms_{0};
  uint32_t pairing_timeout_ms_{300000};
  uint16_t next_pairing_address_{1};
  std::vector<uint16_t> pairing_addresses_;
  std::vector<PairingCandidate> pairing_candidates_;
  std::vector<std::function<void(uint16_t, std::string)>> device_paired_callbacks_;
  ESPPreferenceObject pref_;
  StoredDevices stored_{};
  std::vector<RegisteredLight> lights_;
  std::vector<RegisteredGroupLight> group_lights_;
  std::vector<BrmeshLightReport> last_reports_;
  std::vector<PendingStateQuery> pending_state_queries_;
  std::vector<QueuedAdvertisement> send_queue_;
  std::vector<SeenFrame> seen_frames_;
  uint32_t relayed_frames_{0};
  bool boot_work_scheduled_{false};
  uint32_t last_state_poll_ms_{0};
  // Last confirmed temporary-group membership refresh.
  uint32_t last_refresh_hash_{0};
  uint32_t last_refresh_done_ms_{0};
  bool last_refresh_valid_{false};
  // All stages run on ESPHome's loop via its GAP event dispatcher.
  enum class TxState { IDLE, CONFIGURING, STARTING, ADVERTISING, STOPPING, RECOVERING, BACKOFF };
  TxState tx_state_{TxState::IDLE};
  uint32_t tx_deadline_ms_{0};
  uint8_t tx_failures_{0};
  uint8_t dropped_bursts_{0};
  uint32_t ble_reset_at_ms_{0};
  bool ble_reset_pending_{false};
  bool ble_was_active_{false};
  std::vector<uint8_t> active_advertisement_;
  uint16_t group_token_{0};
  static constexpr uint32_t MIN_ADVERTISEMENT_MS = 100;
  static constexpr uint32_t GAP_TIMEOUT_MS = 2000;
  static constexpr uint8_t MAX_TX_FAILURES = 3;
  static constexpr uint8_t DROPPED_BURSTS_BEFORE_BLE_RESET = 2;
  void fail_advertisement_();
  void finish_advertisement_();
  void drop_queue_(const char *why);
  void request_ble_reset_();
  void handle_ble_inactive_();

  uint8_t next_sequence_(uint8_t sequence);
  void send_inner_payload_(const uint8_t *data, uint8_t length, uint8_t high = 0, uint8_t sequence = 0);
  void send_command_(uint8_t command_type, const uint8_t *data, uint8_t length, const uint8_t *key, bool encrypt_body,
                     uint8_t high = 0, uint8_t sequence = 0, uint32_t airtime_ms = 0);
  void send_command_(uint8_t command_type, const uint8_t *data, uint8_t length, const uint8_t *key, bool encrypt_body,
                     uint8_t high, uint8_t sequence, uint32_t airtime_ms, uint8_t priority, uintptr_t owner,
                     std::function<void()> on_complete = nullptr, bool force_new_protocol = false);
  void enqueue_advertisement_(std::vector<uint8_t> raw_advertisement, uint32_t airtime_ms, uint8_t priority,
                              uint8_t high = 0, bool new_protocol = false, uintptr_t owner = 0,
                              std::function<void()> on_complete = nullptr);
  void cancel_owner_(uintptr_t owner);
  void process_send_queue_();
  void process_state_timeouts_();
  void mark_state_query_pending_(const std::vector<uint16_t> &addresses);
  void clear_pending_state_query_(uint16_t address);
  std::vector<uint16_t> registered_addresses_() const;
  uint32_t hash_addresses_(const std::vector<uint16_t> &addresses) const;
  void publish_group_on_off_(const std::vector<uint16_t> &addresses, bool state);
  bool refresh_all_group_(const std::vector<uint16_t> &addresses, uint8_t priority, uintptr_t owner);
  void schedule_boot_work_();
  bool decoded_state_to_report_(const BrmeshProtocol::DecodedState &state, BrmeshLightReport *report) const;
  void remember_report_(const BrmeshLightReport &report);
  bool is_repeated_frame_(const BrmeshProtocol::DecodedFrame &frame, uint16_t address);
  void notify_transmit_dropped_(bool after_ble_reset = false);
  bool scan_wanted_() const;
  bool state_features_enabled_() const {
    return this->query_after_write_ || this->state_query_on_boot_ || this->state_poll_interval_ms_ > 0;
  }
  std::vector<uint8_t> package_ble_fastcon_body_(const uint8_t *data, uint8_t length, uint8_t high,
                                                  uint8_t sequence, uint8_t command_type, const uint8_t *key,
                                                  bool encrypt_body, bool force_new_protocol = false);
  void send_pairing_scan_request_();
  void handle_discovery_(const BrmeshProtocol::Discovery &discovery, int8_t rssi);
  void handle_heartbeat_(const BrmeshProtocol::Heartbeat &heartbeat);
  void confirm_pairing_(uint16_t address, const char *how);
  bool allocate_pairing_address_(uint16_t *address) const;
  bool address_bound_(uint16_t address) const;
  StoredDevice *find_pairing_by_did_(const std::array<uint8_t, 6> &did);
  static std::string did_hex_(const uint8_t *did);
  void send_discovery_response_(const StoredDevice &device);
  void package_discovery_response_(const StoredDevice &device, std::vector<uint8_t> *payload);
#ifdef USE_ESP32
  void start_scan_();
  void stop_scan_();
  void apply_tx_power_();
#endif
  void load_pairings_();
  void save_pairings_();
  bool upsert_pairing_(const StoredDevice &device);
  StoredDevice *find_pairing_(uint16_t address);
  void log_pairing_candidate_(const uint8_t *adv, uint8_t length, int8_t rssi);
  static bool has_brmesh_manufacturer_(const uint8_t *adv, uint8_t length);
  void advertise_(const std::vector<uint8_t> &raw_advertisement);

  static uint8_t scale_brightness_(float value);
  static uint8_t scale_color_(float value);
  static std::vector<uint8_t> parse_hex_(const std::string &hex);
};

class BrmeshDevicePairedTrigger : public Trigger<uint16_t, std::string> {
 public:
  explicit BrmeshDevicePairedTrigger(BrmeshHub *parent) {
    parent->add_on_device_paired_callback([this](uint16_t address, std::string did) { this->trigger(address, did); });
  }
};

template<typename... Ts> class BrmeshStartPairingAction : public Action<Ts...> {
 public:
  explicit BrmeshStartPairingAction(BrmeshHub *parent) : parent_(parent) {}
  void play(Ts... x) override { this->parent_->start_pairing(); }

 protected:
  BrmeshHub *parent_;
};

template<typename... Ts> class BrmeshStopPairingAction : public Action<Ts...> {
 public:
  explicit BrmeshStopPairingAction(BrmeshHub *parent) : parent_(parent) {}
  void play(Ts... x) override { this->parent_->stop_pairing(); }

 protected:
  BrmeshHub *parent_;
};

template<typename... Ts> class BrmeshClearPairingsAction : public Action<Ts...> {
 public:
  explicit BrmeshClearPairingsAction(BrmeshHub *parent) : parent_(parent) {}
  void play(Ts... x) override { this->parent_->clear_pairings(); }

 protected:
  BrmeshHub *parent_;
};

template<typename... Ts> class BrmeshEnrollDeviceAction : public Action<Ts...> {
 public:
  explicit BrmeshEnrollDeviceAction(BrmeshHub *parent) : parent_(parent) {}

  TEMPLATABLE_VALUE(uint16_t, address)
  TEMPLATABLE_VALUE(std::string, did)
  TEMPLATABLE_VALUE(std::string, device_key)
  TEMPLATABLE_VALUE(uint16_t, type)
  TEMPLATABLE_VALUE(uint16_t, group_id)

  void play(Ts... x) override {
    this->parent_->enroll_device(this->address_.value(x...), this->did_.value(x...), this->device_key_.value(x...),
                                 this->type_.value(x...), this->group_id_.value(x...));
  }

 protected:
  BrmeshHub *parent_;
};

class BrmeshLight : public light::LightOutput, public Component {
 public:
  void set_parent(BrmeshHub *parent) { this->parent_ = parent; }
  void set_address(uint16_t address) { this->address_ = address; }
  void set_addresses(const std::vector<uint16_t> &addresses) { this->addresses_ = addresses; }
  void set_supports_cold_warm(bool supports) { this->supports_cold_warm_ = supports; }

  light::LightTraits get_traits() override;
  void setup_state(light::LightState *state) override;
  void write_state(light::LightState *state) override;
  void loop() override;
  void apply_report(const BrmeshLightReport &report);
  void apply_group_on_off(bool state);
  // Called by the hub when a burst was dropped after repeated BLE failures.
  void on_transmit_dropped(bool after_ble_reset = false);
  bool has_pending_write() const { return this->write_pending_; }
  // Called by BrmeshModeEffect: run a bulb-side animation until stop_mode() or the next normal write.
  void start_mode(const std::vector<uint8_t> &scene, uint8_t jiffies);
  void stop_mode();
  bool is_mode_active() const { return this->mode_active_; }

 protected:
  struct MemberState {
    uint16_t address;
    BrmeshLightReport report;
    uint32_t received_ms;
  };

  void dispatch_(const light::LightColorValues &values);
  void dispatch_mode_();
  bool mode_active_{false};
  std::vector<uint8_t> mode_scene_;
  uint8_t mode_jiffies_{1};
  void publish_values_(const light::LightColorValues &values);

  BrmeshHub *parent_{nullptr};
  uint16_t address_{0};
  std::vector<uint16_t> addresses_{};
  bool supports_cold_warm_{true};
  light::LightState *state_{nullptr};
  bool write_pending_{false};
  bool has_last_write_{false};
  uint8_t retry_budget_{0};
  uint32_t last_dispatch_ms_{0};
  uint32_t last_write_ms_{0};
  bool values_differ_(const light::LightColorValues &a, const light::LightColorValues &b) const;
  light::LightColorValues pending_values_;
  light::LightColorValues last_values_;
  std::vector<MemberState> members_;
  static constexpr uint32_t MIN_DISPATCH_INTERVAL_MS = 250;
};

// One entry in the light's effect list. Selecting it in Home Assistant sends the mode packet once; the bulbs
// animate on their own. Selecting "None" (or any normal light change) sends the ordinary state back.
class BrmeshModeEffect : public light::LightEffect {
 public:
  explicit BrmeshModeEffect(const char *name) : light::LightEffect(name) {}
  void set_scene(const std::vector<uint8_t> &scene) { this->scene_ = scene; }
  void set_jiffies(uint8_t jiffies) { this->jiffies_ = jiffies; }
  void start() override {
    auto *output = static_cast<BrmeshLight *>(this->state_->get_output());
    if (output != nullptr)
      output->start_mode(this->scene_, this->jiffies_);
  }
  void stop() override {
    auto *output = static_cast<BrmeshLight *>(this->state_->get_output());
    if (output != nullptr)
      output->stop_mode();
  }
  void apply() override {}

 protected:
  std::vector<uint8_t> scene_;
  uint8_t jiffies_{1};
};

template<typename... Ts> class BrmeshSendRawAction : public Action<Ts...> {
 public:
  explicit BrmeshSendRawAction(BrmeshHub *parent) : parent_(parent) {}

  TEMPLATABLE_VALUE(std::string, payload_hex)
  TEMPLATABLE_VALUE(uint8_t, sequence)

  void play(Ts... x) override {
    this->parent_->send_raw_payload_hex(this->payload_hex_.value(x...), this->sequence_.value(x...));
  }

 protected:
  BrmeshHub *parent_;
};

template<typename... Ts> class BrmeshSendSceneAction : public Action<Ts...> {
 public:
  explicit BrmeshSendSceneAction(BrmeshHub *parent) : parent_(parent) {}

  TEMPLATABLE_VALUE(uint8_t, scene)

  void play(Ts... x) override { this->parent_->send_scene(this->scene_.value(x...)); }

 protected:
  BrmeshHub *parent_;
};

template<typename... Ts> class BrmeshGroupOnOffAction : public Action<Ts...> {
 public:
  explicit BrmeshGroupOnOffAction(BrmeshHub *parent) : parent_(parent) {}

  TEMPLATABLE_VALUE(uint16_t, type)
  TEMPLATABLE_VALUE(uint8_t, group_id)
  TEMPLATABLE_VALUE(bool, state)

  void play(Ts... x) override {
    this->parent_->send_group_on_off(this->type_.value(x...), this->group_id_.value(x...), this->state_.value(x...));
  }

 protected:
  BrmeshHub *parent_;
};

template<typename... Ts> class BrmeshTempGroupOnOffAction : public Action<Ts...> {
 public:
  explicit BrmeshTempGroupOnOffAction(BrmeshHub *parent) : parent_(parent) {}

  void set_addresses(const std::vector<uint16_t> &addresses) { this->addresses_ = addresses; }

  TEMPLATABLE_VALUE(bool, state)

  void play(Ts... x) override { this->parent_->send_temp_group_on_off(this->addresses_, this->state_.value(x...)); }

 protected:
  BrmeshHub *parent_;
  std::vector<uint16_t> addresses_;
};

}  // namespace brmesh
}  // namespace esphome
