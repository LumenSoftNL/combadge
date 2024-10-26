#include "nowtalk_component.h"

#include <string.h>

namespace esphome {
namespace nowtalk {

void NowTalkComponent::setup() {
  this->cfg_ = global_preferences->make_preference<nowTalkConfig>(this->get_object_id_hash());
  this->cfg_.load(&this->recovered);
};

void NowTalkComponent::on_receive(ESPNowPacket &packet) {};
void NowTalkComponent::on_sent(ESPNowPacket &packet, bool status) {};

std::string NowTalkComponent::get_value(std::string data, char separator, uint8_t index) {
  uint8_t found = 0;
  uint8_t strIndex[] = {0, -1};
  uint8_t maxIndex = data.length() - 1;

  for (uint8_t i = 0; i <= maxIndex && found <= index; i++) {
    if (data.charAt(i) == separator || i == maxIndex) {
      found++;
      strIndex[0] = strIndex[1] + 1;
      strIndex[1] = (i == maxIndex) ? i + 1 : i;
    }
  }

  return found > index ? data.substring(strIndex[0], strIndex[1]) : "";
}

void NowTalkComponent::load_config_(bool clear) {
  if (clear) {
    this->cfg_.reset()
  }
    SPIFFS.remove(configFile);
  // Allocate a temporary JsonDocument
  // Don't forget to change the capacity to match your requirements.
  // Use arduinojson.org/v6/assistant to compute the capacity.
  StaticJsonDocument<524> doc;

  // memset(*config, 0, sizeof(config));
  // Open file for reading
  if (SPIFFS.exists(configFile)) {
    fs::File file = SPIFFS.open(configFile);
    deserializeJson(doc, file);
    file.close();
  }
  //   serializeJsonPretty(doc, Serial);
  // Copy values from the JsonDocument to the Config
  config.channel = doc["channel"] | 0;
  // config.port = doc["port"] | 2731;
  strlcpy(config.switchboard, doc["switchboard"] | "", sizeof(config.switchboard));  // <- destination's capacity
  const char *mac = doc["switchboardMac64"].as<const char *>();
  //   Serial.print("\n{");
  // Serial.print(mac);
  //  Serial.print("}");
  // Serial.println(mac == nullptr);
  if (mac != nullptr) {
    size_t outputLength;

    unsigned char *decode = base64_decode((const unsigned char *) mac, strlen(mac), &outputLength);
    memcpy(config.masterSwitchboard, decode, 6);  // <- destination's capacity
    if (currentSwitchboard[0] == 0) {
      memcpy(currentSwitchboard, decode, 6);  // <- destination's capacity
    }
    free(decode);
  }
  config.timerPing = doc["timerPing"] | 30000;    // send readings timer
  config.timerSleep = doc["timerSleep"] | 90000;  // EVENT_INTERVAL_MS
  config.ledBacklight =
      doc["ledBacklight"] | 80;  // Initial TFT backlight intensity on a scale of 0 to 255. Initial value is 80.
  config.sprVolume = doc["sprVolume"] | 40;
  config.heartbeat = doc["heartbeat"] | 0;

  strlcpy(config.masterIP, doc["masterIP"] | "<None>", sizeof(config.masterIP));  // <- destination's capacity
  strlcpy(config.userName, doc["userName"] | "<None>", sizeof(config.userName));  // <- destination's capacity
  if ((strcmp(config.masterIP, "<None>") == 0) || (strcmp(config.userName, "<None>") == 0)) {
    config.registrationMode = true;
  }

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
}

// Saves the configuration to a file
void NowTalkComponent::save_config_() {

  // Copy values from the JsonDocument to the Config
  doc["channel"] = config.channel;
  // doc["port"] = config.port;
  doc["switchboard"] = config.switchboard;
  size_t outputLength;
  unsigned char *encode = base64_encode((const unsigned char *) config.masterSwitchboard, 6, &outputLength);
  doc["switchboardMac64"] = encode;
  free(encode);

  doc["timerPing"] = config.timerPing;    // send readings timer
  doc["timerSleep"] = config.timerSleep;  // EVENT_INTERVAL_MS

  doc["masterIP"] = config.masterIP;
  doc["userName"] = config.userName;
  doc["ledBacklight"] =
      config.ledBacklight;  // Initial TFT backlight intensity on a scale of 0 to 255. Initial value is 80.
  doc["sprVolume"] = config.sprVolume;

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
  // serializeJsonPretty(doc, Serial);
  if (serializeJson(doc, file) == 0) {
    Serial.println(F("Failed to write to file"));
  }

  // Close the file
  file.close();
}

}  // namespace nowtalk
}  // namespace esphome
