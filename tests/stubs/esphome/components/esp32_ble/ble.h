#pragma once
#include "esp_gap_ble_api.h"
namespace esphome::esp32_ble {
struct BLEScanResult { uint8_t adv_data_len=0,scan_rsp_len=0,ble_adv[62]{}; int8_t rssi=0; };
struct ESP32BLE { bool active=true; unsigned disables=0, enables=0; bool is_active() {return active;}
 void disable() { active=false; disables++; } void enable() { active=true; enables++; } };
inline ESP32BLE instance;
inline ESP32BLE *global_ble=&instance;
}
