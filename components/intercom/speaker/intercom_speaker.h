#pragma once

#include "esphome/components/audio/audio.h"
#include "esphome/components/audio/audio_transfer_buffer.h"
#include "esphome/components/speaker/speaker.h"
#include "esphome/components/intercom/intercom.h"

#include "esphome/core/component.h"

namespace esphome::intercom {

static const size_t INTERIM_BUFFER_SIZE = 512;
class IntercomSpeaker : public Component, public speaker::Speaker, public Parented<intercom::InterCom> {
public:
  float get_setup_priority() const override { return esphome::setup_priority::DATA; }

  size_t play(const uint8_t *data, size_t length) override;

  void start() override;
  void stop() override;
  void loop() override;

  bool has_buffered_data() const override;
 protected:
  uint16_t wdt_counter_{0};
  uint8_t interim_buffer_[INTERIM_BUFFER_SIZE] {};

};

}  // namespace esphome::intercom_speaker
