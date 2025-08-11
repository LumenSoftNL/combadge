#include "intercom_speaker.h"

#include "esphome/core/application.h"
#include "esphome/core/log.h"


namespace esphome::intercom {

static const char *const TAG = "speaker_intercom";

size_t IntercomSpeaker::play(const uint8_t *data, size_t length) {
  if (this->is_stopped()) {
    this->start();
  }
  return this->parent_->buffer_audio(data, length);
}

void IntercomSpeaker::start() {
  this->parent_->set_mode(intercom::Mode::MICROPHONE);
  this->state_ = speaker::STATE_RUNNING;
}

void IntercomSpeaker::stop() {
//  this->parent_->set_mode(intercom::Mode::NONE);
  this->state_ = speaker::STATE_STOPPED;
 }

bool IntercomSpeaker::has_buffered_data() const { return (this->parent_->has_buffered_data()); }

}  // namespace esphome::intercom
