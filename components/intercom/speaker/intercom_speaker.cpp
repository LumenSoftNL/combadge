#include "intercom_speaker.h"

#include "esphome/core/application.h"
#include "esphome/core/log.h"
namespace esphome::intercom {

static const char *const TAG = "intercom_speaker";

size_t IntercomSpeaker::play(const uint8_t *data, size_t length) {
  if (this->is_stopped()) {
    this->start();
  }
  this->wdt_counter_++;
  size_t copied =this->parent_->ring_buffer()->write_without_replacement(data, length , pdMS_TO_TICKS(100));
  yield();
  return copied;
}

void IntercomSpeaker::start() {
  this->parent_->set_mode(intercom::Mode::MICROPHONE);
  this->state_ = speaker::STATE_RUNNING;
}

void IntercomSpeaker::stop() {
  this->state_ = speaker::STATE_STOPPING;
}

void IntercomSpeaker::loop() {
  if (this->wdt_counter_ > 250) {
    App.feed_wdt();
    this->wdt_counter_ = 0;
  }
  if (this->state_ == speaker::STATE_STOPPING && !this->has_buffered_data()) {
    this->parent_->set_mode(intercom::Mode::NONE);
    this->state_ = speaker::STATE_STOPPED;
  }
}

bool IntercomSpeaker::has_buffered_data() const { return (this->parent_->has_buffered_data()); }

}  // namespace esphome::intercom
