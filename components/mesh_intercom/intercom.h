#pragma once

#include "esphome/core/defines.h"

#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/core/ring_buffer.h"

#include "esphome/components/audio/audio.h"
#include "esphome/components/microphone/microphone_source.h"
#include "esphome/components/speaker/speaker.h"

#include "esphome/components/meshmesh/meshmesh.h"

#include <unordered_map>
#include <vector>

namespace esphome::intercom {

enum class Mode { NONE, MICROPHONE, SPEAKER };

class InterCom : public Component, public Parented<meshmesg::MeshmeshComponent> {
 public:
  void setup() override;
  void dump_config() override;
  void loop() override;

  void set_microphone_source(microphone::MicrophoneSource *mic_source) { this->mic_source_ = mic_source; }
  void set_speaker(speaker::Speaker *speaker) { this->speaker_ = speaker; }

  void add_play_audio_callback(std::function<size_t(uint8_t *, size_t)> &&callback) {
    this->play_audio_callback_.add(std::move(callback));
  }

  void set_address(uint32_t address) { this->address_ = address; }

  void set_broadcast_allowed(bool value) { this->broadcast_allowed_ = value; }

  void set_mode(Mode mode);
  bool is_in_mode(Mode mode);

  float get_setup_priority() const override;

  int8_t handleFrame(uint8_t *buf, uint16_t len, uint32_t from);

  void receive_audio(const uint8_t *data, size_t length) { this->buffer_audio(data, length); }
  size_t buffer_audio(const uint8_t *data, size_t length);
  bool has_buffered_data() { return (this->ring_buffer_mic_.use_count() >= 0) && this->ring_buffer_mic_->available(); }

  std::shared_ptr<RingBuffer> ring_buffer() { return this->ring_buffer_mic_; }

 protected:
  void send_audio_packet_();
  bool handle_received_(uint8_t *data, size_t size);
  void speaker_start_();
  bool has_mic_source_() { return this->mic_source_ != nullptr; }
  bool has_spr_source_() { return this->speaker_ != nullptr; }

  microphone::MicrophoneSource *mic_source_{nullptr};
  speaker::Speaker *speaker_{nullptr};

  std::shared_ptr<RingBuffer> ring_buffer_mic_;

  bool validate_address_(uint32_t address);
  uint32_t mesh_address_{};

  audio::AudioStreamInfo target_stream_info_;

  Mode mode_{Mode::NONE};

  bool wait_to_switch_{false};
  bool can_send_packet_{true};
  bool broadcast_allowed_{false};

  HighFrequencyLoopRequester high_freq_;
  CallbackManager<void(uint8_t *, size_t)> play_audio_callback_{};
};

template<typename... Ts> class ModeAction : public Action<Ts...>, public Parented<InterCom> {
 public:
  void set_mode(Mode mode) { this->mode_ = mode; }
  void play(Ts... x) override { this->parent_->set_mode(this->mode_); }

 protected:
  Mode mode_{Mode::NONE};
};

template<typename... Ts> class ChangeAddressAction : public Action<Ts...>, public Parented<InterCom> {
  TEMPLATABLE_VALUE(uint32_t, address)
 public:
  void play(Ts... x) override {
    auto address = this->address_.value(x...);
    this->parent_->set_address(address); }
};


template<typename... Ts> class IsModeCondition : public Condition<Ts...>, public Parented<InterCom> {
 public:
  void set_mode(Mode mode) { this->mode_ = mode; }
  bool check(Ts... x) override { return this->parent_->is_in_mode(this->mode_); }

 protected:
  Mode mode_{Mode::NONE};
};

}  // namespace esphome::intercom
