#pragma once

#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/core/preferences.h"

#include "../espnow/espnow.h"
#include "variables.h"

#include <array>
#include <memory>
#include <queue>
#include <vector>

namespace esphome::nowtalk {

static const uint8_t QUEUE_SIZE = 16;

std::string badgeID() bool checkBadgeID(std::string code);

struct NowTalkConfig {
  boolean wakeup = false;
  unsigned long lastTime = 0;
  unsigned long timerPing = 30000;  // send readings timer
  unsigned long timerSleep = 90000;
  bool registrationMode = false;
  char masterIP[40] = "<None>";
  char userName[64] = "<None>";
  uint8_t masterSwitchboard[6] = {0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  byte channel = 0;
  char switchboard[32] = "";
  int sprVolume = 40;
  int sleepID = -1;
  int PingID = -1;
  int timeoutID = -1;
  short heartbeat = 0;
  size_t updateSize = 0;
};

typedef struct {
  uint8_t mac[6];
  uint8_t code;
  char buf[256];
  size_t count;
} nowtalk_t;

typedef struct {
  uint8_t status;
  uint8_t mac[6];
  char name[64];
  char orginIP[32];
  byte prev;
  byte next;
} nowtalklist_t;

const byte _maxUserCount = 24;

typedef struct {
  byte first_element = 0;
  byte last_element = 0;
  byte current = 0xff;
  byte previous = 0xff;
  byte count = 0;
  const byte maxCount = _maxUserCount;
} nowtalklist_stats_t;

class NowTalkClient : public Component,
                      public Parented<espnow::ESPNowComponent>,
                      public espnow::ESPNowUnknownPeerHandler,
                      public espnow::ESPNowReceivedPacketHandler,
                      public espnow::ESPNowBroadcastedHandler {
 public:
  void setup() override;
  void dump_config() override;
  void loop() override;

  bool on_unknown_peer(const espnow::ESPNowRecvInfo &info, const uint8_t *data, uint8_t size) override;
  bool on_received(const espnow::ESPNowRecvInfo &info, const uint8_t *data, uint8_t size) override;
  bool on_broadcasted(const espnow::ESPNowRecvInfo &info, const uint8_t *data, uint8_t size) override;

  std::string get_value(std::string data, char separator, int index);

  esp_err_t send_message(const uint8_t *mac, uint8_t action, std::string message);
  esp_err_t broadcast(uint8_t action, std::string message);

  void clear_badge();
  void check_reaction();
  void on_no_reaction(AlarmID_t ID);
  void on_ping(AlarmID_t ID);

 protected:
  void load_config_(bool clear = false);
  void save_config_();
  std::string get_value_(std::string data, char separator, uint8_t index);
  void handle_package_(const uint8_t *mac, const uint8_t action, const char *info, size_t count);
  void write_buffer_(const uint8_t *mac, uint8_t *buf, size_t count)

#ifdef USE_UART
  void handle_uart_();
  void read_serial_();
  std::string inputString_ = "";  // a String to hold incoming data
  bool stringComplete_ = false;   // whether the string is complete
#endif

  ESPPreferenceObject cfg_;
  NowTalkConfig config_;  //

  nowtalk_t circbuf_[QUEUE_SIZE] = {};
  uint8_t write_idx_{0};
  uint8_t read_idx_{0};

  nowtalklist_stats_t stats;
  nowtalklist_t users[_maxUserCount];

  bool scan_of_master_{false};
  bool failed_to_send_packet_{false};
};

}  // namespace esphome::nowtalk
