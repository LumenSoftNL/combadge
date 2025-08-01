#include "intercom.h"

#include "esphome/core/log.h"

#include "esp_log.h"
#include <cinttypes>
#include <cstdio>

namespace esphome::intercom {

static const char *const TAG = "intercom";

static const size_t SAMPLE_RATE_HZ = 16000;
static const char *const INTERCOM_HEADER = "Talk1";
static const uint8_t INTERCOM_HEADER_SIZE = 5;
static const size_t SEND_BUFFER_SIZE = 210;
static const size_t RING_BUFFER_SAMPLES = 512 * SAMPLE_RATE_HZ / 1000;  // 512 ms * 16 kHz/ 1000 ms
static const size_t RING_BUFFER_SIZE = RING_BUFFER_SAMPLES * sizeof(int16_t);
static const size_t SEND_BUFFER_SAMPLES = 32 * SAMPLE_RATE_HZ / 1000;  // 32ms * 16kHz / 1000ms
static const size_t RECEIVE_SIZE = 1024;
static const size_t SPEAKER_BUFFER_SIZE = 16 * RECEIVE_SIZE;

InterCom::~InterCom() {}

float InterCom::get_setup_priority() const { return setup_priority::LATE - 10; }

void InterCom::setup() {
  ESP_LOGCONFIG(TAG, "Setting up Voice Assistant");
  espnow::global_esp_now->register_received_handler(this);
  espnow::global_esp_now->register_broadcasted_handler(this);

  this->mic_source_->add_data_callback([this](const std::vector<uint8_t> &data) {
    if (!this->wait_to_switch_) {
      std::shared_ptr<RingBuffer> temp_ring_buffer = this->ring_buffer_;
      if (this->ring_buffer_.use_count() > 1) {
        temp_ring_buffer->write((void *) data.data(), data.size());
      }
    }
  });

  if (this->ring_buffer_.use_count() == 0) {
    this->ring_buffer_ = RingBuffer::create(RING_BUFFER_SIZE);
    if (this->ring_buffer_.use_count() == 0) {
      ESP_LOGE(TAG, "Could not allocate ring buffer");
    }
  }
  this->target_stream_info_ = audio::AudioStreamInfo(16, 1, 16000);
  this->high_freq_.start();
}

void InterCom::speaker_start_() { this->speaker_->set_audio_stream_info(this->target_stream_info_); }

void InterCom::set_mode(Mode direction) {
  if (direction == Mode::SPEAKER) {
    if (this->mode_ == Mode::MICROPHONE && this->mic_source_->is_running()) {
      this->mic_source_->stop();
      this->ring_buffer_->reset();
      this->wait_to_switch_ = true;
      ESP_LOGI(TAG, "waiting for Speaker start");

    } else {
      ESP_LOGI(TAG, "Speaker started");
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
      this->ring_buffer_->reset();
      this->wait_to_switch_ = true;
    }
    if (this->mode_ == Mode::SPEAKER && this->speaker_->is_running()) {
      this->speaker_->stop();
      this->wait_to_switch_ = true;
    }
  }

  this->mode_ = direction;
}

bool InterCom::is_in_mode(Mode direction) {
  switch (direction) {
    case Mode::MICROPHONE:
      return (this->mic_source_->is_running());
    case Mode::SPEAKER:
      return (this->speaker_->is_running());
    default:
      return (this->mic_source_->is_stopped() && this->speaker_->is_stopped());
  }
};

void InterCom::loop() {
  if (wait_to_switch_) {
    if (!this->speaker_->is_stopped() || !this->mic_source_->is_stopped()) {
      ESP_LOGI(TAG, "x");

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
  if (this->mode_ == Mode::MICROPHONE && this->mic_source_->is_running()) {
    this->read_microphone_();
  }
}

void InterCom::read_microphone_() {
  size_t bytes_read = 0;
  uint8_t buffer[SEND_BUFFER_SIZE + INTERCOM_HEADER_SIZE];
  if (this->can_send_packet_) {
    size_t available = this->ring_buffer_->available();
    if (available >= 60) {
      memcpy(&buffer, INTERCOM_HEADER, INTERCOM_HEADER_SIZE);
      size_t bytes_read = this->ring_buffer_->read((void *) &buffer[INTERCOM_HEADER_SIZE], SEND_BUFFER_SIZE, 0);
      if (bytes_read > 0) {
        this->can_send_packet_ = false;
        espnow::global_esp_now->send(this->get_address(), (uint8_t *) &buffer, bytes_read + INTERCOM_HEADER_SIZE,
                                     [this](esp_err_t x) { this->can_send_packet_ = true; });
      }
    }
  }
}

bool InterCom::on_received(const espnow::ESPNowRecvInfo &info, const uint8_t *data, uint8_t size) {
  if (memcmp(data, INTERCOM_HEADER, INTERCOM_HEADER_SIZE) == 0) {
    if (this->mode_ == Mode::SPEAKER && !this->wait_to_switch_) {
      this->speaker_->play(data + INTERCOM_HEADER_SIZE, size - INTERCOM_HEADER_SIZE);
    }
    return true;
  }
  return false;
}

bool InterCom::on_broadcasted(const espnow::ESPNowRecvInfo &info, const uint8_t *data, uint8_t size) {
  if (memcmp(data, INTERCOM_HEADER, INTERCOM_HEADER_SIZE) == 0) {
    if (this->mode_ == Mode::SPEAKER && !this->wait_to_switch_) {
      this->speaker_->play(data + INTERCOM_HEADER_SIZE, size - INTERCOM_HEADER_SIZE);
    }
    return true;
  }
  return false;
}

}  // namespace intercom
