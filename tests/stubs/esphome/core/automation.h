#pragma once
namespace esphome {
template<typename... Ts> class Action { public: virtual void play(Ts...)=0; };
template<typename... Ts> class Trigger { public: void trigger(Ts...) {} };
template<typename T> struct TestTemplatable { template<typename... Ts> T value(Ts...) { return {}; } };
}
#define TEMPLATABLE_VALUE(type, name) esphome::TestTemplatable<type> name##_;
