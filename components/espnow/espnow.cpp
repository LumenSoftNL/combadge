#include "espnow.h"

#include <string.h>

#include "esp_mac.h"
#include "esp_random.h"

#include "esp_wifi.h"
#include "esp_crc.h"
#include "esp_now.h"
#include "esp_event.h"
#include "esphome/core/application.h"

#ifdef USE_WIFI
#include "esphome/components/wifi/wifi_component.h"
#endif

#include "esphome/core/version.h"
#include "esphome/core/log.h"

namespace esphome {
namespace espnow {

static const char *const TAG = "espnow";
static const size_t SEND_BUFFER_SIZE = 250;
static const auto half_period_usec = 1000000 / 100000 / 2;

inline void log_error_(std::string msg, esp_err_t err) { ESP_LOGE(TAG, msg.c_str(), esp_err_to_name(err)); }

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

ESPNowComponent::ESPNowComponent() { global_esp_now = this; }

void ESPNowComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "esp_now:");
  ESP_LOGCONFIG(TAG, "  MAC Address: " MACSTR, MAC2STR(ESPNOW_ADDR_SELF));
  // ESP_LOGCONFIG(TAG, "  WiFi Channel: %n", WiFi.channel());
}

bool ESPNowComponent::validate_channel_(uint8_t channel) {
  wifi_country_t g_self_country;
  esp_wifi_get_country(&g_self_country);
  if (channel >= g_self_country.schan + g_self_country.nchan) {
    ESP_LOGE(TAG, "Can't set channel %d, not allowed in country %c%c%c.", channel, g_self_country.cc[0],
             g_self_country.cc[1], g_self_country.cc[2]);
    return false;
  }
  return true;
}

void ESPNowComponent::setup() {
  ESP_LOGI(TAG, "Setting up ESP-NOW...");

#ifdef USE_WIFI
  global_wifi_component.disable();
#else  // Set device as a Wi-Fi Station
  esp_event_loop_create_default();

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();

  ESP_ERROR_CHECK(esp_wifi_init(&cfg));
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
  ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
  ESP_ERROR_CHECK(esp_wifi_start());
  ESP_ERROR_CHECK(esp_wifi_disconnect());

#endif
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

#ifdef CONFIG_ESPNOW_ENABLE_LONG_RANGE
  esp_wifi_get_protocol(ESP_IF_WIFI_STA, WIFI_PROTOCOL_LR);
#endif

  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(this->wifi_channel_, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);

  esp_err_t err = esp_now_init();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_now_init failed: %s", esp_err_to_name(err));
    this->mark_failed();
    return;
  }

  err = esp_now_register_recv_cb(ESPNowComponent::on_data_received);
  if (err != ESP_OK) {
    log_error_("esp_now_register_recv_cb failed: %s", err);
    this->mark_failed();
    return;
  }

  err = esp_now_register_send_cb(ESPNowComponent::on_data_sent);
  if (err != ESP_OK) {
    log_error_("esp_now_register_send_cb failed: %s", err);
    this->mark_failed();
    return;
  }

  esp_wifi_get_mac(WIFI_IF_STA, ESPNOW_ADDR_SELF);

  ESP_LOGI(TAG, "ESP-NOW add peers.");
  for (auto &address : this->peers_) {
    ESP_LOGI(TAG, "Add peer 0x%s .", format_hex(address).c_str());
    add_peer(address);
  }
  this->send_queue_ = xQueueCreate(SEND_BUFFER_SIZE, sizeof(ESPNowPacket *));

  xTaskCreate(ESPNowComponent::send_task, "send_task", 8192, (void *) this, 1, &this->send_task_handle_);

  ESP_LOGI(TAG, "ESP-NOW setup complete");
}

ESPNowPacket *ESPNowComponent::write(ESPNowPacket *packet) {
  if (200 <= this->send_queue_size()) {
    ESP_LOGE(TAG, "Send Buffer Out of memory (%d). Packet not added to the send queue. ", this->send_queue_size());
    delete packet;
    packet = nullptr;

  } else if (esp_now_is_peer_exist((uint8_t *) &packet->mac)) {
    ESP_LOGW(TAG, "Send Buffer Used: %d", this->send_queue_size());

    xQueueSendToBack(this->send_queue_, (void *) &packet, 1);
  } else {
    ESP_LOGW(TAG, "Peer does not exist! Can't send this packet: %02X:%02X:%02X:%02X:%02X:%02X", packet->mac[0],
             packet->mac[1], packet->mac[2], packet->mac[3], packet->mac[4], packet->mac[5]);
    packet->is_holded = 1;
  }
  return packet;
}

esp_err_t ESPNowComponent::add_peer(uint64_t addr) {
  if (!this->is_ready()) {
    this->peers_.push_back(addr);
    return ESP_OK;
  } else {
    uint8_t mac[6];
    this->del_peer(addr);

    esp_now_peer_info_t peerInfo = {};
    memset(&peerInfo, 0, sizeof(esp_now_peer_info_t));
    peerInfo.channel = this->wifi_channel_;
    peerInfo.encrypt = false;
    memcpy((void *) peerInfo.peer_addr, (void *) &addr, 6);

    return esp_now_add_peer(&peerInfo);
  }
}

esp_err_t ESPNowComponent::del_peer(uint64_t addr) {
  uint8_t mac[6];
  memcpy((void *) &mac, (void *) &addr, 6);
  if (esp_now_is_peer_exist((uint8_t *) &mac))
    return esp_now_del_peer((uint8_t *) &mac);
  return ESP_OK;
}

void ESPNowComponent::on_receive(ESPNowPacket *packet) {
  for (auto *protocol : this->protocols_) {
    if (protocol->on_receive(packet)) {
      return;
    }
  }
  this->on_receive_.call(packet);
}

void ESPNowComponent::on_sent(ESPNowPacket *packet) {
  for (auto *protocol : this->protocols_) {
    if (protocol->on_sent(packet)) {
      return;
    }
  }
  this->on_sent_.call(packet);
}

void ESPNowComponent::on_new_peer(ESPNowPacket *packet) {
  for (auto *protocol : this->protocols_) {
    if (protocol->on_new_peer(packet)) {
      return;
    }
  }
  this->on_new_peer_.call(packet);
}

void ESPNowComponent::unHold_send_(uint64_t mac) {
  ESPNowPacket *packet = this->first_send_packet();
  ESPNowPacket *front = packet;

  if (packet != nullptr) {
    while (1) {
      if (packet->is_holded && packet->mac64 == mac) {
        packet->clear_retrys();
        packet = this->next_send_packet();
        if (front == packet || packet == nullptr) {
          break;
        }
      }
    }
  }
}

void ESPNowComponent::loop() {
  while (!receive_queue_.empty()) {
    ESPNowPacket *packet = this->receive_queue_.front();
    this->receive_queue_.pop();

    if (!esp_now_is_peer_exist((uint8_t *) &packet->mac)) {
      if (this->auto_add_peer_) {
        this->add_peer(packet->mac64);
      } else {
        this->on_new_peer(packet);
      }
    }
    if (esp_now_is_peer_exist((uint8_t *) &packet->mac)) {
      this->unHold_send_(packet->mac64);
      this->on_receive(packet);
    } else {
      ESP_LOGW(TAG, "Peer does not exist can't handle this packet: %02X:%02X:%02X:%02X:%02X:%02X", packet->mac[0],
               packet->mac[1], packet->mac[2], packet->mac[3], packet->mac[4], packet->mac[5]);
    }
    App.feed_wdt();
  }
}

/**< callback function of receiving ESPNOW data */
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 1)
void ESPNowComponent::on_data_received(const esp_now_recv_info_t *recv_info, const uint8_t *data, int size)
#else
void ESPNowComponent::on_data_received(const uint8_t *addr, const uint8_t *data, int size)
#endif
{

  wifi_pkt_rx_ctrl_t *rx_ctrl = NULL;
  uint64_t daddr64 = 0;

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 1)
  uint8_t *addr = recv_info->src_addr;
  uint8_t *des_addr = recv_info->des_addr;
  memcpy((void *) &daddr64, (void *) des_addr, 6);

  rx_ctrl = recv_info->rx_ctrl;
#else
  wifi_promiscuous_pkt_t *promiscuous_pkt =
      (wifi_promiscuous_pkt_t *) (data - sizeof(wifi_pkt_rx_ctrl_t) - sizeof(espnow_frame_format_t));
  rx_ctrl = &promiscuous_pkt->rx_ctrl;
#endif
  uint64_t addr64 = 0;
  memcpy((void *) &addr64, (void *) addr, 6);
  ESPNowPacket *packet = new ESPNowPacket(addr64, (uint8_t *) data, (uint8_t) size);

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 1)
  packet->broadcast = (daddr64 == ESPNOW_BROADCAST_ADDR);
#endif

  packet->rssi = rx_ctrl->rssi;
  packet->timestamp = rx_ctrl->timestamp;
  global_esp_now->push_receive_packet_(packet);
}

void ESPNowComponent::send_task(void *params) {
  ESPNowComponent *this_ = (ESPNowComponent *) params;
  for (;;) {
    if (this_->send_queue_empty()) {
      App.feed_wdt();
      delayMicroseconds(half_period_usec * 2);
      continue;
    }
    ESPNowPacket *packet = this_->first_send_packet();
    if (packet != nullptr) {
      if (this_->try_lock_()) {
        ESPNowPacket *front = packet;
        while (packet->is_holded) {
          packet = this_->next_send_packet();
          if (front == packet || packet == nullptr) {
            break;
          }
        }

        if (packet != nullptr && !packet->is_holded) {
          ESP_LOGV(TAG, "_xYx_ (%p.%d)", packet, packet->retrys);
          packet->retry();
          if (packet->retrys == 6) {
            ESP_LOGW(TAG, "To many send retries. Stopping sending until new packet received.");
          } else {
            esp_err_t err = esp_now_send((uint8_t *) packet->mac, packet->data, packet->size);

            if (err != ESP_OK) {
              ESP_LOGI(TAG, "Packet send failed (%p.%d) Error: %s", packet, packet->retrys, esp_err_to_name(err));
              this_->next_send_packet();
              this_->unlock_();
              //          } else if (packet->is_broadcast()) {
              //            this_->next_send_packet();
              //            ESP_LOGI(TAG, "Packet (%p.%d) broadcasted.", packet, packet->get_counter());
              //            vTaskDelay(5 / portTICK_PERIOD_MS);
            } else {
              ESP_LOGI(TAG, "Send Packet (%p.%d). Wait for conformation.", packet, packet->retrys);
            }
          }
        } else {
          this_->unlock_();
        }
        continue;
      }
    }
    App.feed_wdt();
  }
}

void ESPNowComponent::on_data_sent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  ESPNowPacket *packet = global_esp_now->first_send_packet();

  if (packet == nullptr) {
    return;
  }

  ESP_LOGE(TAG, "Confirm sent (%p.%d). %s", packet, packet->retrys, esp_err_to_name(status));

  if (status != ESP_OK) {
    ESP_LOGE(TAG, "on_data_sent failed");
  } else if (std::memcmp(packet->mac, mac_addr, 6) != 0) {
    ESP_LOGE(TAG, "on_data_sent Invalid mac address.");
    ESP_LOGW(TAG, "expected: %02X:%02X:%02X:%02X:%02X:%02X", packet->mac[0], packet->mac[1], packet->mac[2],
             packet->mac[3], packet->mac[4], packet->mac[5]);
    ESP_LOGW(TAG, "returned: %02X:%02X:%02X:%02X:%02X:%02X", mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3],
             mac_addr[4], mac_addr[5]);
  } else {
    global_esp_now->on_sent(packet);
    global_esp_now->first_send_packet(true);
  }

  global_esp_now->unlock_();
}

ESPNowComponent *global_esp_now = nullptr;

}  // namespace espnow
}  // namespace esphome
