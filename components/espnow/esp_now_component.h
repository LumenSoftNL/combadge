#pragma once

#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"

#include <esp_now.h>

#include <array>
#include <memory>
#include <queue>
#include <vector>

namespace esphome {
namespace esp_now {

class ESPNowComponent;

class ESPNowPacket {
 public:
  ESPNowPacket(uint64_t mac_address, std::vector<uint8_t> data);
  ESPNowPacket(uint8_t *mac_address, const uint8_t *data, int len);

  uint64_t mac_address() { return mac_address_; }
  std::vector<uint8_t> data() { return data_; }

  uint8_t get_counter() { return send_count_; }
  void inc_counter() { send_count_ = send_count_ + 1; }
  void reset_counter() { send_count_ = 0; }

  void broadcast(bool value) { this->broadcast_ = value; }
  bool broadcast() const { return this->broadcast_; }

  void timestamp(uint32_t value) { this->timestamp_ = value; }
  uint32_t timestamp() { return this->timestamp_; }

  void rssi(int8_t rssi) { this->rssi_ = rssi; }
  int8_t rssi() { return this->rssi_; }

 protected:
  uint64_t mac_address_{0};
  std::vector<uint8_t> data_;

  uint8_t send_count_{0};
  bool broadcast_{false};
  uint32_t timestamp_{0};
  uint8_t rssi_{0};
};

class ESPNowListener : public Parented<ESPNowComponent> {
 public:
  virtual bool on_packet_received(ESPNowPacket packet) = 0;
  virtual bool on_packet_send(ESPNowPacket packet) { return false; };
};

class ESPNowComponent : public Component {
 public:
  ESPNowComponent();
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::AFTER_CONNECTION; }

  void set_wifi_channel(uint8_t channel) { this->wifi_channel_ = channel; }

  void send_packet(const uint8_t *mac_address, const uint8_t *data, int len) {
    auto packet = new ESPNowPacket(mac_address, data, len);
    send_packet(packet);
  }

  void send_packet(const uint64_t mac_address, const std::vector<uint8_t> data) {
    auto packet = ESPNowPacket(mac_address, data);
    send_packet(packet);
  }

  void send_packet(const ESPNowPacket packet) { global_esp_now->send_queue_.push(std::move(packet)); }

  static void on_data_received(const esp_now_recv_info_t *esp_now_info, const uint8_t *data, int data_len);
  static void on_data_send(const uint8_t *mac_addr, esp_now_send_status_t status);

  void add_on_packet_send_callback(std::function<void(ESPNowPacket)> &&callback) {
    this->on_packet_send_.add(std::move(callback));
  }

  void add_on_packet_receive_callback(std::function<void(ESPNowPacket)> &&callback) {
    this->on_packet_receved_.add(std::move(callback));
  }

  void register_listener(ESPNowListener *listener) {
    listener->set_parent(this);
    this->listeners_.push_back(listener);
  }

  virtual esp_err_t add_user_peer(uint8_t *addr);

  virtual void on_packet_received(ESPNowPacket packet);
  virtual void on_packet_send(ESPNowPacket packet);

 protected:
  void log_error_(char *msg, esp_err_t err);
  esp_err_t create_broatcast_peer();
  uint8_t wifi_channel_;

  CallbackManager<void(ESPNowPacket)> on_packet_send_;
  CallbackManager<void(ESPNowPacket)> on_packet_receved_;

  std::queue<ESPNowPacket> receive_queue_;
  std::queue<ESPNowPacket> send_queue_;

  std::vector<ESPNowListener *> listeners_;
  bool can_send_{true};
};

template<typename... Ts> class SendAction : public Action<Ts...>, public Parented<ESPNowComponent> {
 public:
  void set_data_template(std::function<std::vector<uint8_t>(Ts...)> func) {
    this->data_func_ = func;
    this->is_templated_data_ = true;
  }
  void set_data_static(std::vector<uint8_t> data) { this->data_static_ = data; }

  void set_mac_address_template(std::function<uint64_t(Ts...)> func) {
    this->bssid_func_ = func;
    this->is_templated_mac_address_ = true;
  }
  void set_mac_address(uint64_t mac_address) { this->mac_address_ = mac_address; }

  void play(Ts... x) override {
    if (this->is_templated_mac_address_) {
      mac_address_ = this->mac_address_func_(x...);
    }

    if (this->is_templated_data_) {
      data_ = this->data_func_(x...);
    }
    this->parent_->send_packet(mac_address_, data_);
  }

 protected:
  bool is_templated_mac_address_{false};
  std::function<uint64_t(Ts...)> mac_address_func_{};
  uint64_t mac_address_{0};

  bool is_templated_data_{false};
  std::function<std::vector<uint8_t>(Ts...)> data_func_{};
  std::vector<uint8_t> data_{};
};

class ESPNowSendTrigger : public Trigger<ESPNowPacket> {
 public:
  explicit ESPNowSendTrigger(ESPNowComponent *parent) {
    parent->add_on_packet_send_callback([this](ESPNowPacket value) { this->trigger(value); });
  }
};

class ESPNowReceiveTrigger : public Trigger<ESPNowPacket> {
 public:
  explicit ESPNowReceiveTrigger(ESPNowComponent *parent) {
    parent->add_on_packet_receive_callback([this](ESPNowPacket value) { this->trigger(value); });
  }
};

extern ESPNowComponent *global_esp_now;

}  // namespace esp_now
}  // namespace esphome
