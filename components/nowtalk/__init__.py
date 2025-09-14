import esphome.codegen as cg
import esphome.config_validation as cv

from esphome import automation
from esphome.const import CONF_ID
from esphome.components.espnow import (
    ESPNOW_SCHEMA,
    register_espnow_extention,
    ESPNowReceivedPacketHandler,
)

CODEOWNERS = ["@LumenSoftNL"]
nowtalk_ns = cg.esphome_ns.namespace("nowtalk")

NowTalkComponent = nowtalk_ns.class_(
    "NowTalkComponent", cg.Component, ESPNowReceivedPacketHandler
)


CONF_NOWTALK = "NowTalk"
CONF_ON_PACKET_RECEIVED = "on_packet_received"
CONF_ON_PACKET_SEND = "on_packet_send"

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(NowTalkComponent),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(ESPNOW_SCHEMA),
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await register_espnow_extention(var, config)
    cg.add_define("USE_NOWTALK")
    cg.add_global(nowtalk_ns.using)
