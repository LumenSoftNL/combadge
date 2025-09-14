import esphome.codegen as cg
from esphome.components import microphone
import esphome.config_validation as cv
from esphome.const import (
    CONF_ID,
)

from .. import intercom_ns, InterCom, CONF_INTERCOM

CODEOWNERS = ["@LumenSoftNL"]
DEPENDENCIES = ["audio"]

InterComMicrophone = intercom_ns.class_(
    "InterComMicrophone",
    microphone.Microphone,
    cg.Component,
    cg.Parented.template(InterCom),
)


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(InterComMicrophone),
            cv.GenerateID(CONF_INTERCOM): cv.use_id(InterCom),
        }
    ),
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await cg.register_parented(var, config[CONF_INTERCOM])
