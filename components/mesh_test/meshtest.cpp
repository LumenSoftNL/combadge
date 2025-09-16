#include "meshtest.h"

#include "esphome/core/application.h"
#include "esphome/core/log.h"

#include <algorithm>
#include <espmeshmesh.h>
#include <cinttypes>
#include <cstdio>

namespace esphome::meshtest {

static const char *const TAG = "meshtest";

static const uint8_t MESHTEST_HEADER_REQ = 0x36;
static const uint8_t MESHTEST_HEADER_REP = 0x38;

static const uint8_t MESHTEST_HEADER_SIZE = 1;

static const size_t SEND_BUFFER_SIZE = 4;

float MeshTest::get_setup_priority() const { return setup_priority::LATE - 10; }

void MeshTest::setup() {
  ESP_LOGCONFIG(TAG, "Setting up Voice Assistant");

  this->parent_->getNetwork()->addHandleFrameCb(
      std::bind(&MeshTest::handleFrame, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));

  this->high_freq_.start();
}

void MeshTest::dump_config() {
  ESP_LOGCONFIG(TAG, "Mesh MeshTest: V2");
}

void MeshTest::loop() {
  if (this->send_frames_) {
    this->send_packet_();
  }
}

void MeshTest::send_packet_() {
  size_t bytes_read = 0;
  uint8_t buffer[SEND_BUFFER_SIZE + MESHTEST_HEADER_SIZE + sizeof(packet_counter_) + 1];
  if (this->can_send_packet_) {
    uint8_t column = 0;
    memcpy(&buffer[column], &MESHTEST_HEADER_REQ, MESHTEST_HEADER_SIZE);
    column += MESHTEST_HEADER_SIZE;
    memcpy(&buffer[column], &this->packet_counter_, sizeof(uint16_t));
    column += sizeof(uint16_t);

    espmeshmesh::uint32toBuffer(buffer + column, 0x14233241);

    column += SEND_BUFFER_SIZE;
    this->packet_counter_++;
    this->can_send_packet_ = false;
    if (this->address_ != UINT32_MAX)
      this->parent_->getNetwork()->uniCastSendData((uint8_t *) &buffer, column, this->address_);
    else
      this->parent_->getNetwork()->broadCastSendData((uint8_t *) &buffer, column);
  }
}

bool MeshTest::validate_address_(uint32_t address) {
  if (this->address_ == UINT32_MAX) {
    return true;
  } else if (address == this->address_) {
    return true;
  }
  return false;
}

bool MeshTest::handle_received_(uint8_t *data, size_t size, uint32_t from) {
  uint16_t new_counter_value = 0;
  if (data[0] == MESHTEST_HEADER_REQ) {
    if (size <= MESHTEST_HEADER_SIZE + sizeof(uint16_t)) {
      ESP_LOGE(TAG, "packet size to small: %d vs %d", size, MESHTEST_HEADER_SIZE + sizeof(uint16_t));
      return false;
    }
    uint8_t column = MESHTEST_HEADER_SIZE;
    memcpy(&new_counter_value, data + column, sizeof(uint16_t));
    column += sizeof(uint16_t);
    uint8_t rep[4] = {0};
    rep[0] = MESHTEST_HEADER_REP;
    rep[1] = 0x06;
    espmeshmesh::uint16toBuffer(rep + 2, new_counter_value);

    if (new_counter_value != this->old_counter_value_) {
      ESP_LOGE(TAG, "packet counter missmatch: %d vs %d", new_counter_value, this->old_counter_value_);
      rep[1] = 0x15;
    }
    this->parent_->getNetwork()->uniCastSendData(rep, 4, from);

    ESP_LOGD(TAG, "Received N%X.%d: %s", from, new_counter_value, format_hex_pretty(data, size).c_str());

    this->old_counter_value_ = new_counter_value + 1;
    return true;

  } else if (data[0] == MESHTEST_HEADER_REP) {
    uint16_t cntr = espmeshmesh::uint16FromBuffer(data + 2);
    if (espmeshmesh::uint16FromBuffer(data + 2) == this->old_counter_value_ - 1) {
      ESP_LOGD(TAG, "Received N%X.%d: %s", from, cntr, format_hex_pretty(data, size).c_str());
      this->can_send_packet_ = true;
    }
    return true;
  }
  return false;
}

int8_t MeshTest::handleFrame(uint8_t *buf, uint16_t len, uint32_t from) {
  size_t lenx = std::min((size_t) len, (size_t) 10);
  ESP_LOGD(TAG, "handleFrame N%X: %s", from, format_hex_pretty(buf, lenx).c_str());
  if (this->validate_address_(from)) {
    bool result = this->handle_received_(buf, (size_t) len, from);
    return result ? HANDLE_UART_OK : FRAME_NOT_HANDLED;
  }
  return FRAME_NOT_HANDLED;
}

}  // namespace esphome::meshtest
