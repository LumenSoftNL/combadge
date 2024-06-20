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
static const uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 0, 1)
typedef struct {
    uint16_t frame_head;
    uint16_t duration;
    uint8_t destination_address[6];
    uint8_t source_address[6];
    uint8_t broadcast_address[6];
    uint16_t sequence_control;

    uint8_t category_code;
    uint8_t organization_identifier[3]; // 0x18fe34
    uint8_t random_values[4];
    struct {
        uint8_t element_id;                 // 0xdd
        uint8_t lenght;                     //
        uint8_t organization_identifier[3]; // 0x18fe34
        uint8_t type;                       // 4
        uint8_t version;
        uint8_t body[0];
    } vendor_specific_content;
} __attribute__((packed)) espnow_frame_format_t;
#endif

ESPNowPacket::ESPNowPacket(const uint64_t mac_address, const std::vector<uint8_t> data) {
  this->mac_address_ = mac_address;
  this->data_ = data;
}

ESPNowPacket::ESPNowPacket(const uint8_t *mac_address, const uint8_t *data, int len) {
  memcpy( &this->mac_address_ , mac_address, 6);
  this->data_.resize(len);
  std::copy_n(data, len, this->data_.begin());
}

ESPNowComponent::ESPNowComponent() { global_esp_now = this; }

void ESPNowComponent::log_error_(std::string msg, esp_err_t err) {
   ESP_LOGE(TAG, msg.c_str(), esp_err_to_name(err));
}


esp_err_t ESPNowComponent::add_user_peer(uint8_t *addr) {
  if (esp_now_is_peer_exist(addr))
    esp_now_del_peer(addr);

  esp_now_peer_info_t peerInfo = {};
  peerInfo.channel = this->wifi_channel_;
  peerInfo.encrypt = false;
  memcpy(peerInfo.peer_addr, addr, 6);
  return esp_now_add_peer(&peerInfo);
}

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

void ESPNowComponent::dump_config() { ESP_LOGCONFIG(TAG, "esp_now:"); }


void ESPNowComponent::on_packet_received(ESPNowPacket packet) {
  for (auto *listener : this->listeners_) {
    if (listener->on_packet_received(*packet)) {
      break;
    }
  }
  this->on_packet_receved_.call(*packet);
}

void ESPNowComponent::send_packet(const ESPNowPacket * packet) { global_esp_now->push_send_package.push(std::move(packet)); }


void ESPNowComponent::on_packet_send(ESPNowPacket packet) {
  for (auto *listener : this->listeners_) {
    if (listener->on_packet_send(*packet)) {
      break;
    }
  }
  this->on_packet_send_.call(*packet);
}

void ESPNowComponent::loop() {
  if (!send_queue_.empty() && this->can_send_ && !this->status_has_warning()) {
    auto packet = this->receive_queue_.front();
    if (packet->get_send_count() > 5) {
      this->status_set_warning("to many send retries. Stop sending until new package received.");
      pocket->reset_send_count();
    } else {
      uint8_t mac_address[6];
      if (packet->mac_address() == 0) {
        memcpy(mac_address, broadcastAddress, 6);
      } else {
        memcpy(mac_address, packet->mac_address(), 6);
      }

      if (!esp_now_is_peer_exist(mac_address)) {
        add_user_peer(mac_address);
      }

      esp_err_t err = esp_now_send(mac_address, packet->data(), packet->data()->size);
      if (err != ESP_OK) {
        this->log_error_("esp_now_init failed: %s", err);
      } else {
        packet->inc_send_count();
        this->can_send_ = false;
      }
    }
  }

  while (!receive_queue_.empty()) {
    std::unique_ptr<ESPNowPacket> packet = std::move(this->receive_queue_.front());
    this->receive_queue_.pop();

    ESP_LOGD(TAG, "mac: %s, data: %s", hexencode(packet->mac_address(), 6).c_str(), hexencode(packet->data()).c_str());
    on_packet_received(packet);
  }
}

*/

/**< callback function of receiving ESPNOW data */
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 1)
void ESPNowComponent::on_data_received((const esp_now_recv_info_t *recv_info, const uint8_t *data, int size)
#else
void ESPNowComponent::on_data_received(const uint8_t *addr, const uint8_t *data, int size)
#endif
{
    wifi_pkt_rx_ctrl_t *rx_ctrl = NULL;

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 1)
    uint8_t * addr = recv_info->src_addr;
    rx_ctrl = recv_info->rx_ctrl;
#else
    wifi_promiscuous_pkt_t *promiscuous_pkt = (wifi_promiscuous_pkt_t *)(data - sizeof(wifi_pkt_rx_ctrl_t) - sizeof(espnow_frame_format_t));
    rx_ctrl = &promiscuous_pkt->rx_ctrl;
#endif

  auto packet = new ESPNowPacket(addr, data, size);

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 1)
  packet->is_broadcast(memcmp(recv_info->des_addr, ESP_NOW.BROADCAST_ADDR, ESP_NOW_ETH_ALEN) == 0);
#endif

  packet->rssi(rx_ctrl->rssi);
  packet->timestamp(rx_ctrl->timestamp);
  global_esp_now->push_receive_packet(packet);

}

void ESPNowComponent::on_data_send(const uint8_t *mac_addr, esp_now_send_status_t status) {
  auto packet = global_esp_now->send_queue_.front();
  if (status != ESP_OK) {
    this->log_error_(TAG, "on_data_send failed: %s", status);
  } else if (std::memcmp(&packet->mac_address(), mac_addr, 6) != 0) {
    this->log_error_(TAG, "Invalid mac address: %s", status);
  } else {
    on_packet_send(packet);

    global_esp_now->send_queue_.pop();
  }

  this->can_send_ = true;
}


ESPNowComponent *global_esp_now = nullptr;

}  // namespace esp_now
}  // namespace esphome
