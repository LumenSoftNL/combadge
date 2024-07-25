#include "espnow.h"

#include <string.h>

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 4, 0)
#include "esp_mac.h"
#include "esp_random.h"
#else
#include "esp_system.h"
#endif

#include <WiFi.h>
#include <esp_wifi.h>

#if defined(USE_ESP32)
#include <esp_now.h>
#elif defined(USE_ESP8266)
#include <ESP8266WiFi.h>
#include <espnow.h>
#endif
#include "esphome/core/application.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include "esphome/core/version.h"

namespace esphome {
namespace esp_now {

static const char *const TAG = "esp_now";

#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 0, 1)
typedef struct {
  uint16_t frame_head;
  uint16_t duration;
  uint8_t destination_address[6];
  uint8_t source_address[6];
  uint8_t broadcast_address[6];
  uint16_t sequence_control;

  uint8_t category_code;
  uint8_t organization_identifier[3];  // 0x18fe34
  uint8_t random_values[4];
  struct {
    uint8_t element_id;                  // 0xdd
    uint8_t lenght;                      //
    uint8_t organization_identifier[3];  // 0x18fe34
    uint8_t type;                        // 4
    uint8_t version;
    uint8_t body[0];
  } vendor_specific_content;
} __attribute__((packed)) espnow_frame_format_t;
#endif


void ESPNowInterface::setup() { parent_->register_protocol(this); }

ESPNowPackage::ESPNowPackage(const uint64_t mac_address, const std::vector<uint8_t> data) {
  this->mac_address_ = mac_address;
  this->data_ = data;
}

ESPNowPackage::ESPNowPackage(const uint64_t mac_address, const uint8_t *data, size_t len) {
  this->data_.resize(len);
  std::copy_n(data, len, this->data_.begin());
}


ESPNowComponent::ESPNowComponent() { global_esp_now = this; }

void ESPNowComponent::log_error_(std::string msg, esp_err_t err) { ESP_LOGE(TAG, msg.c_str(), esp_err_to_name(err)); }

void ESPNowComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "esp_now:");
  ESP_LOGCONFIG(TAG, "  Model: %s Rev %d\n", ESP.getChipModel(),
                  ESP.getChipRevision());
  ESP_LOGCONFIG(TAG, "  MAC Address: %s", WiFi.macAddress().c_str());
  //ESP_LOGCONFIG(TAG, "  WiFi Channel: %n", WiFi.channel());
}

void ESPNowComponent::setup() {
  ESP_LOGI(TAG, "Setting up ESP-NOW...");

  // Set device as a Wi-Fi Station
  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_AP_STA);
#ifdef CONFIG_ESPNOW_ENABLE_LONG_RANGE
  esp_wifi_get_protocol(ESP_IF_WIFI_STA, WIFI_PROTOCOL_LR);
#endif

  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(this->wifi_channel_, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);

#ifdef USE_ESP32
  esp_err_t err = esp_now_init();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_now_init failed: %s", esp_err_to_name(err));
    this->mark_failed();
    return;
  }

#elif defined(USE_ESP8266)
  int err = esp_now_init();
  if (err) {
    ESP_LOGE(TAG, "esp_now_init failed: %d", err);
    this->mark_failed();
    return;
  }

#endif
  err = esp_now_register_recv_cb(ESPNowComponent::on_data_received);
  if (err != ESP_OK) {
    this->log_error_("esp_now_register_recv_cb failed: %s", err);
    this->mark_failed();
    return;
  }

  err = esp_now_register_send_cb(ESPNowComponent::on_data_send);
  if (err != ESP_OK) {
    this->log_error_("esp_now_register_send_cb failed: %s", err);
    this->mark_failed();
    return;
  }

  ESP_LOGI(TAG, "ESP-NOW add peers.");
  for (auto &address : this->peers_) {
    ESP_LOGI(TAG, "Add peer 0x%s .", format_hex(address).c_str());
    add_peer(address);
  }
  ESP_LOGI(TAG, "ESP-NOW setup complete");
}

ESPNowPackage * ESPNowComponent::send_package(ESPNowPackage *package) {
  uint8_t mac[6];
  package->mac_bytes((uint8_t *) &mac);

  if (esp_now_is_peer_exist((uint8_t *) &mac)) {
    this->send_queue_.push(std::move(package));
  } else {
    ESP_LOGW(TAG, "Peer does not exist cant send this package: %02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2],
             mac[3], mac[4], mac[5]);
  }
  return package;
}

esp_err_t ESPNowComponent::add_peer(uint64_t addr) {
  if (!this->is_ready()) {
    this->peers_.push_back(addr);
    return ESP_OK;
  } else {
    uint8_t mac[6];
    this->del_peer(addr);

    uint64_to_addr(addr, (uint8_t*) &mac);

    esp_now_peer_info_t peerInfo = {};
    memset(&peerInfo, 0, sizeof(esp_now_peer_info_t));
    peerInfo.channel = this->wifi_channel_;
    peerInfo.encrypt = false;
    memcpy(peerInfo.peer_addr, mac, 6);

    return esp_now_add_peer(&peerInfo);
  }
}

esp_err_t ESPNowComponent::del_peer(uint64_t addr) {
  uint8_t mac[6];
  uint64_to_addr(addr, (uint8_t*) &mac);
  if (esp_now_is_peer_exist((uint8_t*)&mac))
    return esp_now_del_peer((uint8_t*)&mac);
  return ESP_OK;
}

void ESPNowComponent::on_package_received(ESPNowPackage *package) {
  for (auto *protocol : this->protocols_) {
    if (protocol->on_package_received(package)) {
      return;
    }
  }
  this->on_package_receved_.call(package);
}

void ESPNowComponent::on_package_send(ESPNowPackage *package) {
  for (auto *protocol : this->protocols_) {
    if (protocol->on_package_send(package)) {
      return;
    }
  }
  this->on_package_send_.call(package);
}

void ESPNowComponent::on_new_peer(ESPNowPackage *package) {
  for (auto *protocol : this->protocols_) {
    if (protocol->on_new_peer(package)) {
      return;
    }
  }
  this->on_new_peer_.call(package);
}

void ESPNowComponent::unHold_send_(uint64_t mac) {
  for (ESPNowPackage *package : this->send_queue_) {
    if (package->is_holded() && package->mac_address() == mac) {
      package->reset_counter();
    }
  }
}

void ESPNowComponent::loop() {
  if (!send_queue_.empty() && this->can_send_ && !this->status_has_warning()) {
    ESPNowPackage *package = this->send_queue_.front();
    ESPNowPackage *front = package;
    while (package->is_holded()) {
      this->send_queue_.pop();
      this->send_queue_.push(package);
      package = this->send_queue_.front();
      if (front == package)
        break;
    }

    if (!package->is_holded()) {
      if (package->get_counter() == 5) {
        this->status_set_warning("to many send retries. Stopping sending until new package received.");
      } else {
        uint8_t mac_address[6];
        package->mac_bytes((uint8_t *) &mac_address);
        esp_err_t err = esp_now_send(mac_address, package->data().data(), package->data().size());
        package->inc_counter();
        if (err != ESP_OK) {
          this->log_error_("esp_now_init failed: %s", err);
          this->send_queue_.pop();
          this->send_queue_.push(package);
        } else {
          this->can_send_ = false;
        }
      }
    }
  }

  while (!receive_queue_.empty()) {
    ESPNowPackage *package = std::move(this->receive_queue_.front());
    this->receive_queue_.pop();
    uint8_t mac[6];
    package->mac_bytes((uint8_t *) &mac);
    if (!esp_now_is_peer_exist((uint8_t *) &mac)) {
      if (this->auto_add_peer_) {
        this->add_peer(addr_to_uint64((uint8_t *) &mac));
      } else {
        this->on_new_peer(package);
      }
    }
    if (esp_now_is_peer_exist((uint8_t *) &mac)) {
      this->unHold_send_(package->mac_address());
      this->on_package_received(package);
    } else {
      ESP_LOGW(TAG, "Peer does not exist can't handle this package: %02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1],
               mac[2], mac[3], mac[4], mac[5]);
    }
  }
}

/**< callback function of receiving ESPNOW data */
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 1)
void ESPNowComponent::on_data_received((const esp_now_recv_info_t *recv_info, const uint8_t *data, int size)
#else
void ESPNowComponent::on_data_received(const uint8_t *addr, const uint8_t *data, int size)
#endif
{
  wifi_pkt_rx_ctrl_t *rx_ctrl = NULL;

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 1)
  uint8_t *addr = recv_info->src_addr;
  rx_ctrl = recv_info->rx_ctrl;
#else
  wifi_promiscuous_pkt_t *promiscuous_pkt =
      (wifi_promiscuous_pkt_t *) (data - sizeof(wifi_pkt_rx_ctrl_t) - sizeof(espnow_frame_format_t));
  rx_ctrl = &promiscuous_pkt->rx_ctrl;
#endif

  ESPNowPackage *package = new ESPNowPackage(addr_to_uint64(addr), data, size);

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 1)
  package->is_broadcast(memcmp(recv_info->des_addr, ESP_NOW.BROADCAST_ADDR, ESP_NOW_ETH_ALEN) == 0);
#endif

  package->rssi(rx_ctrl->rssi);
  package->timestamp(rx_ctrl->timestamp);
  global_esp_now->push_receive_package_(package);

}

void ESPNowComponent::on_data_send(const uint8_t *mac_addr, esp_now_send_status_t status) {
  ESPNowPackage *package = global_esp_now->send_queue_.front();
  Mac_Adress_t mac;
  uint64_to_addr(package->mac_address(), mac);
  if (status != ESP_OK) {
    ESP_LOGE(TAG, "on_data_send failed");
  } else if (std::memcmp(mac, mac_addr, 6) != 0) {
    ESP_LOGE(TAG, "on_data_send Invalid mac address.");
    ESP_LOGW(TAG, "expected: %02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1],
               mac[2], mac[3], mac[4], mac[5]);
    ESP_LOGW(TAG, "returned: %02X:%02X:%02X:%02X:%02X:%02X", mac_addr[0], mac_addr[1],
               mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
  } else {
    global_esp_now->on_package_send(package);
    global_esp_now->send_queue_.pop();
  }
  global_esp_now->can_send_ = true;
}


ESPNowComponent *global_esp_now = nullptr;

}  // namespace esp_now
}  // namespace esphome
