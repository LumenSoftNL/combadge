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

namespace esphome {
namespace intercom {

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

  Mode mode_{Mode::NONE};
  bool wait_to_switch_{false};
  bool can_send_packet_{true};

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

}  // namespace intercom
}
