#include "intercom_speaker.h"

#include "esphome/core/application.h"
#include "esphome/core/log.h"
namespace esphome::intercom {

static const char *const TAG = "intercom_speaker";

size_t IntercomSpeaker::play(const uint8_t *data, size_t length) {
  if (this->is_stopped()) {
    this->start();
  }
  size_t copied_size = 0;
  while (length > copied_size) {
    this->wdt_counter_++;
    size_t copy_size = std::min(length - copied_size, INTERIM_BUFFER_SIZE);
    memcpy((void *) &this->interim_buffer_, data + copied_size, copy_size);
    size_t copied = this->parent_->buffer_audio((uint8_t *) &this->interim_buffer_, copy_size);
    if (copied == 0) {
      break;
    }
    copied_size += copied;
    yield();
  }
  return copied_size;
}

void IntercomSpeaker::start() {
  this->parent_->set_mode(intercom::Mode::MICROPHONE);
  this->state_ = speaker::STATE_RUNNING;
}

void IntercomSpeaker::stop() {
  // this->parent_->set_mode(intercom::Mode::NONE);
  this->state_ = speaker::STATE_STOPPING;
}

void IntercomSpeaker::loop() {
  if (this->wdt_counter_ > 250) {
    App.feed_wdt();
    this->wdt_counter_ = 0;
  }
  if (this->state_ == speaker::STATE_STOPPING && !this->has_buffered_data()){
    this->state_ = speaker::STATE_STOPPED;
  }

}

bool IntercomSpeaker::has_buffered_data() const { return (this->parent_->has_buffered_data()); }

}  // namespace esphome::intercom
