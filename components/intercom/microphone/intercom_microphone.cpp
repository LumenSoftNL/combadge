#include "intercom_microphone.h"

#include "esphome/core/log.h"

#include "esphome/components/audio/audio.h"

namespace esphome::intercom {

static const UBaseType_t MAX_LISTENERS = 16;

static const uint32_t READ_DURATION_MS = 16;

static const size_t TASK_STACK_SIZE = 4096;
static const ssize_t TASK_PRIORITY = 23;

static const char *const TAG = "intercom.microphone";

void InterComMicrophone::setup() { this->audio_stream_info_ = audio::AudioStreamInfo(16, 1, 160000); }

void InterComMicrophone::dump_config() { ESP_LOGCONFIG(TAG, "Intercom Speaker to Microphone output:\n"); }

void InterComMicrophone::start() {}

void InterComMicrophone::stop() {}

void InterComMicrophone::loop() {}

}  // namespace esphome::intercom
