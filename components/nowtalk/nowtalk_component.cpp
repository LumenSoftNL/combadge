#include "nowtalk_component.h"

#include <string.h>

namespace esphome {
namespace nowtalk {

bool NowTalkComponent::on_packet_send(ESPNowPacket *packet) { return false; };


bool NowTalkComponent::on_packet_received(ESPNowPacket *packet) {
    return false;
};

}  // namespace esp_now
}  // namespace esphome
