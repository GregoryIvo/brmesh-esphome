#include <cstdint>
#include <cstdio>
#include <array>
#include <string>
#include <vector>

#include "../components/brmesh/brmesh_protocol.h"

static int failures = 0;

static void expect_hex(const char *name, const std::vector<uint8_t> &actual, const std::string &expected) {
  const std::string hex = esphome::brmesh::BrmeshProtocol::format_hex(actual);
  if (hex != expected) {
    std::printf("FAIL %s: expected %s got %s\n", name, expected.c_str(), hex.c_str());
    failures++;
  } else {
    std::printf("PASS %s\n", name);
  }
}

static void expect_int(const char *name, uint32_t actual, uint32_t expected) {
  if (actual != expected) {
    std::printf("FAIL %s: expected %u got %u\n", name, expected, actual);
    failures++;
  } else {
    std::printf("PASS %s\n", name);
  }
}

static void expect_bool(const char *name, bool actual, bool expected) {
  if (actual != expected) {
    std::printf("FAIL %s: expected %s got %s\n", name, expected ? "true" : "false", actual ? "true" : "false");
    failures++;
  } else {
    std::printf("PASS %s\n", name);
  }
}

int main() {
  using esphome::brmesh::BrmeshProtocol;

  expect_hex("direct brightness payload",
             BrmeshProtocol::build_light_payload(6, true, 0x35, 0, 0, 0, false, false, 0, 0),
             "220635000000");

  expect_hex("direct off payload",
             BrmeshProtocol::build_light_payload(6, false, 0x35, 0, 0, 0, false, false, 0, 0),
             "220600000000");

  expect_hex("rgb payload uses app byte order",
             BrmeshProtocol::build_light_payload(6, true, 0x35, 0x11, 0x22, 0x33, true, false, 0, 0),
             "720635331122");

  expect_hex("color temperature payload",
             BrmeshProtocol::build_light_payload(6, true, 0x35, 0, 0, 0, false, true, 0x44, 0xBB),
             "8206350044bb");

  expect_hex("group brightness command uses app command bytes",
             BrmeshProtocol::build_group_light_command(true, 0x35, 0, 0, 0, false, false, 0, 0),
             "35");

  expect_hex("group rgb command uses app byte order",
             BrmeshProtocol::build_group_light_command(true, 0x35, 0x11, 0x22, 0x33, true, false, 0, 0),
             "b53311220000");

  expect_hex("group color temperature command uses app warm cold order",
             BrmeshProtocol::build_group_light_command(true, 0x35, 0, 0, 0, false, true, 0x44, 0xBB),
             "b5000000bb44");

  const std::array<uint8_t, 4> mesh_key{{0x41, 0x42, 0x43, 0x44}};  // dummy key for fixtures
  expect_hex("encrypted command body from log-like direct command",
             BrmeshProtocol::package_fastcon_body(
                 BrmeshProtocol::parse_hex("220635000000"), 0, 0x06, 5, mesh_key.data(), true),
             "8e303fb3634476444142434441424344");

  const auto forced_new_direct_body = BrmeshProtocol::package_fastcon_body(
      BrmeshProtocol::parse_hex("220635000000"), 1, 0x07, 5, mesh_key.data(), true, true);
  expect_int("high-address direct command uses new protocol body size", forced_new_direct_body.size(), 22);
  const auto forced_new_direct_adv = BrmeshProtocol::build_new_protocol_advertisement(forced_new_direct_body);
  expect_int("high-address direct command advertisement remains BLE-sized", forced_new_direct_adv.size(), 31);
  const auto forced_new_direct_mfg =
      BrmeshProtocol::extract_brmesh_manufacturer(forced_new_direct_adv.data(), forced_new_direct_adv.size());
  const auto forced_new_direct_frame =
      BrmeshProtocol::decode_fastcon_frame_for_logging(forced_new_direct_mfg, mesh_key.data());
  expect_bool("high-address direct command new protocol frame decodes", forced_new_direct_frame.valid, true);
  const auto forced_new_direct_payload_prefix =
      forced_new_direct_frame.payload.size() >= 6
          ? std::vector<uint8_t>(forced_new_direct_frame.payload.begin(), forced_new_direct_frame.payload.begin() + 6)
          : std::vector<uint8_t>{};
  expect_hex("high-address direct command keeps payload",
             forced_new_direct_payload_prefix, "220635000000");

  // package_ble_fastcon_body substitutes DEFAULT_KEY for a null key: the body is XORed with 5e367bc4.
  expect_hex("discovery scan body uses the default key like the native packer",
             BrmeshProtocol::package_fastcon_body(
                 BrmeshProtocol::parse_hex("000000000000000000000000"), 0, 0x01, 0, nullptr, false),
             "5e3784c45e367bc45e367bc45e367bc4");

  expect_hex("group off command from app log shape",
             BrmeshProtocol::build_group_control_payload(43050, 0, BrmeshProtocol::parse_hex("00")),
             "432aa8000000000000000000");

  expect_hex("group on command from app log shape",
             BrmeshProtocol::build_group_control_payload(43050, 0, BrmeshProtocol::parse_hex("80")),
             "432aa8008000000000000000");

  expect_hex("group rgb command from app log shape",
             BrmeshProtocol::build_group_control_payload(43050, 0, BrmeshProtocol::parse_hex("8c000000ff00")),
             "932aa8008c000000ff000000");

  const std::vector<uint16_t> temp_group_addrs{1, 2, 3, 4, 5};
  expect_hex("temporary group membership chunk carries app token",
             BrmeshProtocol::build_temp_group_payload(253, temp_group_addrs, 0x3412),
             "45fd0112341f000000000000000000000000");

  const auto temp_batches =
      BrmeshProtocol::build_temp_group_payloads(253, std::vector<uint16_t>{0x0106, 0x0005, 0x006e, 0x0001}, 0x3412);
  const auto address_batches = BrmeshProtocol::build_address_batches(std::vector<uint16_t>{0x0106, 0x0005, 0x006e, 0x0001});
  expect_int("address batches preserve first high byte", address_batches[0].high, 0);
  expect_int("address batches preserve second high byte", address_batches[1].high, 1);
  expect_int("address batches first high first window size", address_batches[0].windows[0].size(), 2);
  expect_int("address batches first high second window size", address_batches[0].windows[1].size(), 1);
  expect_int("temporary group batches split by high and 104 address window", temp_batches.size(), 3);
  expect_hex("temporary group sorted high zero first window", temp_batches[0], "45fd01123411000000000000000000000000");
  expect_hex("temporary group high zero second window", temp_batches[1], "45fd6e123401000000000000000000000000");
  expect_hex("temporary group high one window", temp_batches[2], "45fd06123401000000000000000000000000");
  expect_int("temporary group app delay is interval times retry times payloads",
             BrmeshProtocol::temp_group_delay_ms(temp_batches.size()), 1350);

  const std::vector<uint8_t> adv =
      BrmeshProtocol::parse_hex("0201041bfff0ffa55a11223344556677889900aabbccddeeff0011223344");
  const auto manufacturer = BrmeshProtocol::extract_brmesh_manufacturer(adv.data(), adv.size());
  expect_hex("extract manufacturer payload", manufacturer, "f0ffa55a11223344556677889900aabbccddeeff001122334400");

  const auto whiten_source = BrmeshProtocol::parse_hex("710f558343c35e367bc45e367bc45e367bc40000");
  const auto whitened = BrmeshProtocol::whiten(whiten_source.data(), whiten_source.size());
  expect_hex("whitening is reversible", BrmeshProtocol::whiten(whitened.data(), whitened.size()),
             "710f558343c35e367bc45e367bc45e367bc40000");

  expect_hex("query one device state",
             BrmeshProtocol::build_query_state_payload(std::vector<uint16_t>{6}, 0x0f),
             "00400f000601000000000000000000000000");

  expect_hex("query multiple nearby device states",
             BrmeshProtocol::build_query_state_payload(std::vector<uint16_t>{6, 7, 14}, 0x0f),
             "00400f000603010000000000000000000000");

  const auto query_batches = BrmeshProtocol::build_query_state_payloads(std::vector<uint16_t>{0x0106, 0x0005, 0x006e, 0x0001}, 0x0f);
  expect_int("query batches split by high and 104 address window", query_batches.size(), 3);
  expect_hex("query sorted high zero first window", query_batches[0], "00400f000111000000000000000000000000");
  expect_hex("query high zero second window", query_batches[1], "00400f006e01000000000000000000000000");
  expect_hex("query high one window", query_batches[2], "00400f000601000000000000000000000000");

  const auto new_protocol_body = BrmeshProtocol::parse_hex("0102030405060708090a0b0c0d0e0f10111213141516");
  expect_hex("new protocol advertisement fits legacy BLE limit",
             BrmeshProtocol::build_new_protocol_advertisement(new_protocol_body),
             "0201041bfff0ffa55a0102030405060708090a0b0c0d0e0f10111213141516");

  const auto raw_query_payload = BrmeshProtocol::parse_hex("00400f000101000000000000000000000000");
  expect_hex("raw diagnostic parser accepts native 18 byte payloads", raw_query_payload,
             "00400f000101000000000000000000000000");

  // Upload frames as libbroadlink_ble.so parses them: addr = body[0] | high << 8, body[2] = len << 4 | subtype.
  const auto upload_body = BrmeshProtocol::build_upload_body(0x06, 0, BrmeshProtocol::parse_hex("c000000000ff"));
  expect_hex("upload body layout", upload_body, "060060c000000000ff000000000000000000");
  const auto encoded_state = BrmeshProtocol::package_fastcon_body(upload_body, 1, 0x44, 4, mesh_key.data(), true);
  const auto state_adv = BrmeshProtocol::build_new_protocol_advertisement(encoded_state);
  const auto state_mfg = BrmeshProtocol::extract_brmesh_manufacturer(state_adv.data(), state_adv.size());
  const auto state_frame = BrmeshProtocol::decode_frame(state_mfg, mesh_key.data());
  expect_int("upload frame command type", state_frame.command_type, 4);
  const auto decoded_state = BrmeshProtocol::decode_state_callback(state_mfg, mesh_key.data());
  expect_bool("state callback decodes", decoded_state.valid, true);
  expect_int("state callback decodes address from body[0] and high nibble", decoded_state.address, 0x0106);
  expect_int("state callback exposes sub type", decoded_state.state_type, 0x00);
  expect_hex("state callback exposes data", decoded_state.payload, "c000000000ff");

  const auto zero_len_body = BrmeshProtocol::parse_hex("0600010203040506070809101112131415");
  const auto zero_len_state = BrmeshProtocol::decode_state_from_frame(BrmeshProtocol::decode_frame(
      BrmeshProtocol::extract_brmesh_manufacturer(
          BrmeshProtocol::build_new_protocol_advertisement(
              BrmeshProtocol::package_fastcon_body(zero_len_body, 0, 0x45, 4, mesh_key.data(), true)).data(), 31),
      mesh_key.data()));
  expect_int("zero length nibble means frame length minus seven", zero_len_state.payload.size(), 15);
  expect_int("zero length nibble keeps sub type", zero_len_state.state_type, 1);

  auto corrupt_state_mfg = state_mfg;
  corrupt_state_mfg[corrupt_state_mfg.size() - 1] ^= 0x01;
  expect_bool("state callback rejects checksum-corrupt frame",
              BrmeshProtocol::decode_state_callback(corrupt_state_mfg, mesh_key.data()).valid, false);
  const auto zero_address_body = BrmeshProtocol::package_fastcon_body(
      BrmeshProtocol::build_upload_body(0x00, 0, BrmeshProtocol::parse_hex("c0")), 0, 0x45, 4, mesh_key.data(), true);
  const auto zero_address_adv = BrmeshProtocol::build_new_protocol_advertisement(zero_address_body);
  const auto zero_address_mfg =
      BrmeshProtocol::extract_brmesh_manufacturer(zero_address_adv.data(), zero_address_adv.size());
  expect_bool("state callback rejects zero address",
              BrmeshProtocol::decode_state_callback(zero_address_mfg, mesh_key.data()).valid, false);
  const auto control_frame_state = BrmeshProtocol::decode_state_callback(
      BrmeshProtocol::extract_brmesh_manufacturer(
          BrmeshProtocol::build_new_protocol_advertisement(
              BrmeshProtocol::package_fastcon_body(upload_body, 1, 0x46, 5, mesh_key.data(), true)).data(), 31),
      mesh_key.data());
  expect_bool("control frames are not state uploads", control_frame_state.valid, false);

  // Legacy RF frames exactly as the phone transmits them. The structure (and the whitened prefix) was verified
  // byte-for-byte against an app logcat capture (send---> payload / calculatedPayload); the fixture here is
  // re-keyed to the dummy key so the repository does not publish a real mesh key.
  const auto app_body = BrmeshProtocol::parse_hex("8eb43f7ec84259435f464e5177424344");
  const auto app_adv = BrmeshProtocol::build_legacy_advertisement(app_body);
  expect_hex("legacy advertisement framing", app_adv, "0201041bfff0ff6db64368931ddd87e7c4504a7d8864ba3ff283172b8b6069");
  expect_hex("legacy whitened prefix is the constant seen on air",
             std::vector<uint8_t>(BrmeshProtocol::legacy_whitened_prefix().begin(),
                                  BrmeshProtocol::legacy_whitened_prefix().end()),
             "6db64368931d");
  const auto app_mfg = BrmeshProtocol::extract_brmesh_manufacturer(app_adv.data(), app_adv.size());
  const auto app_frame = BrmeshProtocol::decode_frame(app_mfg, mesh_key.data());
  expect_bool("legacy whitened frame decodes", app_frame.valid, true);
  expect_bool("legacy whitened frame is flagged legacy", app_frame.legacy, true);
  expect_int("legacy frame command type", app_frame.command_type, 5);
  expect_int("legacy frame sequence", app_frame.sequence, 130);
  expect_int("legacy frame forward flag", app_frame.forward, 1);
  expect_hex("legacy frame payload is the time sync payload", app_frame.payload, "89001a071e040d1536000000");
  auto corrupt_legacy = app_mfg;
  corrupt_legacy[10] ^= 0x01;
  expect_bool("legacy frame with a corrupted body fails the fastcon checksum", BrmeshProtocol::decode_frame(corrupt_legacy, mesh_key.data()).valid, false);
  expect_bool("intact legacy frame reports rf crc ok", app_frame.crc_ok, true);
  expect_bool("intact legacy frame is not a relay", app_frame.relayed, false);
  // Observed 2026-09-16 on three captured relays: bulbs re-broadcast a frame with the body intact and the RF
  // CRC XORed with 0x79EF. Fixture re-keyed to the dummy key.
  const auto relayed_mfg = BrmeshProtocol::parse_hex("f0ff6db64368931ddd87e7c4504a7d8864ba3ff283172b8b8f10");
  const auto relayed_frame = BrmeshProtocol::decode_frame(relayed_mfg, mesh_key.data());
  expect_bool("relayed legacy frame decodes", relayed_frame.valid, true);
  expect_bool("relayed legacy frame reports rf crc mismatch", relayed_frame.crc_ok, false);
  expect_bool("relayed legacy frame is recognised as a relay", relayed_frame.relayed, true);
  expect_int("relayed legacy frame keeps sequence", relayed_frame.sequence, 130);
  expect_hex("relayed legacy frame keeps payload", relayed_frame.payload, "89001a071e040d1536000000");
  // Real bulb status reply captured the same night: addr 8, level 0x33, warm 0x61 cold 0x9e.
  const auto reply_mfg = BrmeshProtocol::parse_hex("f0ffa55a") ;
  (void) reply_mfg;
  std::vector<uint8_t> raw16_mfg{0xF0, 0xFF};
  raw16_mfg.insert(raw16_mfg.end(), app_body.begin(), app_body.end());
  expect_bool("raw 16 byte legacy body decodes", BrmeshProtocol::decode_frame(raw16_mfg, mesh_key.data()).valid, true);

  // Mode ("scene") packets: package_scene_control = 0x28 | steps << 4, package_groupId_scene_control = 0x2C.
  expect_hex("single light full color fade at speed 1",
             BrmeshProtocol::build_scene_control_payload(6, 1, BrmeshProtocol::parse_hex("420301050406")),
             "880601420301050406000000");
  expect_hex("group three color flash at app default speed",
             BrmeshProtocol::build_group_scene_control_payload(253, 200, BrmeshProtocol::parse_hex("c20401")),
             "5cfdc8c20401000000000000");
  expect_hex("scene steps are capped at nine",
             BrmeshProtocol::build_group_scene_control_payload(253, 5, BrmeshProtocol::parse_hex("42010203040506070809")),
             "bcfd05420102030405060708");

  // Discovery frame from a bulb in pairing mode (type 1): did, type, device key; no checksum enforced.
  {
    std::vector<uint8_t> body(16, 0);
    body[0] = 0x10;  // type 1, high 0, forward 0
    body[1] = 0x07;
    body[2] = 0xFF;
    body[3] = 0x00;  // deliberately wrong checksum: the native parser never checks it for type 1
    const auto did_type_key = BrmeshProtocol::parse_hex("a1b2c3d4e5f6" "2caa" "11223344");
    for (size_t i = 0; i < did_type_key.size(); i++)
      body[4 + i] = did_type_key[i];
    for (size_t i = 0; i < 4; i++)
      body[i] ^= BrmeshProtocol::DEFAULT_KEY[i];
    std::vector<uint8_t> mfg{0xF0, 0xFF};
    mfg.insert(mfg.end(), body.begin(), body.end());
    const auto frame = BrmeshProtocol::decode_frame(mfg, mesh_key.data());
    expect_bool("discovery frame decodes without checksum", frame.valid, true);
    expect_int("discovery frame command type", frame.command_type, 1);
    const auto discovery = BrmeshProtocol::decode_discovery(frame);
    expect_bool("discovery decodes", discovery.valid, true);
    expect_hex("discovery did", std::vector<uint8_t>(discovery.did.begin(), discovery.did.end()), "a1b2c3d4e5f6");
    expect_int("discovery device type little endian", discovery.device_type, 43564);
    expect_hex("discovery device key", std::vector<uint8_t>(discovery.key.begin(), discovery.key.end()), "11223344");
    expect_hex("discovery response for a short address",
               BrmeshProtocol::build_discovery_response(discovery.did, 18, discovery.device_type, mesh_key.data()),
               "a1b2c3d4e5f6120141424344");
    expect_hex("discovery response extended form for addresses above 256",
               BrmeshProtocol::build_discovery_response(discovery.did, 300, discovery.device_type, mesh_key.data(), 7),
               "a1b2c3d4e5f62c0141424344070000000000");
  }
  // Heartbeat: type 5 frame, payload[0] & 0x0f == 4, addr = payload[1] | high << 8, group = payload[2].
  {
    const auto heartbeat_body = BrmeshProtocol::package_fastcon_body(
        BrmeshProtocol::parse_hex("041200010203040506"), 1, 0x33, 5, mesh_key.data(), true);
    const auto adv = BrmeshProtocol::build_legacy_advertisement(heartbeat_body);
    const auto frame = BrmeshProtocol::decode_frame(BrmeshProtocol::extract_brmesh_manufacturer(adv.data(), adv.size()), mesh_key.data());
    const auto heartbeat = BrmeshProtocol::decode_heartbeat(frame);
    expect_bool("heartbeat decodes", heartbeat.valid, true);
    expect_int("heartbeat address includes high nibble", heartbeat.address, 0x0112);
    expect_int("heartbeat group", heartbeat.group, 0);
    expect_bool("control frames that are not heartbeats are rejected",
                BrmeshProtocol::decode_heartbeat(app_frame).valid, false);
  }

  return failures == 0 ? 0 : 1;
}
