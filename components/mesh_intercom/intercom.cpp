#include "intercom.h"

#include "esphome/core/application.h"
#include "esphome/core/log.h"

#include <espmeshmesh.h>
#include <cinttypes>
#include <cstdio>

namespace esphome::intercom {

static const char *const TAG = "intercom";

static const size_t SAMPLE_RATE_HZ = 16000;

static const uint8_t INTERCOM_HEADER_REQ = 0x34;

static const uint8_t INTERCOM_HEADER_SIZE = 1;

static const size_t SEND_BUFFER_SIZE = 240;

static const size_t RING_BUFFER_SIZE = (1024 * SAMPLE_RATE_HZ / 1000) * sizeof(int16_t);

float InterCom::get_setup_priority() const { return setup_priority::LATE - 10; }

void InterCom::setup() {
  ESP_LOGCONFIG(TAG, "Setting up Voice Assistant");

  this->parent_->getNetwork()->addHandleFrameCb(
      std::bind(&InterCom::handleFrame, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));

  if (this->has_mic_source_()) {
    this->mic_source_->add_data_callback(
        [this](const std::vector<uint8_t> &data) { this->receive_audio(data.data(), data.size()); });
  }
  if (this->has_spr_source_()) {
    this->add_play_audio_callback([this](uint8_t *data, size_t size) { return this->speaker_->play(data, size); });
  }
  if (this->ring_buffer_mic_.use_count() == 0) {
    this->ring_buffer_mic_ = RingBuffer::create(RING_BUFFER_SIZE);
    if (this->ring_buffer_mic_.use_count() == 0) {
      ESP_LOGE(TAG, "Could not allocate ring buffer");
    }
  }

  this->target_stream_info_ = audio::AudioStreamInfo(16, 1, 16000);
  this->high_freq_.start();
}

void InterCom::dump_config() {
  ESP_LOGCONFIG(TAG, "Mesh Intercom: V2");
  ESP_LOGCONFIG(TAG, "  Buffer size: %d", RING_BUFFER_SIZE);
}

void InterCom::speaker_start_() {
  if (this->has_spr_source_()) {
    this->speaker_->set_audio_stream_info(this->target_stream_info_);
  }
}

size_t InterCom::buffer_audio(const uint8_t *data, size_t length) {
  size_t result = this->ring_buffer_mic_->write_without_replacement(data, length, pdMS_TO_TICKS(100));  //
  //  ESP_LOGI(TAG, "%5d,%6d,%5d,%5d ", this->ring_buffer_mic_->available(), this->ring_buffer_mic_->free(), length,
  //  result);
  return result;
}

void InterCom::set_mode(Mode direction) {
  if (this->has_mic_source_() && this->has_spr_source_()) {
    if (direction == Mode::SPEAKER) {
      if (this->mode_ == Mode::MICROPHONE && this->mic_source_->is_running()) {
        this->mic_source_->stop();
        this->ring_buffer_mic_->reset();
        this->wait_to_switch_ = true;
        ESP_LOGI(TAG, "waiting for Speaker start");

      } else {
        ESP_LOGI(TAG, "Start speaker.");
        this->speaker_start_();
      }
    } else if (direction == Mode::MICROPHONE) {
      if (this->mode_ == Mode::SPEAKER && this->speaker_->is_running()) {
        this->speaker_->stop();
        this->wait_to_switch_ = true;
        ESP_LOGI(TAG, "waiting for Mic start");
      } else {
        ESP_LOGI(TAG, "Mic started");
        this->mic_source_->start();
      }
    } else {
      if (this->mode_ == Mode::MICROPHONE && this->mic_source_->is_running()) {
        this->mic_source_->stop();
        this->ring_buffer_mic_->reset();
        ESP_LOGI(TAG, "reset buffer");
        this->wait_to_switch_ = true;
      }
      if (this->mode_ == Mode::SPEAKER && this->speaker_->is_running()) {
        this->speaker_->stop();
        this->wait_to_switch_ = true;
      }
    }
  }
  this->mode_ = direction;
}

bool InterCom::is_in_mode(Mode direction) {
  if (this->has_mic_source_() && this->has_spr_source_()) {
    switch (direction) {
      case Mode::MICROPHONE:
        return (this->mic_source_->is_running());
      case Mode::SPEAKER:
        return (this->speaker_->is_running());
      default:
        return (this->mic_source_->is_stopped() && this->speaker_->is_stopped());
    }
  } else {
    return direction == this->mode_;
  }
}

void InterCom::loop() {
  if (wait_to_switch_) {
    if (!this->speaker_->is_stopped() || !this->mic_source_->is_stopped()) {
      return;
    }
    this->wait_to_switch_ = false;
    if (this->mode_ == Mode::MICROPHONE) {
      ESP_LOGI(TAG, "Mic started in loop");
      this->mic_source_->start();
    }
    if (this->mode_ == Mode::SPEAKER) {
      ESP_LOGI(TAG, "Speaker started in loop");
      this->speaker_start_();
    }
  }
  this->send_audio_packet_();
  App.feed_wdt();
}

void InterCom::send_audio_packet_() {
  size_t bytes_read = 0;
  uint8_t buffer[SEND_BUFFER_SIZE + INTERCOM_HEADER_SIZE + sizeof(packet_counter_) + 2];
  if (this->can_send_packet_) {
    size_t available = this->ring_buffer_mic_->available();
    if (available > 0) {
      buffer[0] = INTERCOM_HEADER_REQ;
      buffer[1] = 2;
      espmeshmesh::uint16toBuffer(buffer + 2, this->packet_counter_++);

      size_t read_size = std::min(available, SEND_BUFFER_SIZE);
      size_t bytes_read = this->ring_buffer_mic_->read((void *) &buffer[4], read_size, pdMS_TO_TICKS(100));
      this->set_timeout("InterCom",600,[this]() {this->can_send_packet_ = true;})
      if (bytes_read > 0) {
        this->can_send_packet_ = false;
        if (this->address_ != UINT32_MAX)
          this->parent_->getNetwork()->uniCastSendData((uint8_t *) &buffer, column + 4, this->address_);
        else
          this->parent_->getNetwork()->broadCastSendData((uint8_t *) &buffer, column + 4);
      }
    }
  }
}

bool InterCom::validate_address_(uint32_t address) {
  if (this->address_ == UINT32_MAX) {
    // this->parent_->getNetwork()->lastCommandFromBroadcast()
    return true;
  } else if (address == this->address_) {
    return true;
  }
  return false;
}

bool InterCom::handle_received_(uint8_t *data, size_t size, uint32_t from) {
  uint16_t new_counter_value = 0;
  uint8_t reply[4] = {INTERCOM_HEADER_REQ, 0x03, 0, 0};
  if (data[1] == 2) {
    if (size < 4) {
      reply[1] = 0x83;
    } else {
      new_counter_value = espmeshmesh::uint16FromBuffer(data + 2);
      espmeshmesh::uint16toBuffer(reply + 2, new_counter_value);

      if (new_counter_value != this->old_counter_value_) {
        ESP_LOGE(TAG, "packet counter missmatch: %d vs %d", new_counter_value, this->old_counter_value_);
        reply[1] = 0x83;
      }
      this->old_counter_value_ = new_counter_value + 1;
    }
    this->parent_->getNetwork()->uniCastSendData(reply, 4, from);

    if (this->mode_ == Mode::SPEAKER && !this->wait_to_switch_ && size > 4) {
      this->speaker_->play(data + 4, size - 4);
    }

    return true;
  } else if (data[1] & 0x03 == 0x03) {
    if (espmeshmesh::uint16FromBuffer(data + 2) == this->packet_counter_-1) {
      this->cancel_timeout("InterCom");
      this->can_send_packet_ = true;
    }
  }
  return false;
}

int8_t InterCom::handleFrame(uint8_t *buf, uint16_t len, uint32_t from) {
  if (this->validate_address_(from) && (buf[0] == INTERCOM_HEADER_REQ)) {
    bool result = this->handle_received_(buf, (size_t) len, from);
    return result ? HANDLE_UART_OK : FRAME_NOT_HANDLED;
  }
  return FRAME_NOT_HANDLED;
}

}  // namespace esphome::intercom
