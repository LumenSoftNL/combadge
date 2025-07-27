#pragma once

#include "esphome/core/defines.h"

#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/core/ring_buffer.h"

#include "esphome/components/audio/audio.h"
#include "esphome/components/microphone/microphone_source.h"
#include "esphome/components/speaker/speaker.h"
#include "esphome/components/espnow/espnow_component.h"

#include <unordered_map>
#include <vector>

namespace esphome::intercom {

enum class Mode {
  NONE,
  MICROPHONE,
  SPEAKER,
};

class InterCom : public Component, public espnow::ESPNowReceivedPacketHandler {
 public:
  virtual ~InterCom();

  void setup() override;
  void loop() override;

  void set_microphone_source(microphone::MicrophoneSource *mic_source) { this->mic_source_ = mic_source; }
  void set_speaker(speaker::Speaker *speaker) { this->speaker_ = speaker; }

  void set_noise_suppression_level(uint8_t noise_suppression_level) {
    this->noise_suppression_level_ = noise_suppression_level;
  }
  void set_auto_gain(uint8_t auto_gain) { this->auto_gain_ = auto_gain; }
  void set_volume_multiplier(float volume_multiplier) { this->volume_multiplier_ = volume_multiplier; }

  void set_address_template(std::function<std::array<uint8_t, ESP_NOW_ETH_ALEN>()> func) {
    this->address_func_ = func;
    this->address_is_static_ = false;
  }
  void set_address_static(const std::array<uint8_t, ESP_NOW_ETH_ALEN> &address) {
    this->address_static_ = address;
    this->address_is_static_ = true;
  }

  uint8_t *get_address() {
    if (this->address_is_static_) {
      return this->address_static_.data();
    } else {
      return this->address_func_().data();
    }
  }

  void set_mode(Mode mode);
  bool is_in_mode(Mode mode);

  float get_setup_priority() const override;
  bool espnow_received_handler(const espnow::ESPNowRecvInfo &info, const uint8_t *data, uint8_t size) override;

 protected:
  void read_microphone_();
  void speaker_start_();

  microphone::MicrophoneSource *mic_source_{nullptr};
  speaker::Speaker *speaker_{nullptr};

  std::shared_ptr<RingBuffer> ring_buffer_;
  audio::AudioStreamInfo target_stream_info_;

  Mode mode_{Mode::SPEAKER};
  uint8_t noise_suppression_level_;
  uint8_t auto_gain_;
  float volume_multiplier_;
  bool wait_to_switch_{false};
  bool can_send_packet_{true};

  std::function<std::array<uint8_t, ESP_NOW_ETH_ALEN>()> address_func_;
  std::array<uint8_t, ESP_NOW_ETH_ALEN> address_static_ = {0xff,0xff,0xff,0xff,0xff,0xff} ;
  bool address_is_static_{true};
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

}  // intercom
