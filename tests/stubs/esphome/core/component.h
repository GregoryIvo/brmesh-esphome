#pragma once
#include <cstdint>
namespace esphome {
inline uint32_t test_now=100;
inline uint32_t millis() { return test_now; }
namespace setup_priority { constexpr float AFTER_BLUETOOTH=200; }
class Component {
 public:
 virtual ~Component()=default;
 virtual void setup() {} virtual void loop() {} virtual void dump_config() {}
 virtual float get_setup_priority() const { return 0; }
 void mark_failed() {} void status_set_warning() {} void status_clear_warning() {}
};
}
