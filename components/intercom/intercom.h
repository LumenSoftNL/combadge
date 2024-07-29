#pragma once

#include "esphome/core/defines.h"

#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/core/ring_buffer.h"

#include "esphome/components/microphone/microphone.h"
#include "esphome/components/speaker/speaker.h"
#include "esphome/components/espnow/espnow.h"

#ifdef USE_ESP_ADF
#include <esp_vad.h>
#endif

#include <unordered_map>
#include <vector>

namespace esphome {
namespace intercom {

enum class Direction {
  NONE,
  MICROPHONE,
  SPEAKER,
};

enum class State {
  IDLE,
  START_MICROPHONE,
  STARTING_MICROPHONE,
  STREAMING_MICROPHONE,
  STOPPING_MICROPHONE,
  STOP_MICROPHONE,

  START_SPEAKER,
  STARTING_SPEAKER,
  STREAMING_SPEAKER,
  STOPPING_SPEAKER,
  STOP_SPEAKER,
};

class InterCom : public espnow::ESPNowInterface {
 public:
  void setup() override;
  void loop() override;
  float get_setup_priority() const override;
  bool on_package_received(espnow::ESPNowPacket *package);

  void set_microphone(microphone::Microphone *mic) { this->mic_ = mic; }
  void set_speaker(speaker::Speaker *speaker) { this->speaker_ = speaker; }

  void set_direction(Direction direction);
  bool is_running(Direction direction);

#ifdef USE_ESP_ADF
  void set_vad_threshold(uint8_t vad_threshold) { this->vad_threshold_ = vad_threshold; }
#endif

  void set_noise_suppression_level(uint8_t noise_suppression_level) {
    this->noise_suppression_level_ = noise_suppression_level;
  }
  void set_auto_gain(uint8_t auto_gain) { this->auto_gain_ = auto_gain; }
  void set_volume_multiplier(float volume_multiplier) { this->volume_multiplier_ = volume_multiplier; }

  Trigger<> *get_end_trigger() const { return this->end_trigger_; }
  Trigger<> *get_start_trigger() const { return this->start_trigger_; }
  Trigger<std::string, std::string> *get_error_trigger() const { return this->error_trigger_; }
  Trigger<> *get_idle_trigger() const { return this->idle_trigger_; }

 protected:
  bool allocate_buffers_();
  void clear_buffers_();
  void deallocate_buffers_();
  uint8_t *read_buffer_;
  std::unique_ptr<RingBuffer> input_buffer_;
  std::unique_ptr<RingBuffer> output_buffer_;

  int read_microphone_();
  void write_speaker_();
  microphone::Microphone *mic_{nullptr};
  speaker::Speaker *speaker_{nullptr};

  void set_state_(State state);
  void set_state_(State state, State desired_state);
  State state_{State::IDLE};
  State desired_state_{State::IDLE};

  Trigger<> *start_trigger_ = new Trigger<>();
  Trigger<> *end_trigger_ = new Trigger<>();
  Trigger<std::string, std::string> *error_trigger_ = new Trigger<std::string, std::string>();
  Trigger<> *idle_trigger_ = new Trigger<>();

  uint8_t noise_suppression_level_;
  uint8_t auto_gain_;
  float volume_multiplier_;

  bool silence_detection_{false};

  HighFrequencyLoopRequester high_freq_;

#ifdef USE_ESP_ADF
  vad_handle_t vad_instance_;
  uint8_t vad_threshold_{5};
  uint8_t vad_counter_{0};
#endif
};

template<typename... Ts> class DirectionAction : public Action<Ts...>, public Parented<InterCom> {
  TEMPLATABLE_VALUE(std::string, wake_word);

 public:
  void play(Ts... x) override { this->parent_->set_direction(this->state_); }

  void set_state(Direction state) { this->state_ = state; }

 protected:
  Direction state_{Direction::NONE};
};

template<typename... Ts> class IsRunningCondition : public Condition<Ts...>, public Parented<InterCom> {
 public:
  void set_state(Direction state) { this->state_ = state; }
  bool check(Ts... x) override { return this->parent_->is_running(this->state_); }

 protected:
  Direction state_{Direction::NONE};
};

extern InterCom *global_intercom;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

}  // namespace intercom
}  // namespace esphome
