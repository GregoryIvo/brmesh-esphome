#pragma once
#include "light_output.h"
namespace esphome::light {
class LightEffect {
 public:
  explicit LightEffect(const char *name) : name_(name) {}
  virtual ~LightEffect() = default;
  virtual void start() {}
  virtual void stop() {}
  virtual void apply() = 0;
  const char *get_name() const { return name_; }
  void init_internal(LightState *state) { state_ = state; }
 protected:
  LightState *state_{nullptr};
  const char *name_;
};
}
