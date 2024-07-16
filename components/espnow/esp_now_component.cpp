#include "esp_now_component.h"

#include <string.h>

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 4, 0)
#include "esp_mac.h"
#include "esp_random.h"
#else
#include "esp_system.h"
#endif

#include "esp_wifi.h"

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

ESPNowInterface::ESPNowInterface() {
    if (global_esp_now == nullprt) {
      auto espnow = new ESPNowComponent();
      App.register_component(espnow);
    }
    global_esp_now.register_protocol(this);
  }


ESPNowPackage::ESPNowPackage(const uint64_t mac_address, const std::vector<uint8_t> data) {
  this->mac_address_ = mac_address;
  this->data_ = data;
}

ESPNowPackage::ESPNowPackage(const uint8_t *mac_address, const uint8_t *data, int len) {
  memcpy(&this->mac_address_, mac_address, 6);
  this->data_.resize(len);
  std::copy_n(data, len, this->data_.begin());
}

ESPNowComponent::ESPNowComponent() { global_esp_now = this; }

void ESPNowComponent::log_error_(std::string msg, esp_err_t err) { ESP_LOGE(TAG, msg.c_str(), esp_err_to_name(err)); }

void ESPNowComponent::dump_config() { ESP_LOGCONFIG(TAG, "esp_now:"); }

void ESPNowComponent::setup() {
  ESP_LOGCONFIG(TAG, "Setting up ESP-NOW...");

  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));
  ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_start());
  ESP_ERROR_CHECK(esp_wifi_set_channel(this->wifi_channel_, WIFI_SECOND_CHAN_NONE));

  esp_err_t err = esp_now_init();
  if (err != ESP_OK) {
    this->log_error_("esp_now_init failed: %s", err);
    this->mark_failed();
    return;
  }

  esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_LR);

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

  ESP_LOGCONFIG(TAG, "ESP-NOW setup complete");
}


void ESPNowComponent::send_package(ESPNowPackage *package) {
  uint8_t[6] mac;
  package->mac_bytes(&mac);
  if (!esp_now_is_peer_exist(mac)) {
    if (this->auto_add_peer_) {
      this->add_peer(mac);
    } else {
      this->on_new_peer(package);
    }
  }
  if (esp_now_is_peer_exist(mac)) {
    this->push_send_package_(package);
  } else {
    this->log_error_("peer does not exist cant send this package:"  MACSTR, MAC2STR(mac));
  }
}

esp_err_t ESPNowComponent::add_peer(uint8_t *addr) {
  if (esp_now_is_peer_exist(addr))
    esp_now_del_peer(addr);

  esp_now_peer_info_t peerInfo = {};
  peerInfo.channel = this->wifi_channel_;
  peerInfo.encrypt = false;
  memcpy(peerInfo.peer_addr, addr, 6);
  return esp_now_add_peer(&peerInfo);
}

esp_err_t ESPNowComponent::del_peer(uint8_t *addr) {
  if (esp_now_is_peer_exist(addr))
    return esp_now_del_peer(addr);
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
  for(const auto & package : this->can_send_) {
    if (package->is_holding() && package->mac_address == mac) {
      package->reset_counter();
    }
  }
}

void ESPNowComponent::loop() {
  if (!send_queue_.empty() && this->can_send_ && !this->status_has_warning()) {
    auto package = this->send_queue_.first();
    auto first = package;
    while (package->is_holding()) {
      this->send_queue_.pop();
      this->send_queue_.push_back(package);
      package = this->send_queue_.first()
      if (first == package) break;
    }

    if (!package->is_holding()) {
      if (package->get_counter() = 5) {
        this->status_set_warning("to many send retries. Stopping sending until new package received.");
      } else {
        uint8_t mac_address[6];
        package->mac_bytes(mac_address);
        esp_err_t err = esp_now_send(mac_address, package->data().data(), package->data().size());
        package->inc_counter();
        if (err != ESP_OK) {
          this->log_error_("esp_now_init failed: %s", err);
          this->send_queue_.pop();
          this->send_queue_.push_back(package);
        } else {
          this->can_send_ = false;
        }
      }
    }
  }

  while (!receive_queue_.empty()) {
    ESPNowPackage *package = std::move(this->receive_queue_.pop());
    uint8_t[6] mac;
    package->mac_bytes(&mac);
    if (!esp_now_is_peer_exist(mac)) {
      if (this->auto_add_peer_) {
        this->add_peer(mac);
      } else {
        this->on_new_peer(package);
      }
    }
    if (esp_now_is_peer_exist(mac)) {
      this->unHold_send_(package->mac_address());
      this->on_package_received(package);
    } else {
      this->log_error_("peer does not exist can't handle this package:"  MACSTR, MAC2STR(mac));
    }
  }
}

/**< callback function of receiving ESPNOW data */
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 1)
void ESPNowComponent::on_data_received((const esp_now_recv_info_t *recv_info, const uint8_t *data, int size)
#else
void ESPNowComponent::on_data_received(const uint8_t * addr, const uint8_t * data, int size)
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

  auto package = new ESPNowPackage(addr, data, size);

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 1)
  package->is_broadcast(memcmp(recv_info->des_addr, ESP_NOW.BROADCAST_ADDR, ESP_NOW_ETH_ALEN) == 0);
#endif

  package->rssi(rx_ctrl->rssi);
  package->timestamp(rx_ctrl->timestamp);
  global_esp_now->push_receive_package_(package);

}

void ESPNowComponent::on_data_send(const uint8_t *mac_addr, esp_now_send_status_t status) {
  auto package = global_esp_now->send_queue_.pop();
  auto addr = package->mac_address();
  if (status != ESP_OK) {
    ESP_LOGE(TAG, "on_data_send failed");
    global_esp_now->send_queue_.push_back(package);

  } else if (std::memcmp(&addr, mac_addr, 6) != 0) {
    ESP_LOGE(TAG, "on_data_send Invalid mac address.");
  } else {
    global_esp_now->on_package_send(package);
  }
  global_esp_now->can_send_ = true;
}


ESPNowComponent *global_esp_now = nullptr;

}  // namespace esp_now
}  // namespace esphome
