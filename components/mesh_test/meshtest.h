#pragma once

#include "esphome/core/defines.h"

#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/components/meshmesh/meshmesh.h"

#include <unordered_map>
#include <vector>

namespace esphome::meshtest {


class MeshTest : public Component, public Parented<meshmesh::MeshmeshComponent> {
 public:
  void setup() override;
  void dump_config() override;
  void loop() override;

  void set_address(uint32_t address) { this->address_ = address; }

  void set_broadcast_allowed(bool value) { this->broadcast_allowed_ = value; }
  void set_send_frames(bool value) { this->send_frames_ = value; }

  float get_setup_priority() const override;

  int8_t handleFrame(uint8_t *buf, uint16_t len, uint32_t from);

 protected:
  void send_packet_();
  bool handle_received_(uint8_t *data, size_t size, uint32_t from);

  bool validate_address_(uint32_t address);
  uint32_t address_{0xffffffff};

  uint16_t old_counter_value_ = 1;
  uint16_t packet_counter_ = 0;

  bool send_frames_{false};

  bool can_send_packet_{true};
  bool broadcast_allowed_{false};

  HighFrequencyLoopRequester high_freq_;
};

template<typename... Ts> class ModeAction : public Action<Ts...>, public Parented<MeshTest> {
 public:
  void set_send_frames(bool send_frames) { this->send_frames_ = send_frames; }
  void play(Ts... x) override { this->parent_->set_send_frames(this->send_frames_); }

 protected:
  bool send_frames_{false};
};

template<typename... Ts> class ChangeAddressAction : public Action<Ts...>, public Parented<MeshTest> {
  TEMPLATABLE_VALUE(uint32_t, address)
 public:
  void play(Ts... x) override {
    auto address = this->address_.value(x...);
    this->parent_->set_address(address); }
};

}  // namespace esphome::intercom
