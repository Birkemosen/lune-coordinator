#include "ch422g.h"
#include "esphome/core/log.h"

namespace esphome {
namespace ch422g {

static const uint8_t IO_EXTENSION_MODE = 0x02;
static const uint8_t IO_EXTENSION_OUTPUT = 0x03;
static const uint8_t IO_EXTENSION_INPUT = 0x04;

static const char *const TAG = "ch422g";

void CH422GComponent::setup() {
  ESP_LOGI(TAG, "7B IO extension mode: 0x%02X, output shadow: 0x%02X", this->mode_value_,
           static_cast<uint8_t>(this->output_bits_));
  if (!this->set_mode_(this->mode_value_)) {
    ESP_LOGE(TAG, "CH422G not detected at 0x%02X", this->address_);
    this->mark_failed();
    return;
  }

  ESP_LOGCONFIG(TAG, "Initialization complete. Warning: %d, Error: %d", this->status_has_warning(),
                this->status_has_error());
}

void CH422GComponent::loop() { this->pin_read_flags_ = 0x00; }

void CH422GComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "CH422G:");
  LOG_I2C_DEVICE(this)
  ESP_LOGCONFIG(TAG, "  Initial/output bits: 0x%03X", this->output_bits_);
  if (this->is_failed()) {
    ESP_LOGE(TAG, ESP_LOG_MSG_COMM_FAIL);
  }
}

void CH422GComponent::pin_mode(uint8_t pin, gpio::Flags flags) {
  if (pin >= 8)
    return;
  if (flags & gpio::FLAG_OUTPUT) {
    this->mode_value_ |= (1 << pin);
  } else {
    this->mode_value_ &= ~(1 << pin);
  }
}

bool CH422GComponent::digital_read(uint8_t pin) {
  if (this->pin_read_flags_ == 0 || this->pin_read_flags_ & (1 << pin)) {
    this->read_inputs_();
  }

  this->pin_read_flags_ |= (1 << pin);
  return (this->input_bits_ & (1 << pin)) != 0;
}

void CH422GComponent::digital_write(uint8_t pin, bool value) {
  if (value) {
    this->output_bits_ |= (1 << pin);
  } else {
    this->output_bits_ &= ~(1 << pin);
  }
  this->write_outputs_();
}

bool CH422GComponent::read_inputs_() {
  if (this->is_failed()) {
    return false;
  }
  this->input_bits_ = this->read_command_(IO_EXTENSION_INPUT);
  this->status_clear_warning();
  return true;
}

bool CH422GComponent::write_command_(uint8_t command, uint8_t value) {
  uint8_t data[2] = {command, value};
  auto err = this->bus_->write_readv(this->address_, data, sizeof(data), nullptr, 0);
  if (err != i2c::ERROR_OK) {
    char buf[64];
    snprintf(buf, sizeof(buf), "write failed for command 0x%X, error %d", command, err);
    this->status_set_warning(buf);
    return false;
  }
  this->status_clear_warning();
  return true;
}

uint8_t CH422GComponent::read_command_(uint8_t command) {
  uint8_t value;
  auto err = this->bus_->write_readv(this->address_, &command, 1, &value, 1);
  if (err != i2c::ERROR_OK) {
    char buf[64];
    snprintf(buf, sizeof(buf), "read failed for command 0x%X, error %d", command, err);
    this->status_set_warning(buf);
    return 0;
  }
  this->status_clear_warning();
  return value;
}

bool CH422GComponent::set_mode_(uint8_t mode) { return this->write_command_(IO_EXTENSION_MODE, mode); }

bool CH422GComponent::write_outputs_() {
  return this->write_command_(IO_EXTENSION_OUTPUT, static_cast<uint8_t>(this->output_bits_));
}

float CH422GComponent::get_setup_priority() const { return setup_priority::IO; }

#ifdef USE_LOOP_PRIORITY
float CH422GComponent::get_loop_priority() const { return 9.0f; }
#endif

void CH422GGPIOPin::pin_mode(gpio::Flags flags) { this->parent_->pin_mode(this->pin_, flags); }
bool CH422GGPIOPin::digital_read() { return this->parent_->digital_read(this->pin_) ^ this->inverted_; }
void CH422GGPIOPin::digital_write(bool value) { this->parent_->digital_write(this->pin_, value ^ this->inverted_); }
size_t CH422GGPIOPin::dump_summary(char *buffer, size_t len) const {
  return buf_append_printf(buffer, len, 0, "EXIO%u via CH422G", this->pin_);
}
void CH422GGPIOPin::set_flags(gpio::Flags flags) {
  flags_ = flags;
  this->parent_->pin_mode(this->pin_, flags);
}

}  // namespace ch422g
}  // namespace esphome
