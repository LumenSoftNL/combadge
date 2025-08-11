import esphome.codegen as cg
from esphome.components import speaker
import esphome.config_validation as cv
from esphome.const import (
    CONF_ID,
)

from .. import CONF_INTERCOM, InterCom, intercom_ns

AUTO_LOAD = ["audio"]
CODEOWNERS = ["@LumenSoftNL"]


IntercomSpeaker = intercom_ns.class_(
    "IntercomSpeaker", cg.Component, speaker.Speaker, cg.Parented.template(InterCom)
)

CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(IntercomSpeaker),
            cv.GenerateID(CONF_INTERCOM): cv.use_id(InterCom),
        }
    ),
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await cg.register_parented(var, config[CONF_INTERCOM])
