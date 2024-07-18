#pragma once

#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"

#include <array>
#include <memory>
#include <queue>
#include <vector>

namespace esphome {
namespace esp_now {

static const uint64_t broadcastAddress = 0xFFFFFFFFFFFF;

class ESPNowComponent;

class ESPNowPackage {
 public:
  ESPNowPackage(const uint64_t mac_address, const std::vector<uint8_t> data);
  ESPNowPackage(const uint8_t *mac_address, const uint8_t *data, int len);

  uint64_t mac_address() { return mac_address_; }

  void mac_bytes(uint8_t * mac_addres) {
    auto mac = this->mac_address_==0?broadcastAddress:this->mac_address_;
    memcpy(mac_addres, &mac,6);
  }

  std::vector<uint8_t> data() { return data_; }

  uint8_t get_counter() { return send_count_; }
  void inc_counter() {
    send_count_ = send_count_ + 1;
    if (send_count_ > 5 && !is_holded_) {
      set_holding();
    }
  }
  void reset_counter() {
    send_count_ = 0;
    del_holding();
  }

  void is_broadcast(bool value) { this->is_broadcast_ = value; }
  bool is_broadcast() const { return this->is_broadcast_; }

  void timestamp(uint32_t value) { this->timestamp_ = value; }
  uint32_t timestamp() { return this->timestamp_; }

  void rssi(int8_t rssi) { this->rssi_ = rssi; }
  int8_t rssi() { return this->rssi_; }

  bool is_holded() { return this->is_holded_; }
  void set_holding() {this->is_holded_ = true; }
  void del_holding() {this->is_holded_ = false; }
 protected:
  uint64_t mac_address_{0};
  std::vector<uint8_t> data_;

  uint8_t send_count_{0};
  bool is_broadcast_{false};
  uint32_t timestamp_{0};
  uint8_t rssi_{0};

  bool is_holded_{false};
};


class ESPNowInterface : public Parented<ESPNowComponent> {
 public:
  ESPNowInterface();

  virtual bool on_package_received(ESPNowPackage *package) { return false; };
  virtual bool on_package_send(ESPNowPackage *package) { return false; };
  virtual bool on_new_peer(ESPNowPackage *package) { return false; };
  virtual void send_package(const uint64_t mac_address, const std::vector<uint8_t> data) {
    parent_->send_package(mac_address, data);
  }
  virtual void add_peer(const uint64_t mac_address) {
    parent_->add_peer(mac_address);
  }
  virtual void del_peer(const uint64_t mac_address) {
    parent_->add_peer(mac_address);
  }
  void set_auto_add_user(bool value) {
    parent_->set_auto_add_user(value);
  }
};

class ESPNowComponent : public Component {
 public:
  ESPNowComponent();

#ifdef USE_ESP8266
  static void on_data_received(uint8_t *addr, uint8_t *data, uint8_t size);
#elif USE_ESP32
  #if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 1)
    static void on_data_received((const esp_now_recv_info_t *recv_info, const uint8_t *data, int size);
  #else
    static void on_data_received(const uint8_t *addr, const uint8_t *data, int size);
  #endif
#endif

  static void on_data_send(const uint8_t *mac_addr, esp_now_send_status_t status);

  void dump_config() override;
  float get_setup_priority() const override {
    return setup_priority::AFTER_CONNECTION; }

  void setup() override;

  void loop() override;
  void set_wifi_channel(uint8_t channel) {
    this->wifi_channel_ = channel;
  }

  ESPNowPackage send_package(const uint8_t *mac_address, const uint8_t *data, int len) {
    auto package = new ESPNowPackage(mac_address, data, len);
    this->send_package(package);
    return package;
  }

  ESPNowPackage send_package(const uint64_t mac_address, const std::vector<uint8_t> data) {
    auto package = new ESPNowPackage(mac_address, data);
    this->send_package(package);
    return package;
  }

  void send_package(ESPNowPackage * package);


  void add_on_package_send_callback(std::function<void(ESPNowPackage*)> &&callback) {
    this->on_package_send_.add(std::move(callback));
  }

  void add_on_package_receive_callback(std::function<void(ESPNowPackage*)> &&callback) {
    this->on_package_receved_.add(std::move(callback));
  }

  void add_on_peer_callback(std::function<void(ESPNowPackage*)> &&callback) {
    this->on_new_peer_.add(std::move(callback));
  }

  void register_protocol(ESPNowInterface *protocol) {
    protocol->set_parent(this);
    this->protocols_.push_back(protocol);
  }

  esp_err_t add_peer(uint64_t addr) {
    uint8_t[6] mac;
    memcpy(&addr, &mac , 6);
    return add_peer(&mac);
  }
  esp_err_t add_peer(uint8_t *addr);

  esp_err_t del_peer(uint64_t addr) {
    uint8_t[6] mac;
    memcpy(&addr, &mac , 6);
    return del_peer(&mac);
  }
  esp_err_t del_peer(uint8_t *addr);

  void set_auto_add_peer(bool value) { this->auto_add_peer_ = value; }

  void on_package_received(ESPNowPackage* package);
  void on_package_send(ESPNowPackage* package);
  void on_new_peer(ESPNowPackage* package);

  void log_error_(std::string msg, esp_err_t err);

 protected:
  void unHold_send_(uint64_t mac);
  void push_receive_package_(ESPNowPackage* package) {
    this->receive_queue_.push(std::move(package));
  }

  void push_send_package_(ESPNowPackage* package) {
    this->send_queue_.push(std::move(package));
  }


  uint8_t wifi_channel_{0};
  bool auto_add_peer_{false};

  CallbackManager<void(ESPNowPackage*)> on_package_send_;
  CallbackManager<void(ESPNowPackage*)> on_package_receved_;
  CallbackManager<void(ESPNowPackage*)> on_new_peer_;

  std::queue<ESPNowPackage*> receive_queue_;
  std::queue<ESPNowPackage*> send_queue_;

  std::vector<ESPNowInterface *> protocols_;
  bool can_send_{true};


};

template<typename... Ts> class SendAction : public Action<Ts...> {
 public:
  template<typename V> void set_mac(V mac) { this->mac_ = mac; }
  template<typename V> void set_data(V data) { this->data_ = data; }

  void play(Ts... x) override {
    auto mac = this->mac_.value(x...);
    auto data = this->data_.value(x...);
    global_esp_now->send_package(mac, data);
  }

 protected:
  TemplatableValue<uint64_t, Ts...> mac_{};
  TemplatableValue<std::vector<uint8_t>, Ts...> data_{};
}

template<typename... Ts> class NewPeerAction : public Action<Ts...> {
 public:
  template<typename V> void set_mac(V mac) { this->mac_ = mac; }
  void play(Ts... x) override {
    auto mac = this->mac_.value(x...);
    global_esp_now->add_peer(mac);
  }

 protected:
  TemplatableValue<uint64_t, Ts...> mac_{};
}

template<typename... Ts> class DelPeerAction : public Action<Ts...> {
 public:
  template<typename V> void set_mac(V mac) { this->mac_ = mac; }
  void play(Ts... x) override {
    auto mac = this->mac_.value(x...);
    global_esp_now->del_peer(mac);
  }

 protected:
  TemplatableValue<uint64_t, Ts...> mac_{};
}




class ESPNowSendTrigger : public Trigger<ESPNowPackage *> {
 public:
  explicit ESPNowSendTrigger(ESPNowComponent *parent) {
    parent_->add_on_package_send_callback([this](ESPNowPackage *value) { this->trigger(value); });
  }
};

class ESPNowReceiveTrigger : public Trigger<ESPNowPackage *> {
 public:
  explicit ESPNowReceiveTrigger(ESPNowComponent *parent) {
    parent_->add_on_package_receive_callback([this](ESPNowPackage *value) { this->trigger(value); });
  }
};

class ESPNowNewPeerTrigger : public Trigger<ESPNowPackage *> {
 public:
  explicit ESPNowNewPeerTrigger(ESPNowComponent *parent) {
    parent_->add_on_peer_callback([this](ESPNowPackage *value) { this->trigger(value); });
  }
};



extern ESPNowComponent *global_esp_now;

}  // namespace esp_now
}  // namespace esphome
