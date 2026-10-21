#include "brmesh_protocol.h"

#include <algorithm>
#include <cstring>
#include <cstdio>

namespace esphome {
namespace brmesh {

std::vector<uint8_t> BrmeshProtocol::build_light_payload(uint16_t address, bool state, uint8_t level, uint8_t red,
                                                         uint8_t green, uint8_t blue, bool has_rgb,
                                                         bool has_color_temperature, uint8_t cold, uint8_t warm) {
  const uint8_t addr_low = address & 0xFF;
  const uint8_t output_level = state ? level : 0;

  if (has_rgb && state)
    return {0x72, addr_low, output_level, blue, red, green};

  if (has_color_temperature && state)
    return {0x82, addr_low, output_level, 0x00, cold, warm};

  return {0x22, addr_low, output_level, 0x00, 0x00, 0x00};
}

std::vector<uint8_t> BrmeshProtocol::build_group_light_command(bool state, uint8_t level, uint8_t red, uint8_t green,
                                                               uint8_t blue, bool has_rgb,
                                                               bool has_color_temperature, uint8_t cold,
                                                               uint8_t warm) {
  const uint8_t power_level = state ? static_cast<uint8_t>(0x80 | (level & 0x7F)) : 0x00;

  if (has_rgb && state)
    return {power_level, blue, red, green, 0x00, 0x00};

  if (has_color_temperature && state)
    return {power_level, 0x00, 0x00, 0x00, warm, cold};

  const uint8_t brightness_level = state ? static_cast<uint8_t>(level & 0x7F) : 0x00;
  return {brightness_level};
}

std::vector<uint8_t> BrmeshProtocol::parse_hex(const std::string &hex) {
  std::vector<uint8_t> out;
  uint8_t high = 0;
  bool have_high = false;
  for (char ch : hex) {
    if (ch == ' ' || ch == ':' || ch == '-')
      continue;
    uint8_t nibble;
    if (ch >= '0' && ch <= '9')
      nibble = ch - '0';
    else if (ch >= 'a' && ch <= 'f')
      nibble = ch - 'a' + 10;
    else if (ch >= 'A' && ch <= 'F')
      nibble = ch - 'A' + 10;
    else
      return {};
    if (!have_high) {
      high = nibble;
      have_high = true;
    } else {
      out.push_back((high << 4) | nibble);
      have_high = false;
    }
  }
  if (have_high)
    return {};
  return out;
}

std::vector<uint8_t> BrmeshProtocol::package_fastcon_body(const std::vector<uint8_t> &data, uint8_t high,
                                                          uint8_t sequence, uint8_t command_type, const uint8_t *key,
                                                          bool encrypt_body, bool force_new_protocol) {
  const uint8_t body_length = force_new_protocol || data.size() > 12 ? 18 : 12;
  std::vector<uint8_t> payload(body_length + 4, 0);
  const uint8_t forward = command_type == 5 ? 1 : 0;
  payload[0] = (high & 0x0F) | ((command_type & 0x07) << 4) | ((forward & 0x01) << 7);
  payload[1] = sequence;
  payload[2] = encrypt_body && key != nullptr ? key[3] : 0xFF;
  payload[3] = 0;
  for (size_t i = 0; i < data.size(); i++)
    payload[4 + i] = data[i];

  uint8_t checksum = 0;
  for (size_t i = 0; i < payload.size(); i++) {
    if (i == 3)
      continue;
    checksum = (checksum + payload[i]) & 0xFF;
  }
  payload[3] = checksum;

  for (uint8_t i = 0; i < 4; i++)
    payload[i] = DEFAULT_KEY[i] ^ payload[i];
  // package_ble_fastcon_body substitutes DEFAULT_KEY when the caller passes a null key, so the body is
  // always XORed; "unencrypted" frames (discovery scan) still carry 0xFF in the header key byte.
  const uint8_t *body_key = (encrypt_body && key != nullptr) ? key : DEFAULT_KEY.data();
  for (uint8_t i = 0; i < body_length; i++)
    payload[4 + i] = body_key[i & 3] ^ payload[4 + i];
  return payload;
}

std::vector<uint8_t> BrmeshProtocol::build_group_control_payload(uint16_t type, uint8_t group_id,
                                                                 const std::vector<uint8_t> &command) {
  std::vector<uint8_t> out{static_cast<uint8_t>(command.size() > 1 ? 0x93 : 0x43),
                           static_cast<uint8_t>(type & 0xFF), static_cast<uint8_t>((type >> 8) & 0xFF),
                           group_id};
  for (uint8_t i = 0; i < 8; i++)
    out.push_back(i < command.size() ? command[i] : 0x00);
  return out;
}

static std::vector<uint8_t> build_scene_payload(uint8_t family, uint8_t target, uint8_t jiffies,
                                                const std::vector<uint8_t> &scene) {
  const size_t steps = std::min(scene.size(), BrmeshProtocol::MAX_SCENE_STEPS);
  std::vector<uint8_t> out(12, 0);
  out[0] = static_cast<uint8_t>(family + (steps << 4));
  out[1] = target;
  out[2] = jiffies;
  for (size_t i = 0; i < steps; i++)
    out[3 + i] = scene[i];
  return out;
}

std::vector<uint8_t> BrmeshProtocol::build_scene_control_payload(uint8_t address_low, uint8_t jiffies,
                                                                 const std::vector<uint8_t> &scene) {
  return build_scene_payload(0x28, address_low, jiffies, scene);
}

std::vector<uint8_t> BrmeshProtocol::build_group_scene_control_payload(uint8_t group_id, uint8_t jiffies,
                                                                       const std::vector<uint8_t> &scene) {
  return build_scene_payload(0x2C, group_id, jiffies, scene);
}

std::vector<uint8_t> BrmeshProtocol::build_temp_group_payload(uint8_t temp_group_id,
                                                              const std::vector<uint16_t> &addresses,
                                                              uint16_t token) {
  std::vector<uint8_t> out(18, 0);
  out[0] = 0x45;
  out[1] = temp_group_id;
  // BLEFastconHelper.broadcastTempGroupId(...) passes a short token to
  // BLEUtil.package_batch_temp_group(...); s == 0 is replaced with currentTimeMillis().
  out[3] = token & 0xFF;
  out[4] = (token >> 8) & 0xFF;
  if (addresses.empty())
    return out;

  const uint8_t base = addresses.front() & 0xFF;
  out[2] = base;

  for (uint16_t address : addresses) {
    const uint8_t addr_low = address & 0xFF;
    if (addr_low == 0)
      continue;
    const uint8_t offset = static_cast<uint8_t>(addr_low - base);
    const uint8_t byte_index = offset / 8;
    if (byte_index >= 13)
      continue;
    out[5 + byte_index] |= 1 << (offset & 0x07);
  }
  return out;
}

std::vector<uint8_t> BrmeshProtocol::build_query_state_payload(const std::vector<uint16_t> &addresses,
                                                               uint8_t query_selector) {
  std::vector<uint8_t> out(18, 0);
  out[0] = 0x00;
  out[1] = 0x40;
  out[2] = query_selector;

  if (addresses.empty()) {
    out[5] = 0x01;
    return out;
  }

  std::vector<uint16_t> sorted = addresses;
  std::sort(sorted.begin(), sorted.end());
  const uint8_t base = sorted.front() & 0xFF;
  out[4] = base;

  for (uint16_t address : sorted) {
    const uint8_t addr_low = address & 0xFF;
    if (addr_low == 0)
      continue;
    const uint8_t offset = static_cast<uint8_t>(addr_low - base);
    const uint8_t byte_index = offset / 8;
    if (byte_index >= 13)
      continue;
    out[5 + byte_index] |= 1 << (offset & 0x07);
  }

  return out;
}

std::vector<BrmeshProtocol::AddressBatch> BrmeshProtocol::build_address_batches(const std::vector<uint16_t> &addresses) {
  std::vector<AddressBatch> batches;
  if (addresses.empty())
    return batches;

  std::vector<uint16_t> sorted = addresses;
  std::sort(sorted.begin(), sorted.end());

  for (size_t index = 0; index < sorted.size();) {
    const uint8_t high = sorted[index] >> 8;
    std::vector<uint16_t> lows;
    while (index < sorted.size() && (sorted[index] >> 8) == high) {
      lows.push_back(sorted[index] & 0xFF);
      index++;
    }
    if (lows.empty())
      continue;

    AddressBatch batch;
    batch.high = high;
    batch.windows.resize(3);
    const uint16_t base = lows.front();
    for (uint16_t low : lows) {
      const uint16_t offset = low - base;
      const uint8_t window = offset / 104;
      if (window < batch.windows.size())
        batch.windows[window].push_back(low);
    }
    batches.push_back(batch);
  }
  return batches;
}

std::vector<std::vector<uint8_t>> BrmeshProtocol::build_temp_group_payloads(uint8_t temp_group_id,
                                                                            const std::vector<uint16_t> &addresses,
                                                                            uint16_t token) {
  std::vector<std::vector<uint8_t>> payloads;
  // BLEFastconHelper.broadcastTempGroupId(...) delegates to calcHighArrays(...) and
  // broadcastTempGroupIdSingle(...): sorted high-byte groups, then 104-address low-byte windows.
  for (const auto &batch : build_address_batches(addresses)) {
    for (const auto &window : batch.windows) {
      if (!window.empty())
        payloads.push_back(build_temp_group_payload(temp_group_id, window, token));
    }
  }
  return payloads;
}

std::vector<std::vector<uint8_t>> BrmeshProtocol::build_query_state_payloads(const std::vector<uint16_t> &addresses,
                                                                             uint8_t query_selector) {
  std::vector<std::vector<uint8_t>> payloads;
  // Mirrors BLEFastconHelper.queryDeviceStatusWithAddrs(...), including the same 104-address windows.
  for (const auto &batch : build_address_batches(addresses)) {
    for (const auto &window : batch.windows) {
      if (!window.empty())
        payloads.push_back(build_query_state_payload(window, query_selector));
    }
  }
  return payloads;
}

uint32_t BrmeshProtocol::temp_group_delay_ms(size_t payload_count, uint32_t interval_ms, uint8_t retry) {
  // BLEFastconHelper.broadcastTempGroupId(...) returns interval * retry * payload_count after sendBatchCommand(...).
  return static_cast<uint32_t>(payload_count) * interval_ms * retry;
}

std::vector<uint8_t> BrmeshProtocol::build_new_protocol_advertisement(const std::vector<uint8_t> &body) {
  std::vector<uint8_t> out{0x02, 0x01, 0x04, 0x1B, 0xFF, 0xF0, 0xFF, 0xA5, 0x5A};
  out.insert(out.end(), body.begin(), body.end());
  return out;
}

std::vector<uint8_t> BrmeshProtocol::extract_brmesh_manufacturer(const uint8_t *adv, uint8_t length) {
  uint8_t index = 0;
  while (index < length) {
    const uint8_t field_len = adv[index];
    if (field_len == 0)
      break;
    if (index + field_len >= length + 1)
      break;

    const uint8_t type = adv[index + 1];
    if (type == 0xFF && field_len >= 3 && adv[index + 2] == 0xF0 && adv[index + 3] == 0xFF)
      return std::vector<uint8_t>(adv + index + 2, adv + index + 1 + field_len);

    index += field_len + 1;
  }
  return {};
}

BrmeshProtocol::DecodedFrame BrmeshProtocol::decode_fastcon_frame_for_logging(const std::vector<uint8_t> &manufacturer,
                                                                              const uint8_t *mesh_key) {
  DecodedFrame frame{};
  if (manufacturer.size() < 8) {
    frame.reason = "manufacturer payload too short";
    return frame;
  }
  if (manufacturer[0] != 0xF0 || manufacturer[1] != 0xFF) {
    frame.reason = "manufacturer id mismatch";
    return frame;
  }
  if (mesh_key == nullptr) {
    frame.reason = "mesh key unavailable";
    return frame;
  }

  // Three shapes exist on air (BLEFastconHelper.doDealWithScanResult):
  //   f0ff a55a <22 bytes>      new protocol, plain FastCon body
  //   f0ff <16 bytes>           legacy protocol, plain FastCon body
  //   f0ff 6db64368931d <18>    legacy RF frame exactly as phones/gateways transmit it: whitened, must be
  //                             de-whitened (BLEUtil.whiten_data) before it is a FastCon body.
  std::vector<uint8_t> body;
  if (manufacturer.size() >= 26 && manufacturer[2] == 0xA5 && manufacturer[3] == 0x5A) {
    body.assign(manufacturer.begin() + 4, manufacturer.begin() + 26);
  } else {
    std::vector<uint8_t> raw(manufacturer.begin() + 2, manufacturer.end());
    if (raw.size() == 24 && std::equal(legacy_whitened_prefix().begin(), legacy_whitened_prefix().end(), raw.begin())) {
      // Bulbs re-broadcast legacy frames with the body intact but a re-signed RF CRC, so the CRC is
      // advisory here; the FastCon checksum below is the integrity gate.
      if (!dewhiten_legacy_body(raw, &body, &frame.crc_ok, &frame.relayed)) {
        frame.reason = "legacy rf frame malformed";
        return frame;
      }
      frame.legacy = true;
    } else if (raw.size() == 16) {
      body = raw;
      frame.legacy = true;
    } else if (raw.size() == 24) {
      frame.reason = "legacy 24 byte body not supported";
      return frame;
    } else if (raw.size() == 22) {
      body = raw;
    } else {
      frame.reason = "unsupported fastcon body length";
      return frame;
    }
  }

  for (uint8_t i = 0; i < 4; i++)
    body[i] = DEFAULT_KEY[i] ^ body[i];

  frame.command_type = (body[0] >> 4) & 0x07;
  frame.high = body[0] & 0x0F;
  frame.forward = body[0] >> 7;
  frame.sequence = body[1];
  frame.encrypted = body[2] != 0xFF;
  const size_t body_length = body.size() - 4;
  frame.payload.assign(body.begin() + 4, body.end());
  if (frame.command_type == COMMAND_TYPE_DISCOVERY) {
    // parse_ble_broadcast(_new) builds the BLEDeviceInfo straight from the plain body: no decryption and
    // no checksum verification for discovery frames.
    frame.valid = true;
    frame.reason = "discovery";
    return frame;
  }
  if (frame.encrypted) {
    for (size_t i = 0; i < body_length; i++)
      frame.payload[i] = mesh_key[i & 3] ^ frame.payload[i];
  } else {
    for (size_t i = 0; i < body_length; i++)
      frame.payload[i] = DEFAULT_KEY[i & 3] ^ frame.payload[i];
  }
  uint8_t checksum = 0;
  for (size_t i = 0; i < 4; i++) {
    if (i != 3)
      checksum = (checksum + body[i]) & 0xFF;
  }
  for (uint8_t byte : frame.payload)
    checksum = (checksum + byte) & 0xFF;
  if (checksum != body[3]) {
    frame.reason = "fastcon checksum mismatch";
    return frame;
  }
  frame.valid = true;
  frame.reason = "decoded";
  return frame;
}

BrmeshProtocol::DecodedState BrmeshProtocol::decode_state_from_frame(const DecodedFrame &frame) {
  // Java_cn_com_broadlink_blelight_jni_BLEUtil_parse_ble_broadcast_new, command type 4 branch:
  //   addr    = body[0] | (header.high << 8)
  //   length  = body[2] >> 4, or (frame length - 7) when that nibble is zero
  //   subtype = body[2] & 0x0f            (0 => OnDevStateCallback "device state upload")
  //   data    = body[3 .. 3 + length)
  DecodedState state{};
  if (!frame.valid) {
    state.reason = frame.reason;
    return state;
  }
  if (frame.command_type != COMMAND_TYPE_UPLOAD) {
    state.reason = "not an upload frame";
    return state;
  }
  if (frame.payload.size() < 3) {
    state.reason = "upload payload too short";
    return state;
  }
  const uint8_t length_and_type = frame.payload[2];
  size_t length = length_and_type >> 4;
  if (length == 0)
    length = frame.payload.size() + 4 - 7;
  if (length + 3 > frame.payload.size())
    length = frame.payload.size() - 3;
  state.state_type = length_and_type & 0x0F;
  state.address = (static_cast<uint16_t>(frame.high) << 8) | frame.payload[0];
  state.payload.assign(frame.payload.begin() + 3, frame.payload.begin() + 3 + length);
  state.valid = state.address != 0;
  state.reason = state.valid ? "decoded" : "zero address";
  return state;
}

BrmeshProtocol::Discovery BrmeshProtocol::decode_discovery(const DecodedFrame &frame) {
  Discovery out{};
  if (!frame.valid || frame.command_type != COMMAND_TYPE_DISCOVERY || frame.payload.size() < 12)
    return out;
  for (size_t i = 0; i < 6; i++)
    out.did[i] = frame.payload[i];
  out.device_type = frame.payload[6] | (static_cast<uint16_t>(frame.payload[7]) << 8);
  for (size_t i = 0; i < 4; i++)
    out.key[i] = frame.payload[8 + i];
  out.high = frame.high;
  bool any = false;
  for (uint8_t b : out.did)
    any = any || b != 0;
  out.valid = any;
  return out;
}

BrmeshProtocol::Heartbeat BrmeshProtocol::decode_heartbeat(const DecodedFrame &frame) {
  Heartbeat out{};
  if (!frame.valid || frame.command_type != COMMAND_TYPE_CONTROL || frame.payload.size() < 3)
    return out;
  if ((frame.payload[0] & 0x0F) != 4)
    return out;
  out.address = frame.payload[1] | (static_cast<uint16_t>(frame.high) << 8);
  out.group = frame.payload[2];
  out.valid = out.address != 0;
  return out;
}

std::vector<uint8_t> BrmeshProtocol::build_discovery_response(const std::array<uint8_t, 6> &did, uint16_t address,
                                                              uint16_t device_type, const uint8_t *mesh_key,
                                                              uint8_t group_id) {
  // BLEFastconHelper.sendDiscRes: the 18-byte form is used for addr > 256, type 43756 or high > 2.
  const bool extended = address > 256 || device_type == 43756 || (address >> 8) > 2;
  std::vector<uint8_t> out(extended ? 18 : 12, 0);
  for (size_t i = 0; i < 6; i++)
    out[i] = did[i];
  out[6] = address & 0xFF;
  out[7] = 1;
  for (size_t i = 0; i < 4; i++)
    out[8 + i] = mesh_key[i];
  if (extended)
    out[12] = group_id;
  return out;
}

BrmeshProtocol::DecodedState BrmeshProtocol::decode_state_callback(const std::vector<uint8_t> &manufacturer,
                                                                   const uint8_t *mesh_key) {
  return decode_state_from_frame(decode_fastcon_frame_for_logging(manufacturer, mesh_key));
}

std::vector<uint8_t> BrmeshProtocol::build_upload_body(uint8_t address_low, uint8_t sub_type,
                                                       const std::vector<uint8_t> &data, size_t body_length) {
  std::vector<uint8_t> out(body_length, 0);
  if (body_length < 3)
    return out;
  out[0] = address_low;
  out[1] = 0;
  const size_t length = std::min(data.size(), std::min<size_t>(15, body_length - 3));
  out[2] = static_cast<uint8_t>((length << 4) | (sub_type & 0x0F));
  for (size_t i = 0; i < length; i++)
    out[3 + i] = data[i];
  return out;
}

std::vector<uint8_t> BrmeshProtocol::get_rf_payload(const uint8_t *data, uint8_t length) {
  const uint8_t data_offset = 0x12;
  const uint8_t inverse_offset = 0x0F;
  const uint8_t result_size = data_offset + 3 + length + 2;
  std::vector<uint8_t> result(result_size, 0);

  result[0x0F] = 0x71;
  result[0x10] = 0x0F;
  result[0x11] = 0x55;

  for (uint8_t i = 0; i < 3; i++)
    result[data_offset + 3 - i - 1] = FASTCON_ADDRESS[i];
  for (uint8_t i = 0; i < length; i++)
    result[data_offset + 3 + i] = data[i];
  for (uint8_t i = inverse_offset; i < inverse_offset + 6; i++)
    result[i] = reverse8(result[i]);

  const uint16_t crc = crc16(FASTCON_ADDRESS.data(), data, length);
  result[result_size - 2] = crc & 0xFF;
  result[result_size - 1] = (crc >> 8) & 0xFF;
  return result;
}

std::vector<uint8_t> BrmeshProtocol::build_legacy_advertisement(const std::vector<uint8_t> &body) {
  auto rf_payload = get_rf_payload(body.data(), body.size());
  auto whitened = whiten(rf_payload.data(), rf_payload.size());
  std::vector<uint8_t> out{0x02, 0x01, 0x04, static_cast<uint8_t>(whitened.size() - 12), 0xFF, 0xF0, 0xFF};
  out.insert(out.end(), whitened.begin() + 15, whitened.end());
  return out;
}

const std::array<uint8_t, 6> &BrmeshProtocol::legacy_whitened_prefix() {
  // Whitened form of the constant RF header (71 0F 55 + reversed C1 C2 C3, all bit-reversed). Every legacy
  // frame on air starts with these six bytes after the F0 FF manufacturer id.
  static const std::array<uint8_t, 6> prefix = [] {
    const std::vector<uint8_t> empty(16, 0);
    auto rf = get_rf_payload(empty.data(), empty.size());
    auto whitened = whiten(rf.data(), rf.size());
    std::array<uint8_t, 6> out{};
    for (size_t i = 0; i < 6; i++)
      out[i] = whitened[15 + i];
    return out;
  }();
  return prefix;
}

bool BrmeshProtocol::dewhiten_legacy_body(const std::vector<uint8_t> &whitened24, std::vector<uint8_t> *body16,
                                          bool *crc_ok, bool *relayed) {
  if (whitened24.size() != 24 || body16 == nullptr)
    return false;
  // Whitening is an XOR keystream, so a 15-byte zero preamble reproduces the transmitter's LFSR phase.
  std::vector<uint8_t> rf(15, 0);
  rf.insert(rf.end(), whitened24.begin(), whitened24.end());
  auto plain = whiten(rf.data(), rf.size());
  std::vector<uint8_t> body(plain.begin() + 21, plain.begin() + 37);
  const uint16_t crc = crc16(FASTCON_ADDRESS.data(), body.data(), body.size());
  const uint16_t received = static_cast<uint16_t>(plain[37] | (plain[38] << 8));
  if (crc_ok != nullptr)
    *crc_ok = received == crc;
  if (relayed != nullptr)
    *relayed = received == static_cast<uint16_t>(crc ^ RELAY_CRC_XOR);
  *body16 = body;
  return true;
}

std::vector<uint8_t> BrmeshProtocol::whiten(const uint8_t *data, uint8_t length) {
  uint8_t ctx[7];
  whitening_init(0x25, ctx);
  std::vector<uint8_t> result(length, 0);
  whitening_encode(data, length, ctx, result.data());
  return result;
}

std::string BrmeshProtocol::format_hex(const std::vector<uint8_t> &bytes) {
  std::string out;
  out.reserve(bytes.size() * 2);
  char chunk[3];
  for (auto byte : bytes) {
    std::snprintf(chunk, sizeof(chunk), "%02x", byte);
    out.append(chunk);
  }
  return out;
}

uint8_t BrmeshProtocol::reverse8(uint8_t value) {
  uint8_t result = 0;
  for (uint8_t i = 0; i < 8; i++)
    result |= ((value >> i) & 1) << (7 - i);
  return result;
}

uint16_t BrmeshProtocol::reverse16(uint16_t value) {
  uint16_t result = 0;
  for (uint8_t i = 0; i < 16; i++)
    result |= ((value >> i) & 1) << (15 - i);
  return result;
}

uint16_t BrmeshProtocol::crc16(const uint8_t *addr, const uint8_t *data, uint8_t length) {
  uint16_t crc = 0xFFFF;
  for (int8_t i = 2; i >= 0; i--) {
    crc ^= addr[i] << 8;
    for (uint8_t j = 0; j < 4; j++) {
      uint16_t tmp = crc << 1;
      if ((crc & 0x8000) != 0)
        tmp ^= 0x1021;
      crc = tmp << 1;
      if ((tmp & 0x8000) != 0)
        crc ^= 0x1021;
    }
  }
  for (uint8_t i = 0; i < length; i++) {
    crc ^= reverse8(data[i]) << 8;
    for (uint8_t j = 0; j < 4; j++) {
      uint16_t tmp = crc << 1;
      if ((crc & 0x8000) != 0)
        tmp ^= 0x1021;
      crc = tmp << 1;
      if ((tmp & 0x8000) != 0)
        crc ^= 0x1021;
    }
  }
  return ~reverse16(crc) & 0xFFFF;
}

void BrmeshProtocol::whitening_init(uint8_t value, uint8_t *ctx) {
  ctx[0] = 1;
  ctx[1] = (value >> 5) & 1;
  ctx[2] = (value >> 4) & 1;
  ctx[3] = (value >> 3) & 1;
  ctx[4] = (value >> 2) & 1;
  ctx[5] = (value >> 1) & 1;
  ctx[6] = value & 1;
}

void BrmeshProtocol::whitening_encode(const uint8_t *data, uint8_t length, uint8_t *ctx, uint8_t *result) {
  memcpy(result, data, length);
  for (uint8_t i = 0; i < length; i++) {
    int var_c = ctx[3];
    int var_14 = ctx[5];
    int var_18 = ctx[6];
    int var_10 = ctx[4];
    int var_8 = var_14 ^ ctx[2];
    int var_4 = var_10 ^ ctx[1];
    int var_x = var_18 ^ var_c;
    int var_0 = var_x ^ ctx[0];

    int c = result[i];
    result[i] = ((c & 0x80) ^ ((var_8 ^ var_18) << 7)) + ((c & 0x40) ^ (var_0 << 6)) +
                ((c & 0x20) ^ (var_4 << 5)) + ((c & 0x10) ^ (var_8 << 4)) +
                ((c & 0x08) ^ (var_x << 3)) + ((c & 0x04) ^ (var_10 << 2)) +
                ((c & 0x02) ^ (var_14 << 1)) + ((c & 0x01) ^ var_18);

    ctx[2] = var_4;
    ctx[3] = var_8;
    ctx[4] = var_8 ^ var_c;
    ctx[5] = var_0 ^ var_10;
    ctx[6] = var_4 ^ var_14;
    ctx[0] = var_8 ^ var_18;
    ctx[1] = var_0;
  }
}

}  // namespace brmesh
}  // namespace esphome
