import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.core import coroutine_with_priority

CODEOWNERS = ["@LumenSoftNL"]
nowtalk_ns = cg.esphome_ns.namespace("nowtalk")

from esphome import automation
import esphome.config_validation as cv
import esphome.codegen as cg
from esphome.const import CONF_ID
from esphome.components.espnow import espnow_ns, ESPNowListener, CONF_ESPNOW


CODEOWNERS = ["@LumenSoftNL"]


NowTalkComponent = espnow_ns.class_("NowTalkComponent", cg.Component, ESPNowListener)


CONF_NOWTALK = "NowTalk"
CONF_ON_PACKET_RECEIVED = "on_packet_received"
CONF_ON_PACKET_SEND = "on_packet_send"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(NowTalkComponent),

    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    trigger = cg.new_Pvariable(config[CONF_ESPNOW], var)
    cg.add(var.set_esphome(trigger))
    cg.add_define("USE_NOWTALK")
    cg.add_global(nowtalk_ns.using)
