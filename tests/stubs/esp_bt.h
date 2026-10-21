#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "esp_gap_ble_api.h"
enum esp_ble_power_type_t { ESP_BLE_PWR_TYPE_ADV = 9, ESP_BLE_PWR_TYPE_DEFAULT = 11 };
enum esp_power_level_t { ESP_PWR_LVL_P9 = 7 };
inline int esp_ble_tx_power_set(esp_ble_power_type_t, esp_power_level_t) { fake_ble::calls.push_back("tx_power"); return ESP_OK; }
