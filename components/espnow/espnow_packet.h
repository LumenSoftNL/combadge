#pragma once

// #if defined(USE_ESP32)

#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"

#include <esp_now.h>

#include <array>
#include <memory>
#include <queue>
#include <vector>

namespace esphome {
namespace espnow {

typedef uint8_t espnow_addr_t[6];

static const uint64_t ESPNOW_BROADCAST_ADDR = 0xFFFFFFFFFFFF;
static espnow_addr_t ESPNOW_ADDR_SELF = {0};


template<typename... Args> std::string string_format(const std::string &format, Args... args) {
  int size_s = std::snprintf(nullptr, 0, format.c_str(), args...) + 1;  // Extra space for '\0'
  if (size_s <= 0) {
    return ("Error during formatting.");
  }
  auto size = static_cast<size_t>(size_s);
  std::unique_ptr<char[]> buf(new char[size]);
  std::snprintf(buf.get(), size, format.c_str(), args...);
  return std::string(buf.get(), buf.get() + size - 1);  // We don't want the '\0' inside
}



struct ESPNowPacket {
  union {
    uint64_t mac64;
    struct {
      uint8_t mac[6];
      uint8_t reserve;
      uint8_t group_a : 1;
      uint8_t group_b : 1;
      uint8_t group_c : 1;
      uint8_t group_d : 1;

      uint8_t group_e : 1;
      uint8_t group_f : 1;
      uint8_t group_g : 1;
      uint8_t group_h : 1;
    };
  };
  uint8_t data[250];
  union {
    uint64_t settings;
    struct {
      uint8_t zerobyte;
      uint8_t size;

      uint32_t timestamp;
      uint8_t rssi;

      uint8_t retrys : 3;
      uint8_t is_broadcast : 1;
    };
  };

  inline ESPNowPacket(const uint64_t mac_address, const uint8_t *udata, uint8_t isize) ESPHOME_ALWAYS_INLINE : settings(0) {
    mac64 = mac_address == 0 ? ESPNOW_BROADCAST_ADDR : mac_address;

    this->is_broadcast = this->mac64 == ESPNOW_BROADCAST_ADDR;
    size = std::min((uint8_t) 250, isize);

    std::memcpy((uint8_t *) udata, &data, size);
    data[size+1] = 0;
  }

  inline void get_mac(uint8_t *mac_addres) {
    std::memcpy(&mac64, mac_addres, 6);
  }

  inline void set_mac(uint8_t *mac_addres) {
    std::memcpy(mac_addres, &mac,  6);
  }


  void retry() {
    if (this->retrys < 7 ) {
      retrys = retrys + 1;
    }
  }

  inline std::string addr_to_str() {
    return string_format("{\"%02X:%02X:%02X:%02X:%02X:%02X\"}", mac[0], mac[1],
               mac[2], mac[3], mac[4], mac[5]);
  }


};

}
}
