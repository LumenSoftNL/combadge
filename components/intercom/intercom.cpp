#include "intercom.h"

#include "esphome/core/application.h"
#include "esphome/core/log.h"

#include <cinttypes>
#include <cstdio>

namespace esphome::intercom {

static const char *const TAG = "intercom";

static const size_t SAMPLE_RATE_HZ = 16000;
static const char *const INTERCOM_HEADER = "NowTalk1";
static const uint8_t INTERCOM_HEADER_SIZE = 8;

static const size_t SEND_BUFFER_SIZE = 240;

static const size_t RING_BUFFER_SAMPLES = 512 * SAMPLE_RATE_HZ / 1000;  // 512 ms * 16 kHz/ 1000 ms
static const size_t RING_BUFFER_SIZE = RING_BUFFER_SAMPLES * sizeof(int16_t);
static const size_t SEND_BUFFER_SAMPLES = 32 * SAMPLE_RATE_HZ / 1000;  // 32ms * 16kHz / 1000ms
static const size_t RECEIVE_SIZE = 1024;
static const size_t SPEAKER_BUFFER_SIZE = 16 * RECEIVE_SIZE;

float InterCom::get_setup_priority() const { return setup_priority::LATE - 10; }

void InterCom::setup() {
  ESP_LOGCONFIG(TAG, "Setting up Voice Assistant");
  this->parent_->register_received_handler(this);
  this->parent_->register_broadcasted_handler(this);
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
  ESP_LOGCONFIG(TAG, "Configuration:");
  ESP_LOGCONFIG(TAG, "  Buffer size: ", RING_BUFFER_SIZE);
}

void InterCom::speaker_start_() {
  if (this->has_spr_source_()) {
    this->speaker_->set_audio_stream_info(this->target_stream_info_);
  }
}

size_t InterCom::buffer_audio(const uint8_t *data, size_t length) {
  std::shared_ptr<RingBuffer> temp_ring_buffer = this->ring_buffer_mic_;
  if (this->ring_buffer_mic_.use_count() > 1) {
    size_t result = temp_ring_buffer->write_without_replacement(data, length, 100);
    ESP_LOGI(TAG, "%5d,%6d,%5d,%5d ", temp_ring_buffer->available(), temp_ring_buffer->free(), length, result);
    return result;
  }
  return 0;
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
};

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
  if (this->is_in_mode(Mode::MICROPHONE)) {
    this->read_microphone_();
  }
  App.feed_wdt();
}

void InterCom::read_microphone_() {
  size_t bytes_read = 0;
  uint8_t buffer[SEND_BUFFER_SIZE + INTERCOM_HEADER_SIZE + 1];
  if (this->can_send_packet_) {
    size_t available = this->ring_buffer_mic_->available();
    if (available >= 64) {
      memcpy(&buffer, INTERCOM_HEADER, INTERCOM_HEADER_SIZE);
      size_t read_size = std::min(available, SEND_BUFFER_SIZE);
      size_t bytes_read = this->ring_buffer_mic_->read((void *) &buffer[INTERCOM_HEADER_SIZE], read_size, 100);
      if (bytes_read > 0) {
        this->can_send_packet_ = false;
        uint8_t *address = nullptr;
        espnow::peer_address_t addr;
        if (this->address_.has_value()) {
          addr = this->address_.value();
          address = addr.data();
        }
        this->parent_->send(address, (uint8_t *) &buffer, bytes_read + INTERCOM_HEADER_SIZE,
                            [this](esp_err_t x) { this->can_send_packet_ = true; });
      }
    }
  }
}

bool InterCom::validate_address(const uint8_t *address) {
  uint8_t *current_address = nullptr;
  auto addr = this->address_.value();
  if (this->address_.has_value()) {
    current_address = addr.data();
  }
  if (current_address == nullptr) {
    return true;
  } else if (memcmp(address, current_address, ESP_NOW_ETH_ALEN) == 0) {
    return true;
  }
  return false;
}

bool InterCom::on_received(const espnow::ESPNowRecvInfo &info, const uint8_t *data, uint8_t size) {
  if (this->validate_address(info.des_addr) && memcmp(data, INTERCOM_HEADER, INTERCOM_HEADER_SIZE) == 0) {
    if (this->mode_ == Mode::SPEAKER && !this->wait_to_switch_) {
      this->speaker_->play(data + INTERCOM_HEADER_SIZE, size - INTERCOM_HEADER_SIZE);
    }
    return true;
  }
  return false;
}

bool InterCom::on_broadcasted(const espnow::ESPNowRecvInfo &info, const uint8_t *data, uint8_t size) {
  if (this->validate_address(info.des_addr) && memcmp(data, INTERCOM_HEADER, INTERCOM_HEADER_SIZE) == 0) {
    if (this->mode_ == Mode::SPEAKER && !this->wait_to_switch_) {
      this->speaker_->play(data + INTERCOM_HEADER_SIZE, size - INTERCOM_HEADER_SIZE);
    }
    return true;
  }
  return false;
}

}  // namespace esphome::intercom
