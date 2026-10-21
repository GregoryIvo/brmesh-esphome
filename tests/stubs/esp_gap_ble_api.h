#pragma once
#include <cstdint>
#include <string>
#include <vector>
using esp_err_t = int;
constexpr int ESP_OK=0, ESP_BT_STATUS_SUCCESS=0;
constexpr int BLE_SCAN_TYPE_PASSIVE=0, BLE_ADDR_TYPE_PUBLIC=0, BLE_SCAN_FILTER_ALLOW_ALL=0;
constexpr int BLE_SCAN_DUPLICATE_DISABLE=0, ADV_TYPE_NONCONN_IND=0, ADV_CHNL_ALL=7;
constexpr int ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY=0;
struct esp_ble_scan_params_t { int scan_type, own_addr_type, scan_filter_policy, scan_interval, scan_window, scan_duplicate; };
struct esp_ble_adv_params_t { int adv_int_min, adv_int_max, adv_type, own_addr_type; uint8_t peer_addr[6]; int peer_addr_type, channel_map, adv_filter_policy; };
enum esp_gap_ble_cb_event_t { ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT, ESP_GAP_BLE_ADV_START_COMPLETE_EVT,
 ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT, ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT, ESP_GAP_BLE_SCAN_START_COMPLETE_EVT,
 ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT };
struct esp_ble_gap_cb_param_t {
 struct Status { int status=0; } adv_data_raw_cmpl, adv_start_cmpl, adv_stop_cmpl, scan_param_cmpl, scan_start_cmpl, scan_stop_cmpl;
};
using esp_ble_gap_cb_param_t = esp_ble_gap_cb_param_t;
namespace fake_ble {
inline std::vector<std::string> calls;
inline std::vector<std::vector<uint8_t>> packets;
inline int config_result=ESP_OK;
}
inline const char *esp_err_to_name(int) { return "test error"; }
inline int esp_ble_gap_config_adv_data_raw(uint8_t *data, uint32_t size) {
 fake_ble::calls.push_back("config"); fake_ble::packets.emplace_back(data,data+size); return fake_ble::config_result;
}
inline int esp_ble_gap_start_advertising(esp_ble_adv_params_t *) { fake_ble::calls.push_back("start"); return ESP_OK; }
inline int esp_ble_gap_stop_advertising() { fake_ble::calls.push_back("stop"); return ESP_OK; }
inline int esp_ble_gap_set_scan_params(esp_ble_scan_params_t *) { fake_ble::calls.push_back("scan_params"); return ESP_OK; }
inline int esp_ble_gap_start_scanning(int) { fake_ble::calls.push_back("scan_start"); return ESP_OK; }
inline int esp_ble_gap_stop_scanning() { fake_ble::calls.push_back("scan_stop"); return ESP_OK; }
