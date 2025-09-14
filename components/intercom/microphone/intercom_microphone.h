#pragma once

#include "esphome/components/microphone/microphone.h"
#include "esphome/core/component.h"


namespace esphome::intercom {

class InterComMicrophone : public microphone::Microphone, public Component, public Parented<intercom::InterCom> {
 public:
  void setup() override;
  void dump_config() override;
  void start() override;
  void stop() override;

  void loop() override;

 protected:

};

}  // namespace esphome
