import esphome.config_validation as cv
import esphome.codegen as cg

from esphome.const import (
    CONF_ID,
    CONF_MICROPHONE,
    CONF_SPEAKER,
    CONF_MODE,
    CONF_ON_IDLE,
)
from esphome import automation
from esphome.automation import register_action
from esphome.components import microphone, speaker, espnow
from esphome.core import CORE


DEPENDENCIES = ["microphone", "speaker", "espnow"]

CODEOWNERS = ["@LumenSoftNL"]

CONF_VAD_THRESHOLD = "vad_threshold"

intercom_ns = cg.esphome_ns.namespace("intercom")
InterCom = intercom_ns.class_("InterCom", cg.Component, espnow.ESPNowInterface)

ModeAction = intercom_ns.class_(
    "ModeAction", automation.Action, cg.Parented.template(InterCom)
)

IsModeCondition = intercom_ns.class_(
    "IsModeCondition", automation.Condition, cg.Parented.template(InterCom)
)

Mode = intercom_ns.enum("Mode", is_class=True)
MODE_ENUM = {
    "NONE": Mode.NONE,
    "MICROPHONE": Mode.MICROPHONE,
    "SPEAKER": Mode.SPEAKER,
}

CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(InterCom),
            cv.GenerateID(CONF_MICROPHONE): cv.use_id(microphone.Microphone),
            cv.GenerateID(CONF_SPEAKER): cv.use_id(speaker.Speaker),
            cv.Optional(CONF_VAD_THRESHOLD): cv.All(
                cv.requires_component("esp_adf"), cv.only_with_esp_idf, cv.uint8_t
            ),
        }
    ).extend(espnow.PROTOCOL_SCHEMA),
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    await espnow.register_protocol(var, config)

    mic = await cg.get_variable(config[CONF_MICROPHONE])
    cg.add(var.set_microphone(mic))

    spkr = await cg.get_variable(config[CONF_SPEAKER])
    cg.add(var.set_speaker(spkr))

    if (vad_threshold := config.get(CONF_VAD_THRESHOLD)) is not None:
        cg.add(var.set_vad_threshold(vad_threshold))

    cg.add_define("USE_INTERCOM")


INTERCOM_ACTION_SCHEMA = cv.maybe_simple_value(
    {
        cv.GenerateID(): cv.use_id(InterCom),
        cv.Required(CONF_MODE): cv.enum(MODE_ENUM, upper=True),
    },
    key=CONF_MODE,
)


@register_action(
    "intercom.mode",
    ModeAction,
    INTERCOM_ACTION_SCHEMA,
)
async def intercom_action_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    cg.add(var.set_mode(config[CONF_MODE]))

    return var


@automation.register_condition(
    "intercom.is_mode",
    IsModeCondition,
    INTERCOM_ACTION_SCHEMA
)
async def display_is_displaying_page_to_code(config, condition_id, template_arg, args):
    var = cg.new_Pvariable(condition_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    cg.add(var.set_mode(config[CONF_MODE]))
    return var
