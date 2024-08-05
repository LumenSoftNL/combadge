#include "intercom.h"

#include "esphome/core/log.h"

#include <cinttypes>
#include <cstdio>

namespace esphome {
namespace intercom {

static const char *const TAG = "intercom";

#ifdef SAMPLE_RATE_HZ
#undef SAMPLE_RATE_HZ
#endif

static const size_t SAMPLE_RATE_HZ = 16000;
static const size_t INPUT_BUFFER_SIZE = 32 * SAMPLE_RATE_HZ / 1000;  // 32ms * 16kHz / 1000ms
static const size_t BUFFER_SIZE = 512 * SAMPLE_RATE_HZ / 1000;
static const size_t SEND_BUFFER_SIZE = 128;

float InterCom::get_setup_priority() const { return setup_priority::AFTER_CONNECTION; }

void InterCom::setup() {
  ESP_LOGCONFIG(TAG, "Setting up Voice Assistant...");
  this->parent_->register_protocol(this);
}

bool InterCom::on_receive(espnow::ESPNowPacket *packet) {
  if (this->state_ == State::STREAMING_SPEAKER) {
    this->speaker_->play(packet->data, packet->size);
    this->set_timeout("playing", 2000, [this]() { this->set_mode(Mode::NONE); });
  } else {
    this->set_mode(Mode::SPEAKER);
  }
  return true;
}

bool InterCom::allocate_buffers_() {
  if (this->read_buffer_ != nullptr) {
    return true;  // Already allocated
  }

#ifdef USE_ESP_ADF
  this->vad_instance_ = vad_create(VAD_MODE_4);
#endif

  ExternalRAMAllocator<uint8_t> allocator(ExternalRAMAllocator<uint8_t>::ALLOW_FAILURE);
  this->read_buffer_ = allocator.allocate(INPUT_BUFFER_SIZE);
  if (this->read_buffer_ == nullptr) {
    ESP_LOGW(TAG, "Could not allocate read buffer");
    return false;
  }

  this->input_buffer_ = RingBuffer::create(BUFFER_SIZE);
  if (this->input_buffer_ == nullptr) {
    ESP_LOGW(TAG, "Could not allocate ring buffer");
    return false;
  }

  return true;
}

void InterCom::clear_buffers_() {
  if (this->read_buffer_ != nullptr) {
    memset(this->read_buffer_, 0, INPUT_BUFFER_SIZE);
  }
  if (this->input_buffer_ != nullptr) {
    input_buffer_->reset();
  }
  this->speaker_->flush();
}

void InterCom::deallocate_buffers_() {
  ExternalRAMAllocator<uint8_t> send_deallocator(ExternalRAMAllocator<uint8_t>::ALLOW_FAILURE);
  send_deallocator.deallocate(this->read_buffer_, INPUT_BUFFER_SIZE);
  this->read_buffer_ = nullptr;

  if (this->input_buffer_ != nullptr) {
    this->input_buffer_->reset();
    this->input_buffer_ = nullptr;
  }

#ifdef USE_ESP_ADF
  if (this->vad_instance_ != nullptr) {
    vad_destroy(this->vad_instance_);
    this->vad_instance_ = nullptr;
  }
#endif
}

void InterCom::set_mode(Mode direction) {
  if (direction == Mode::MICROPHONE) {
    ESP_LOGI(TAG, "Switch to Microphone mode");
    switch (this->state_) {
      case State::STARTING_MICROPHONE:
      case State::STREAMING_MICROPHONE:
        break;  // do nothing
      case State::STOPPING_MICROPHONE:
        this->set_state_(State::STOPPING_MICROPHONE, State::START_MICROPHONE);
        break;
      case State::STARTING_SPEAKER:
      case State::STREAMING_SPEAKER:
        this->set_state_(State::STOP_SPEAKER, State::START_MICROPHONE);
        break;
      case State::STOPPING_SPEAKER:
        this->set_state_(State::STOPPING_SPEAKER, State::START_MICROPHONE);
        break;
      default:
        this->set_state_(State::START_MICROPHONE);
    }
  } else if (direction == Mode::SPEAKER) {
    ESP_LOGI(TAG, "Switch to Speaker mode");

    switch (this->state_) {
      case State::STARTING_MICROPHONE:
      case State::STREAMING_MICROPHONE:
        this->set_state_(State::STOP_MICROPHONE, State::START_SPEAKER);
        break;
      case State::STOPPING_MICROPHONE:
        this->set_state_(State::STOPPING_MICROPHONE, State::START_SPEAKER);
        break;

      case State::STARTING_SPEAKER:
      case State::STREAMING_SPEAKER:
        break;  // do nothing
      case State::STOPPING_SPEAKER:
        this->set_state_(State::STOPPING_SPEAKER, State::START_SPEAKER);
        break;
      default:
        this->set_state_(State::START_SPEAKER);
    }
  } else {
    ESP_LOGI(TAG, "Switch to None");
    switch (this->state_) {
      case State::STARTING_MICROPHONE:
      case State::STREAMING_MICROPHONE:
        this->set_state_(State::STOP_MICROPHONE, State::IDLE);
        break;
      case State::STOPPING_MICROPHONE:
        this->set_state_(State::STOPPING_MICROPHONE, State::IDLE);
        break;

      case State::STARTING_SPEAKER:
      case State::STREAMING_SPEAKER:
        this->set_state_(State::STOP_SPEAKER, State::IDLE);
        break;
      case State::STOPPING_SPEAKER:
        this->set_state_(State::STOPPING_SPEAKER, State::IDLE);
        break;
      default:
        this->set_state_(State::IDLE);
    }
  }
}

bool InterCom::is_running(Mode direction) {
  switch (direction) {
    case Mode::MICROPHONE:
      return (this->mic_ != nullptr && this->mic_->is_running());
    case Mode::SPEAKER:
      return (this->speaker_ != nullptr && this->speaker_->is_running());
    default:
      return this->state_ != State::IDLE;
  }
};

void InterCom::loop() {
  if (!this->allocate_buffers_()) {
    this->status_set_error("Failed to allocate buffers");
    return;
  }

  switch (this->state_) {
    case State::START_MICROPHONE: {
      ESP_LOGD(TAG, "Starting Microphone");
      input_buffer_->reset();
      this->mic_->start();
      //this->high_freq_.start();
      this->set_state_(State::STARTING_MICROPHONE, State::STREAMING_MICROPHONE );
      break;
    }
    case State::STARTING_MICROPHONE:
      if (this->mic_->is_running()) {
        this->set_state_(this->desired_state_);
      }
      break;
    case State::STREAMING_MICROPHONE:
      this->read_microphone_();
      break;

    case State::STOP_MICROPHONE:
      if (this->mic_->is_running()) {
        this->mic_->stop();
        this->set_state_(State::STOPPING_MICROPHONE);
      } else {
        this->set_state_(this->desired_state_);
      }
      break;

    case State::STOPPING_MICROPHONE:
      if (this->mic_->is_stopped()) {
        this->set_state_(this->desired_state_);
      }
      break;

    case State::START_SPEAKER:
      ESP_LOGD(TAG, "Starting Speaker");
      this->speaker_->start();
     // this->high_freq_.start();
      this->set_state_(State::STARTING_SPEAKER, State::STREAMING_SPEAKER);
      break;

    case State::STARTING_SPEAKER:
      if (this->speaker_->is_running()) {
        this->set_state_(this->desired_state_);
      }
      break;

    case State::STREAMING_SPEAKER:
      break;

    case State::STOP_SPEAKER:
      this->speaker_->finish();
      this->cancel_timeout("playing");
      this->set_state_(State::STOPPING_SPEAKER);
      break;

    case State::STOPPING_SPEAKER:
      if (this->speaker_->is_stopped()) {
        this->set_state_(this->desired_state_);
      }
      break;

    default:
      break;
  }
}

void InterCom::read_microphone_() {
  size_t bytes_read = 0;
  if (this->mic_->is_running()) {  // Read audio into input buffer
    bytes_read = this->mic_->read((int16_t *) this->read_buffer_, SEND_BUFFER_SIZE);
    if (bytes_read > 0) {
#ifdef USE_ESP_ADFXX

      vad_state_t vad_state = vad_process(this->vad_instance_, this->read_buffer_, SAMPLE_RATE_HZ, VAD_FRAME_LENGTH_MS);
      if (vad_state == VAD_SPEECH) {
        if (this->vad_counter_ < this->vad_threshold_) {
          this->vad_counter_++;
        } else {
          ESP_LOGD(TAG, "VAD detected speech");

          // Reset for next time
          this->vad_counter_ = 0;
        }
      } else {
        if (this->vad_counter_ > 0) {
          this->vad_counter_--;
        }
      }
#endif
      this->parent_->write(0, this->read_buffer_, bytes_read);
    }
  }
}

static const LogString *state_to_string(State state) {
  switch (state) {
    case State::IDLE:
      return LOG_STR("IDLE");
    case State::START_MICROPHONE:
      return LOG_STR("START_MICROPHONE");
    case State::STARTING_MICROPHONE:
      return LOG_STR("STARTING_MICROPHONE");
    case State::STREAMING_MICROPHONE:
      return LOG_STR("STREAMING_MICROPHONE");
    case State::STOP_MICROPHONE:
      return LOG_STR("STOP_MICROPHONE");
    case State::STOPPING_MICROPHONE:
      return LOG_STR("STOPPING_MICROPHONE");

    case State::START_SPEAKER:
      return LOG_STR("START_SPEAKER");
    case State::STARTING_SPEAKER:
      return LOG_STR("STARTING_SPEAKER");
    case State::STREAMING_SPEAKER:
      return LOG_STR("STREAMING_SPEAKER");
    case State::STOP_SPEAKER:
      return LOG_STR("STOP_SPEAKER");
    case State::STOPPING_SPEAKER:
      return LOG_STR("STOPPING_SPEAKER");

    default:
      return LOG_STR("UNKNOWN");
  }
};

void InterCom::set_state_(State state) {
  State old_state = this->state_;
  this->state_ = state;
  ESP_LOGD(TAG, "State changed from %s to %s", LOG_STR_ARG(state_to_string(old_state)),
           LOG_STR_ARG(state_to_string(state)));
}

void InterCom::set_state_(State state, State desired_state) {
  State old_state = this->desired_state_;
  this->set_state_(state);
  this->desired_state_ = desired_state;
  ESP_LOGD(TAG, "- Desired state changed from %s to %s", LOG_STR_ARG(state_to_string(old_state)),
           LOG_STR_ARG(state_to_string(desired_state)));
}

InterCom *global_intercom = nullptr;

}  // namespace intercom
}  // namespace esphome
