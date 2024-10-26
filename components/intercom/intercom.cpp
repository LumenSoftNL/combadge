#include "intercom.h"

#include "esphome/core/log.h"
#include "esp_log.h"
#include <cinttypes>
#include <cstdio>

namespace esphome {
namespace intercom {

static const char *const TAG = "intercom";

const size_t SEND_BUFFER_SIZE = 210;

InterCom::~InterCom() {
#ifdef USE_ESP_ADF
  if (this->vad_instance_ != nullptr) {
    vad_destroy(this->vad_instance_);
    this->vad_instance_ = nullptr;
  }
#endif
}

float InterCom::get_setup_priority() const { return setup_priority::AFTER_CONNECTION; }

void InterCom::setup() {
  ESP_LOGCONFIG(TAG, "Setting up Voice Assistant...");
  this->parent_->register_protocol(this);
}

bool InterCom::on_receive(espnow::ESPNowPacket &packet) {
  if (this->mode_ == Mode::SPEAKER && this->speaker_ != nullptr &&
      (this->mic_ == nullptr || this->mic_->is_stopped())) {
    this->speaker_->play(packet.get_payload(), packet.size);
    this->set_timeout("playing", 2000, [this]() { this->set_mode(Mode::NONE); });
  } else {
    this->set_mode(Mode::SPEAKER);
  }
  return true;
}

void InterCom::set_mode(Mode direction) {
  this->mode_ = direction;
  if (mode_ == Mode::SPEAKER) {
    if (this->speaker_ == nullptr) {
      this->mode_ = Mode::NONE;
    }
    if (this->mic_ != nullptr && this->mic_->is_running()) {
      this->mic_->stop();
    }
  } else if (this->mode_ == Mode::MICROPHONE) {
    if (this->mic_ == nullptr) {
      this->mode_ = Mode::NONE;
    }
    if (this->speaker_ != nullptr && this->speaker_->is_running()) {
      this->speaker_->finish();
    }

  } else {
    if (this->mic_ != nullptr && this->mic_->is_running()) {
      this->mic_->stop();
    }
    if (this->speaker_ != nullptr && this->speaker_->is_running()) {
      this->speaker_->finish();
    }
  }
}

bool InterCom::is_in_mode(Mode direction) {
  switch (direction) {
    case Mode::MICROPHONE:
      return (this->mic_ != nullptr && this->mic_->is_running());
    case Mode::SPEAKER:
      return (this->speaker_ != nullptr && this->speaker_->is_running());
    default:
      return (this->mic_ == nullptr || this->mic_->is_stopped()) &&
             (this->speaker_ == nullptr || this->speaker_->is_stopped());
  }
};

void InterCom::loop() {
  if (this->mode_ == Mode::MICROPHONE) {
    if (this->speaker_ != nullptr && this->speaker_->is_stopped()) {
      if (this->mic_->is_stopped()) {
        this->mic_->start();
      }
    }
    if (this->mic_->is_running()) {  // Read audio into input buffer
      this->read_microphone_();
    }
  }
}

void InterCom::read_microphone_() {
  size_t bytes_read = 0;
  uint8_t buffer[SEND_BUFFER_SIZE];

  bytes_read = this->mic_->read((int16_t *) &buffer, SEND_BUFFER_SIZE);
  if (bytes_read > 0) {
    this->send(0, (uint8_t *) &buffer, bytes_read);
  }
}

InterCom *global_intercom = nullptr;

}  // namespace intercom
}  // namespace esphome
