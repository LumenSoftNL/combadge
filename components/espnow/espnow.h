#pragma once

// #if defined(USE_ESP32)

#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "espnow_packet.h"
#include <esp_now.h>

#include <array>
#include <memory>
#include <queue>
#include <vector>
#include <mutex>

namespace esphome {
namespace espnow {


class ESPNowComponent;

template<typename T, typename Container = std::deque<T> > class iterable_queue : public std::queue<T, Container> {
 public:
  typedef typename Container::iterator iterator;
  typedef typename Container::const_iterator const_iterator;

  iterator begin() { return this->c.begin(); }
  iterator end() { return this->c.end(); }
  const_iterator begin() const { return this->c.begin(); }
  const_iterator end() const { return this->c.end(); }
};

class ESPNowInterface : public Component, public Parented<ESPNowComponent> {
 public:
  ESPNowInterface() {};

  void setup() override;

  virtual bool on_receive(ESPNowPacket *packet) { return false; };
  virtual bool on_sent(ESPNowPacket *packet) { return false; };
  virtual bool on_new_peer(ESPNowPacket *packet) { return false; };
};

class ESPNowComponent : public Component {
 public:
  ESPNowComponent();

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 1)
  static void on_data_received(const esp_now_recv_info_t *recv_info, const uint8_t *data, int size);
#else
  static void on_data_received(const uint8_t *addr, const uint8_t *data, int size);
#endif

  static void on_data_sent(const uint8_t *mac_addr, esp_now_send_status_t status);

  void dump_config() override;
  float get_setup_priority() const override { return -100; }

  void setup() override;

  void loop() override;
  void set_wifi_channel(uint8_t channel) { this->wifi_channel_ = channel; }

  ESPNowPacket *write(const uint64_t mac_address, const uint8_t *data, uint8_t len) {
    return this->write( new ESPNowPacket(mac_address, data, len));
  }

  ESPNowPacket *write(const uint64_t mac_address, const std::vector<uint8_t> data) {
    return this->write(new ESPNowPacket(mac_address, (uint8_t*) data.data(), (uint8_t) data.size()));
  }

  ESPNowPacket *write(ESPNowPacket * packet);

  void add_on_sent_callback(std::function<void(ESPNowPacket *)> &&callback) { this->on_sent_.add(std::move(callback)); }

  void add_on_receive_callback(std::function<void(ESPNowPacket *)> &&callback) {
    this->on_receive_.add(std::move(callback));
  }

  void add_on_peer_callback(std::function<void(ESPNowPacket *)> &&callback) {
    this->on_new_peer_.add(std::move(callback));
  }

  void register_protocol(ESPNowInterface *protocol) {
    protocol->set_parent(this);
    this->protocols_.push_back(protocol);
  }

  esp_err_t add_peer(uint64_t addr);
  esp_err_t del_peer(uint64_t addr);

  void set_auto_add_peer(bool value) { this->auto_add_peer_ = value; }

  void on_receive(ESPNowPacket *packet);
  void on_sent(ESPNowPacket *packet);
  void on_new_peer(ESPNowPacket *packet);

  bool send_queue_empty() {
    return uxQueueMessagesWaiting(this->send_queue_)==0;
  }
  size_t send_queue_size() {
    return uxQueueMessagesWaiting(this->send_queue_);
  }

  ESPNowPacket *first_send_packet(bool pop = false) {

    if (this->send_queue_empty()) {
      return nullptr;
    }

    //xQueueSendToFront(xQueue, pvItemToQueue, xTicksToWait)
    ESPNowPacket *packet;

    xQueuePeek(this->send_queue_,  &packet, 1);

    if (pop) {
      xQueueReceive(this->send_queue_, &packet, 1);
      delete packet;
      packet = nullptr;
    }
    return packet;
  }

  ESPNowPacket *next_send_packet() {
    if (this->send_queue_empty()) {
      return nullptr;
    }

    ESPNowPacket *packet;
    xQueueReceive(this->send_queue_,  &packet, 1);
    xQueueSendToBack(this->send_queue_, &packet, 1);

    return this->first_send_packet();
  }

 protected:
  void unHold_send_(uint64_t mac);
  void push_receive_packet_(ESPNowPacket *packet) { this->receive_queue_.push(std::move(packet)); }
  bool validate_channel_(uint8_t channel);
  uint8_t wifi_channel_{0};
  bool auto_add_peer_{false};

  CallbackManager<void(ESPNowPacket *)> on_sent_;
  CallbackManager<void(ESPNowPacket *)> on_receive_;
  CallbackManager<void(ESPNowPacket *)> on_new_peer_;

  static void send_task(void *params);
  TaskHandle_t send_task_handle_{nullptr};

  std::queue<ESPNowPacket *> receive_queue_{};
  QueueHandle_t send_queue_{};

  std::vector<ESPNowInterface *> protocols_{};
  std::vector<uint64_t> peers_{};

  Mutex send_lock_;
  void lock_() { this->send_lock_.lock(); }
  bool try_lock_() { return this->send_lock_.try_lock(); }
  void unlock_() { this->send_lock_.unlock(); }
 };

template<typename... Ts> class SendAction : public Action<Ts...>, public Parented<ESPNowComponent> {
 public:
  template<typename V> void set_mac(V mac) { this->mac_ = mac; }
  void set_data_template(std::function<std::vector<uint8_t>(Ts...)> func) {
    this->data_func_ = func;
    this->static_ = false;
  }
  void set_data_static(const std::vector<uint8_t> &data) {
    this->data_static_ = data;
    this->static_ = true;
  }

  void play(Ts... x) override {
    auto mac = this->mac_.value(x...);

    if (this->static_) {
      this->parent_->write(mac, this->data_static_);
    } else {
      auto val = this->data_func_(x...);
      this->parent_->write(mac, val);
    }
  }

 protected:
  TemplatableValue<uint64_t, Ts...> mac_{};
  bool static_{false};
  std::function<std::vector<uint8_t>(Ts...)> data_func_{};
  std::vector<uint8_t> data_static_{};
};

template<typename... Ts> class NewPeerAction : public Action<Ts...>, public Parented<ESPNowComponent> {
 public:
  template<typename V> void set_mac(V mac) { this->mac_ = mac; }
  void play(Ts... x) override {
    auto mac = this->mac_.value(x...);
    parent_->add_peer(mac);
  }

 protected:
  TemplatableValue<uint64_t, Ts...> mac_{};
};

template<typename... Ts> class DelPeerAction : public Action<Ts...>, public Parented<ESPNowComponent> {
 public:
  template<typename V> void set_mac(V mac) { this->mac_ = mac; }
  void play(Ts... x) override {
    auto mac = this->mac_.value(x...);
    parent_->del_peer(mac);
  }

 protected:
  TemplatableValue<uint64_t, Ts...> mac_{};
};

class ESPNowSentTrigger : public Trigger<ESPNowPacket *> {
 public:
  explicit ESPNowSentTrigger(ESPNowComponent *parent) {
    parent->add_on_sent_callback([this](ESPNowPacket *value) { this->trigger(value); });
  }
};

class ESPNowReceiveTrigger : public Trigger<ESPNowPacket *> {
 public:
  explicit ESPNowReceiveTrigger(ESPNowComponent *parent) {
    parent->add_on_receive_callback([this](ESPNowPacket *value) { this->trigger(value); });
  }
};

class ESPNowNewPeerTrigger : public Trigger<ESPNowPacket *> {
 public:
  explicit ESPNowNewPeerTrigger(ESPNowComponent *parent) {
    parent->add_on_peer_callback([this](ESPNowPacket *value) { this->trigger(value); });
  }
};

extern ESPNowComponent *global_esp_now;

}  // namespace espnow
}  // namespace esphome

// #endif
