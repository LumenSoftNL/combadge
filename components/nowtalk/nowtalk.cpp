#include "nowtalk.h"

#include <string.h>

namespace esphome::nowtalk {

static const char *const TAG = "nowtalk";
static const char const BADGECHARS[] = "0123456789AbCdEfGhIjKlMnOpQrStUvWxYz";  // aBcDeFgHiJkLmNoPqRsTuVwXyZ

static const char *const NOWTALK_HEADER = "EnnT1";
static const uint8_t NOWTALL_HEADER_SIZE = 5;
static const char *const NOWTALK_SOS = "SoS";
static const uint8_t NOWTALK_SOS_SIZE = 3;

std::string badgeID_{};

uint64_t getEfuseMac(void) {
  uint64_t _chipmacid = 0LL;
  esp_efuse_mac_get_default((uint8_t *) (&_chipmacid));
  return _chipmacid;
}

std::string badgeID() {
  if (badgeID_.empty()) {
    uint8_t base = sizeof(BADGECHARS);

    uint32_t chipId = 0xa5000000;
    uint8_t crc = 0;
    uint64_t mac = getEfuseMac();
    for (int i = 0; i < 17; i = i + 8) {
      chipId |= ((mac >> (40 - i)) & 0xff) << i;
    }

    do {
      badgeID_.push_back(BADGECHARS[chipId % base]);  // Add on the left
      crc += chipId % base;
      chipId /= base;
    } while (chipId != 0);
    badgeID_.push_back(BADGECHARS[crc % base]);
  }
  return badgeID_;
}

bool checkBadgeID(std::string code) {
  uint8_t baseCount = BADGECHARS.size();
  uint8_t length = code.size();
  uint16_t count = 0;
  if (length != 8)
    return false;
  for (int8_t index = 6; index >= 0; index--) {
    char chr = code.at(index);
    size_t pos = BADGECHARS.find(chr);
    if (pos == 0) {
      return false;
    }
    count += pos;
  }
  uint8_t crc = count % baseCount;
  char chr = code.at(7);
  return crc == BADGECHARS.find(chr);
}

void NowTalkClient::setup() {
  this->load_config_(false);
  this->parent_->register_received_handler(this);
};

void NowTalkClient::dump_config() {
  ESP_LOGCONFIG(TAG, "NowTalk Client:");
  // ESP_LOGCONFIG(TAG, "  Buffer size: %d", RING_BUFFER_SIZE);
  ESP_LOGCONFIG(TAG, "  Model: %s Rev %d", ESP.getChipModel(), ESP.getChipRevision());
  ESP_LOGCONFIG(TAG, "  Version: %s", VERSION);
  ESP_LOGCONFIG(TAG, "  Username: %s", config.userName);
  ESP_LOGCONFIG(TAG, "  MasterIP: %s", format_mac_address_pretty(config.masterIP).c_str());
}

void NowTalkClient::loop() {
  if (this->read_idx_ != this->write_idx_) {
    nowtalk_t espnow = circbuf[this->read_idx_];
    handlePackage(espnow.mac, espnow.code, espnow.buf, espnow.count);
    this->read_idx_ = (this->read_idx_ + 1) % QUEUE_SIZE;
  }
  if (this->failed_to_send_packet_) {

  }
}

bool NowTalkClient::on_unknown_peer(const espnow::ESPNowRecvInfo &info, const uint8_t *data, uint8_t size) {
  if (this->scan_of_master_) {
    return this->on_received(const espnow::ESPNowRecvInfo &info, const uint8_t *data, uint8_t size);
  }
  return false;
}

bool NowTalkClient::on_broadcasted(const espnow::ESPNowRecvInfo &info, const uint8_t *data, uint8_t size) {
  if (memcmp(data, NOWTALK_HEADER, NOWTALK_HEADER_SIZE) == 0 &&
      memcmp(data + NOWTALK_HEADER_SIZE, NOWTALK_SOS, NOWTALK_SOS_SIZE) == 0) {
    this->write_buffer_(info.src_addr, data+NOWTALK_HEADER_SIZE+NOWTALK_SOS_SIZE, size- NOWTALK_HEADER_SIZE+NOWTALK_SOS_SIZE);
  }
}

bool NowTalkClient::on_received(const espnow::ESPNowRecvInfo &info, const uint8_t *data, uint8_t size) {
  static uint16_t old_counter_value = 0;
  uint16_t new_counter_value = 0;
  if (memcmp(data, NOWTALK_HEADER, NOWTALK_HEADER_SIZE) == 0) {
    this->write_buffer_(info.src_addr, data+NOWTALK_HEADER_SIZE, size- NOWTALK_HEADER_SIZE);
    return true;
  }
  return false;
}

void NowTalkClient::write_buffer_(const uint8_t *mac, uint8_t *buf, size_t count) {
  // copy to circular buffer
  int idx = this->write_idx_;
  this->write_idx_ = (this->write_idx_ + 1) % QUEUE_SIZE;
  if (idx == this->read_idx_) {
    this->read_idx_ = (this->read_idx_ + 1) % QUEUE_SIZE;
  }

  memcpy(circbuf[idx].mac, mac, 6);
  circbuf[idx].code = buf[0];
  memset(circbuf[idx].buf, 0, 255);
  if (count > 1) {
    memcpy(circbuf[idx].buf, buf + 1, count - 1);
  }
  circbuf[idx].count = count - 1;
}

void NowTalkClient::load_config_(bool clear) {
  if (clear) {
    memfill(&this->config_, sizeof(this->config_), 0);
    this->config_.timerPing = 30000;   // send readings timer
    this->config_.timerSleep = 90000;  // EVENT_INTERVAL_MS
    this->config_.sprVolume = 40;
    this->save_config_();
  } else {
    this->cfg_ = global_preferences->make_preference<NowTalkConfig>(this->get_object_id_hash());
    this->cfg_.load(&this->config_);
  }

  /*
   for (JsonObject elem : doc["alarms"].as<JsonArray>()) {
      int id = elem["id"];
      uint64_t nt = elem["nextTrigger"].as<uint64_t>();
      alarms[id].value = elem["time"];
      alarms[id].nextTrigger = nt;
      alarms[id].Mode.alarmType = elem["mode_alarmType"];
      alarms[id].Mode.isEnabled = elem["mode_isEnabled"];
      alarms[id].Mode.isOneShot = elem["mode_isOneShot"];
      alarms[id].onTickHandler = (OnTick_t) elem["trigger"].as<uint32_t>();
      Serial.printf("timeleft %lld\n", myEvents.timeNow() - nt);
      myEvents.updateNextTrigger(id);
    }
  */
}

// Saves the configuration to a file
void NowTalkClient::save_config_() {
  this->cfg_ = global_preferences->make_preference<NowTalkConfig>(this->get_object_id_hash());
  this->cfg_.save(&this->config_);
  this->sync();
  /*
  JsonArray data = doc.createNestedArray("alarms");
  for (uint8_t id = 0; id < dtNBR_ALARMS; id++) {
    if (alarms[id].Mode.alarmType != dtNotAllocated) {
      JsonObject data_0 = data.createNestedObject();
      data_0["id"] = id;
      data_0["time"] = alarms[id].value;
      data_0["nextTrigger"] = alarms[id].nextTrigger;
      data_0["mode_alarmType"] = alarms[id].Mode.alarmType;
      data_0["mode_isEnabled"] = alarms[id].Mode.isEnabled;
      data_0["mode_isOneShot"] = alarms[id].Mode.isOneShot;
      data_0["trigger"] = (uint32_t) alarms[id].onTickHandler;
    }
  }
  */
}

std::string NowTalkClient::get_value(std::string data, char separator, int index) {
  int found = 0;
  int strIndex[] = {0, -1};
  int maxIndex = data.length() - 1;

  for (int i = 0; i <= maxIndex && found <= index; i++) {
    if (data.charAt(i) == separator || i == maxIndex) {
      found++;
      strIndex[0] = strIndex[1] + 1;
      strIndex[1] = (i == maxIndex) ? i + 1 : i;
    }
  }

  return found > index ? data.substring(strIndex[0], strIndex[1]) : "";
}

#ifdef USE_UART

void NowTalkClient::handle_uart_() {
  inputString = inputString.substring(3);

  if (inputString.equals("clear")) {
    ClearBadge();
  } else if (inputString.startsWith("init~")) {
    if (config.registrationMode) {
      String checkID = this->get_value(inputString, '~', 1);
      String IP = this->get_value(inputString, '~', 2);
      String name = this->get_value(inputString, '~', 3);
      String network = this->get_value(inputString, '~', 4);
      String mac = this->get_value(inputString, '~', 5);

      uint16_t crc = this->get_value(inputString, '~', 6).toInt();

      String test = "nowTalkSrv!" + checkID + "~" + IP + "~" + name + "~" + network + "~" + mac;

      uint16_t checkCrc = crc16_le(0, (uint8_t const *) test.c_str(), test.length());
      //            uint16_t checkCrc crc16((uint8_t *)test.c_str(),
      //            test.length, 0x1021, 0, 0, false, false);
      if ((crc == checkCrc) && (checkID == badgeID())) {  //
        strlcpy(config.masterIP, IP.c_str(),
                sizeof(config.masterIP));  // <- destination's capacity
        strlcpy(config.userName, name.c_str(),
                sizeof(config.userName));  // <- destination's capacity
        strlcpy(config.switchboard, network.c_str(),
                sizeof(config.switchboard));  // <- destination's capacity

        size_t outputLength;
        unsigned char *decoded = base64_decode((const unsigned char *) mac.c_str(), mac.length(), &outputLength);
        memcpy(config.masterSwitchboard, decoded, 6);
        memcpy(currentSwitchboard, decoded, 6);
        free(decoded);
        saveConfiguration();
        Serial.print("# ACK");
        ShowMessage('*', ("Switchboard:\n%s\n\nCallname: %s"), config.switchboard, config.userName);
        config.registrationMode = false;
        return;
      } else {
        Serial.print("# NACK");
        ShowMessage(F("Wrong Badge Id!\nPress Enter Button."), '!');
      }
    }

  } else if (inputString.startsWith("info~")) {
    if (config.masterSwitchboard[0] != 0) {
      String mac = this->get_value(inputString, '~', 1);

      byte decoded[6];
      mac.replace(":", "");
      hexStringToBytes(mac.c_str(), decoded);
      boolean okay = memcmp(config.masterSwitchboard, (char *) decoded, 6) == 0;
      if (!okay) {
        Serial.println("# NACK");
        return;
      }
    }
    uint8_t mac[6];
    Serial.print("# ");
    serial_mac(WiFi.macAddress(mac));
    Serial.print('~');
    Serial.print(badgeID());
    Serial.print('~');
    Serial.print(config.masterIP);
    Serial.print('~');
    Serial.print(config.userName);
    Serial.print('~');
    Serial.print(VERSION);
    Serial.print('\n');
  } else {
    Serial.printf(("E ERROR: > %s<\n"), inputString.c_str());
  }
}

void NowTalkClient::read_serial_() {
  while (Serial.available()) {
    // get the new byte:
    char inChar = (char) Serial.read();
    if (inChar == '\r')
      continue;
    if (inChar == '\n') {
      if (inputString.startsWith("###")) {
        handleCommand();
      }
      // clear the string:
      inputString = "";

    } else {
      inputString += inChar;
    }
  }
}
#endif

esp_err_t NowTalkClient::send_message(const uint8_t *mac, uint8_t action, std::string message) {
  if (this->failed_to_send_packet_) {
    return espnow::ESP_ERR_ESPNOW_FAILED_TO_SEND;
  }
  message.insert(0,(char)(action));
  esp_err_t result = this->parent_->send(mac, message.data(), message.length(), [this](esp_err_t x) { this->failed_to_send_packet_ = x != ESP_OK; });
  return result;
}

esp_err_t NowTalkClient::broadcast(uint8_t action, std::string message) {
  esp_err_t result = send_message(ESPNOW_BROADCAST_ADDR, action, message);
  esp_now_del_peer(ESPNOW_BROADCAST_ADDR);
  return result;
}

void NowTalkClient::clear_badge() {
  ShowMessage(F("Okey,\n Clearing badge and\nreboot."), '!');
  delay(3000);
  loadConfiguration(true);
  Reboot();
}

void NowTalkClient::check_reaction() {
  if (config.heartbeat > 3) {
    esp_now_del_peer(currentSwitchboard);
    currentSwitchboard[0] = 0x00;
  }
  uint32_t i = config.timerPing * ((currentSwitchboard[0] == 0) ? 5 : 1);
  // Serial.printf("checkReaction %d %ld \n", i, config.timerPing);
  myEvents.timerOnce(i, OnPing);
}

void NowTalkClient::on_no_reaction(AlarmID_t ID) {
  checkReaction();
  if (config.wakeup)
    GoSleep();
}

void NowTalkClient::on_ping(AlarmID_t ID) {
  if (currentSwitchboard[0] == 0) {
    Serial.println(F("*  Search SwitchBoard"));
    broadcast(0x01, "");
  } else {
    send_message(currentSwitchboard, 0x01, "");
  }
  config.timeoutID = myEvents.timerOnce(500, OnNoReaction);
}

void NowTalkClient::handle_package_(const uint8_t *mac, const uint8_t action, const char *info, size_t count) {
  stdString test;
  String split = String(info);
  // char msg[255];

  debug('>', mac, action, info);

  switch (action) {
    case NOWTALK_CLIENT_PING: {
      // do noting. this is a call for a server. this is a client.
    } break;
    case NOWTALK_SERVER_PONG: {
      // turn off timeout
      bool isNewMaster = (currentSwitchboard[0] == 0);
      if (isNewMaster) {
        memcpy(currentSwitchboard, mac, 6);
        if (config.masterSwitchboard[0] == 0) {
          memcpy(config.masterSwitchboard, mac, 6);
          saveConfiguration();
        }
        add_peer(mac);
      }
      if (split.length() > 0) {
        String name = getValue(split, '~', 0);
        strlcpy(config.switchboard, name.c_str(), sizeof(config.switchboard));  // <- destination's capacity

        String date = getValue(split, '~', 1);
        if (!config.wakeup) {
          ShowMessage('*', "New switchboard:\n\n%s", config.switchboard);
        }
      }
      /*
      configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
      tm xtm = createTM(date.c_str());
      setTime(makeTime(xtm))
      */

      //  Serial.println(config.timerPing);
      myEvents.free(config.timeoutID);
      config.timeoutID = -1;
      myEvents.timerOnce(config.timerPing, OnPing);
      config.heartbeat = 0;

      if (config.wakeup) {
        GoSleep();
      }

    } break;

    case NOWTALK_SERVER_REQUEST_DETAILS: {
      add_peer(mac);
      if ((strcmp(config.masterIP, "<None>") != 0) && (strcmp(config.userName, "<None>") != 0)) {
        send_message(mac, NOWTALK_CLIENT_DETAILS,
                     String(config.masterIP) + "~" + String(config.userName) + "~" + badgeID());
      } else {
        send_message(mac, NOWTALK_CLIENT_NACK, "");
      }
      esp_now_del_peer(mac);
    } break;

    case NOWTALK_SERVER_NEW_IP: {
      String OldIP = getValue(split, '~', 0);
      String NewIP = getValue(split, '~', 1);
      if (strcmp(config.masterIP, OldIP.c_str()) == 0) {
        strlcpy(config.masterIP, NewIP.c_str(), sizeof(config.masterIP));  // <- destination's capacity

        send_message(mac, NOWTALK_CLIENT_ACK, "");
        ShowMessage('*', "Update IP to: \n%s", config.masterIP);
      } else {
        send_message(mac, NOWTALK_CLIENT_NACK, "");
      }
    } break;
    case NOWTALK_SERVER_NEW_NAME: {
      String OldName = getValue(split, '~', 0);
      String NewName = getValue(split, '~', 1);
      if (strcmp(config.userName, OldName.c_str()) == 0) {
        strlcpy(config.userName, NewName.c_str(), sizeof(config.userName));  // <- destination's capacity
        send_message(mac, NOWTALK_CLIENT_ACK, "");
        ShowMessage('*', "Update Name to: \n%s", config.userName);
      } else {
        send_message(mac, NOWTALK_CLIENT_NACK, "");
      }
    } break;
    default:
      ShowMessage('!', "Unknown msg from: \n%02x%02x%02x%02x%02x%02x\n [%02x] %s", mac[0], mac[1], mac[2], mac[3],
                  mac[4], mac[5], action, info);
  }
}

}  // namespace esphome::nowtalk
