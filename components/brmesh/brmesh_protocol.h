#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace esphome {
namespace brmesh {

class BrmeshProtocol {
 public:
  struct DecodedFrame {
    bool valid{false};
    bool legacy{false};      // true when the frame came from the whitened RF format or a raw 16-byte body
    bool crc_ok{true};       // legacy RF CRC matched (false for mesh relays, which re-sign the frame)
    bool relayed{false};     // legacy RF CRC matches the pattern bulbs use when they re-broadcast a frame
    uint8_t command_type{0};
    uint8_t high{0};
    uint8_t sequence{0};
    uint8_t forward{0};
    bool encrypted{false};
    std::vector<uint8_t> payload{};
    std::string reason{};
  };

  struct AddressBatch {
    uint8_t high{0};
    std::vector<std::vector<uint16_t>> windows{};
  };

  // Mirrors what libbroadlink_ble.so hands to OnPassThroughCallback / OnDevStateCallback for a
  // command-type-4 upload frame: (short address, sub type, data).
  // Command type 1: a bulb in pairing mode answering the discovery scan. Body is NOT encrypted and the
  // native parser does not verify its checksum.
  struct Discovery {
    bool valid{false};
    std::array<uint8_t, 6> did{};
    uint16_t device_type{0};
    std::array<uint8_t, 4> key{};
    uint8_t high{0};
  };

  // Command type 5 frame with payload[0] & 0x0F == 4: "short_add(%d), group_add(%d), version(%s)".
  struct Heartbeat {
    bool valid{false};
    uint16_t address{0};
    uint8_t group{0};
  };

  struct DecodedState {
    bool valid{false};
    uint16_t address{0};
    uint8_t state_type{0};          // low nibble of body[2]; 0 = device state upload
    std::vector<uint8_t> payload{};  // body[3 .. 3 + length)
    std::string reason{};
  };

  static constexpr std::array<uint8_t, 4> DEFAULT_KEY{{0x5E, 0x36, 0x7B, 0xC4}};
  static constexpr std::array<uint8_t, 3> FASTCON_ADDRESS{{0xC1, 0xC2, 0xC3}};
  static constexpr uint8_t COMMAND_TYPE_DISCOVERY = 1;
  static constexpr uint8_t COMMAND_TYPE_UPLOAD = 4;
  static constexpr uint8_t COMMAND_TYPE_CONTROL = 5;

  static std::vector<uint8_t> build_light_payload(uint16_t address, bool state, uint8_t level, uint8_t red,
                                                  uint8_t green, uint8_t blue, bool has_rgb,
                                                  bool has_color_temperature, uint8_t cold, uint8_t warm);
  static std::vector<uint8_t> build_group_light_command(bool state, uint8_t level, uint8_t red, uint8_t green,
                                                        uint8_t blue, bool has_rgb, bool has_color_temperature,
                                                        uint8_t cold, uint8_t warm);
  static std::vector<uint8_t> parse_hex(const std::string &hex);
  static std::vector<uint8_t> package_fastcon_body(const std::vector<uint8_t> &data, uint8_t high, uint8_t sequence,
                                                   uint8_t command_type, const uint8_t *key, bool encrypt_body,
                                                   bool force_new_protocol = false);
  static std::vector<uint8_t> build_group_control_payload(uint16_t type, uint8_t group_id,
                                                          const std::vector<uint8_t> &command);
  // BLEUtil.package_scene_control / package_groupId_scene_control: the bulb-side "mode" animations.
  // scene bytes: bit4 cold, bit3 warm, bit2 green, bit1 red, bit0 blue; first byte |= 0x80 for flash
  // (jump) instead of fade and |= 0x40 for the app's grayscale flag (always set by the Mode screen).
  static std::vector<uint8_t> build_scene_control_payload(uint8_t address_low, uint8_t jiffies,
                                                          const std::vector<uint8_t> &scene);
  static std::vector<uint8_t> build_group_scene_control_payload(uint8_t group_id, uint8_t jiffies,
                                                                const std::vector<uint8_t> &scene);
  static constexpr size_t MAX_SCENE_STEPS = 9;
  static std::vector<uint8_t> build_temp_group_payload(uint8_t temp_group_id, const std::vector<uint16_t> &addresses,
                                                       uint16_t token = 0);
  static std::vector<uint8_t> build_query_state_payload(const std::vector<uint16_t> &addresses,
                                                        uint8_t query_selector);
  static std::vector<AddressBatch> build_address_batches(const std::vector<uint16_t> &addresses);
  static std::vector<std::vector<uint8_t>> build_temp_group_payloads(uint8_t temp_group_id,
                                                                     const std::vector<uint16_t> &addresses,
                                                                     uint16_t token = 0);
  static std::vector<std::vector<uint8_t>> build_query_state_payloads(const std::vector<uint16_t> &addresses,
                                                                      uint8_t query_selector);
  static uint32_t temp_group_delay_ms(size_t payload_count, uint32_t interval_ms = 150, uint8_t retry = 3);
  static std::vector<uint8_t> build_new_protocol_advertisement(const std::vector<uint8_t> &body);
  // Builds the whitened legacy advertisement (02 01 04 | 1B FF F0 FF | 24 bytes) from a 16-byte FastCon body.
  static std::vector<uint8_t> build_legacy_advertisement(const std::vector<uint8_t> &body);
  // Builds the body of a command-type-4 upload frame the way libbroadlink_ble.so expects to parse it.
  static std::vector<uint8_t> build_upload_body(uint8_t address_low, uint8_t sub_type,
                                                const std::vector<uint8_t> &data, size_t body_length = 18);
  static std::vector<uint8_t> extract_brmesh_manufacturer(const uint8_t *adv, uint8_t length);
  static DecodedFrame decode_fastcon_frame_for_logging(const std::vector<uint8_t> &manufacturer,
                                                       const uint8_t *mesh_key);
  static DecodedFrame decode_frame(const std::vector<uint8_t> &manufacturer, const uint8_t *mesh_key) {
    return decode_fastcon_frame_for_logging(manufacturer, mesh_key);
  }
  static DecodedState decode_state_callback(const std::vector<uint8_t> &manufacturer, const uint8_t *mesh_key);
  static DecodedState decode_state_from_frame(const DecodedFrame &frame);
  static Discovery decode_discovery(const DecodedFrame &frame);
  static Heartbeat decode_heartbeat(const DecodedFrame &frame);
  // BLEUtil.package_disc_res / package_disc_res2: did[6], addr, 1, mesh key[4] (+ group for the 18-byte form).
  static std::vector<uint8_t> build_discovery_response(const std::array<uint8_t, 6> &did, uint16_t address,
                                                       uint16_t device_type, const uint8_t *mesh_key,
                                                       uint8_t group_id = 0);
  static std::vector<uint8_t> whiten(const uint8_t *data, uint8_t length);
  // Legacy RF frames on air: whitened(header + address + body + crc16) minus the 15-byte preamble.
  static std::vector<uint8_t> get_rf_payload(const uint8_t *data, uint8_t length);
  static const std::array<uint8_t, 6> &legacy_whitened_prefix();
  // Returns false only when the input is malformed; *crc_ok reports whether the RF CRC matched and
  // *relayed whether it matched the constant offset bulbs apply when re-broadcasting (crc ^ RELAY_CRC_XOR).
  static bool dewhiten_legacy_body(const std::vector<uint8_t> &whitened24, std::vector<uint8_t> *body16,
                                   bool *crc_ok = nullptr, bool *relayed = nullptr);
  static constexpr uint16_t RELAY_CRC_XOR = 0x79EF;
  static uint16_t crc16(const uint8_t *addr, const uint8_t *data, uint8_t length);
  static std::string format_hex(const std::vector<uint8_t> &bytes);

 private:
  static uint8_t reverse8(uint8_t value);
  static uint16_t reverse16(uint16_t value);
  static void whitening_init(uint8_t value, uint8_t *ctx);
  static void whitening_encode(const uint8_t *data, uint8_t length, uint8_t *ctx, uint8_t *result);
};

}  // namespace brmesh
}  // namespace esphome
