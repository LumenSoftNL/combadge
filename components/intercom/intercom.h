#pragma once

#include "esphome/core/defines.h"

#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"

#include "esphome/components/microphone/microphone.h"
#include "esphome/components/speaker/speaker.h"
#include "esphome/components/espnow/espnow.h"

#include <unordered_map>
#include <vector>

namespace esphome {
namespace intercom {

enum class Mode {
  NONE,
  MICROPHONE,
  SPEAKER,
};

class InterCom : public Component, public espnow::ESPNowProtocol {
 public:
  virtual ~InterCom();
  void setup() override;
  void loop() override;
  float get_setup_priority() const override;
  bool on_receive(espnow::ESPNowPacket &packet);

  void set_microphone(microphone::Microphone *mic) { this->mic_ = mic; }
  void set_speaker(speaker::Speaker *speaker) { this->speaker_ = speaker; }

  void set_mode(Mode mode);
  bool is_in_mode(Mode mode);

  uint32_t get_protocol_id() override { return 0x572674; }
  std::string get_protocol_name() override { return "InterCom"; }


 protected:
  void read_microphone_();

  microphone::Microphone *mic_{nullptr};
  speaker::Speaker *speaker_{nullptr};
  Mode mode_{Mode::NONE};

  HighFrequencyLoopRequester high_freq_;
  
};

template<typename... Ts> class ModeAction : public Action<Ts...>, public Parented<InterCom> {
 public:
  void set_mode(Mode mode) { this->mode_ = mode; }
  void play(Ts... x) override { this->parent_->set_mode(this->mode_); }

 protected:
  Mode mode_{Mode::NONE};
};

template<typename... Ts> class IsModeCondition : public Condition<Ts...>, public Parented<InterCom> {
 public:
  void set_mode(Mode mode) { this->mode_ = mode; }
  bool check(Ts... x) override { return this->parent_->is_in_mode(this->mode_); }

 protected:
  Mode mode_{Mode::NONE};
};

extern InterCom *global_intercom;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

}  // namespace intercom
}  // namespace esphome
