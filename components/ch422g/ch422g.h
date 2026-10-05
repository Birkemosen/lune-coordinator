#pragma once

#include "esphome/components/i2c/i2c.h"
#include "esphome/core/component.h"
#include "esphome/core/hal.h"

namespace esphome {
namespace ch422g {

class CH422GComponent : public Component, public i2c::I2CDevice {
 public:
  CH422GComponent() = default;

  void setup() override;
  void loop() override;
  bool digital_read(uint8_t pin);
  void digital_write(uint8_t pin, bool value);
  void pin_mode(uint8_t pin, gpio::Flags flags);

  void set_initial_output_bits(uint16_t bits) { this->output_bits_ = bits; }

  float get_setup_priority() const override;
#ifdef USE_LOOP_PRIORITY
  float get_loop_priority() const override;
#endif
  void dump_config() override;

 protected:
  bool write_command_(uint8_t command, uint8_t value);
  uint8_t read_command_(uint8_t command);
  bool set_mode_(uint8_t mode);
  bool read_inputs_();
  bool write_outputs_();

  uint16_t output_bits_{0x00};
  uint8_t pin_read_flags_ = {0x00};
  uint8_t input_bits_ = {0x00};
  uint8_t mode_value_{0xFF};
};

class CH422GGPIOPin : public GPIOPin {
 public:
  void setup() override {}
  void pin_mode(gpio::Flags flags) override;
  bool digital_read() override;
  void digital_write(bool value) override;
  size_t dump_summary(char *buffer, size_t len) const override;

  void set_parent(CH422GComponent *parent) { parent_ = parent; }
  void set_pin(uint8_t pin) { pin_ = pin; }
  void set_inverted(bool inverted) { inverted_ = inverted; }
  void set_flags(gpio::Flags flags);

  gpio::Flags get_flags() const override { return this->flags_; }

 protected:
  CH422GComponent *parent_{};
  uint8_t pin_{};
  bool inverted_{};
  gpio::Flags flags_{};
};

}  // namespace ch422g
}  // namespace esphome
