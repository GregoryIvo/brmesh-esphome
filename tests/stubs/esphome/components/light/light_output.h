#pragma once
#include <initializer_list>
namespace esphome::light {
enum class ColorMode { RGB, BRIGHTNESS, COLOR_TEMPERATURE };
struct LightColorValues {
 float state=1,brightness=1,red=1,green=1,blue=1,color_brightness=1,color_temperature=153;
 ColorMode mode=ColorMode::RGB;
#define FIELD(n) float get_##n() const {return n;} void set_##n(float v) {n=v;}
 FIELD(state) FIELD(brightness) FIELD(red) FIELD(green) FIELD(blue) FIELD(color_brightness) FIELD(color_temperature)
#undef FIELD
 ColorMode get_color_mode() const {return mode;} void set_color_mode(ColorMode m) {mode=m;}
 void as_binary(bool *x) const {*x=state==1;}
 void as_brightness(float *x) const {*x=state*brightness;}
 void as_rgb(float *r,float *g,float *b) const {*r=state*brightness*color_brightness*red;*g=state*brightness*color_brightness*green;*b=state*brightness*color_brightness*blue;}
 void as_ct(float lo,float hi,float *ct,float *b) const {*ct=(color_temperature-lo)/(hi-lo);*b=state*brightness;}
};
class LightOutput;
struct LightState { LightColorValues current_values,remote_values; unsigned published=0; LightOutput *output=nullptr; void publish_state() {published++;} LightOutput *get_output() const {return output;} };
struct LightTraits { void set_supported_color_modes(std::initializer_list<ColorMode>) {} void set_min_mireds(float) {} void set_max_mireds(float) {} };
class LightOutput { public: virtual ~LightOutput()=default; virtual LightTraits get_traits()=0; virtual void setup_state(LightState *) {} virtual void write_state(LightState *)=0; };
}
