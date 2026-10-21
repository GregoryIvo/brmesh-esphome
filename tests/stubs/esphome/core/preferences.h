#pragma once
namespace esphome {
struct ESPPreferenceObject { template<typename T> bool load(T *) {return false;} template<typename T> void save(T *) {} };
struct Preferences { template<typename T> ESPPreferenceObject make_preference(unsigned int,bool) {return {};} void sync() {} };
inline Preferences prefs;
inline Preferences *global_preferences=&prefs;
}
