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

CONF_ON_START = "on_start"
CONF_ON_END = "on_end"
CONF_ON_ERROR = "on_error"

CONF_SILENCE_DETECTION = "silence_detection"
CONF_USE_WAKE_WORD = "use_wake_word"
CONF_VAD_THRESHOLD = "vad_threshold"

CONF_AUTO_GAIN = "auto_gain"
CONF_NOISE_SUPPRESSION_LEVEL = "noise_suppression_level"
CONF_VOLUME_MULTIPLIER = "volume_multiplier"


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
            cv.Optional(CONF_NOISE_SUPPRESSION_LEVEL, default=0): cv.int_range(0, 4),
            cv.Optional(CONF_AUTO_GAIN, default="0dBFS"): cv.All(
                cv.float_with_unit("decibel full scale", "(dBFS|dbfs|DBFS)"),
                cv.int_range(0, 31),
            ),
            cv.Optional(CONF_VOLUME_MULTIPLIER, default=1.0): cv.float_range(
                min=0.0, min_included=False
            ),
            cv.Optional(CONF_ON_START): automation.validate_automation(single=True),
            cv.Optional(CONF_ON_END): automation.validate_automation(single=True),
            cv.Optional(CONF_ON_ERROR): automation.validate_automation(single=True),
            cv.Optional(CONF_ON_IDLE): automation.validate_automation(single=True),
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

    cg.add(var.set_noise_suppression_level(config[CONF_NOISE_SUPPRESSION_LEVEL]))
    cg.add(var.set_auto_gain(config[CONF_AUTO_GAIN]))
    cg.add(var.set_volume_multiplier(config[CONF_VOLUME_MULTIPLIER]))

    if CONF_ON_IDLE in config:
        await automation.build_automation(
            var.get_idle_trigger(),
            [],
            config[CONF_ON_IDLE],
        )

    if CONF_ON_START in config:
        await automation.build_automation(
            var.get_start_trigger(), [], config[CONF_ON_START]
        )

    if CONF_ON_END in config:
        await automation.build_automation(
            var.get_end_trigger(), [], config[CONF_ON_END]
        )

    if CONF_ON_ERROR in config:
        await automation.build_automation(
            var.get_error_trigger(),
            [(cg.std_string, "code"), (cg.std_string, "message")],
            config[CONF_ON_ERROR],
        )

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
