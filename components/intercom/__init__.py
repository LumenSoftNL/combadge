import esphome.config_validation as cv
import esphome.codegen as cg

from esphome.const import (
    CONF_ID,
    CONF_MICROPHONE,
    CONF_SPEAKER,
    CONF_MODE,
)
from esphome import automation
from esphome.automation import register_action
from esphome.components import microphone, speaker, espnow

DEPENDENCIES = ["microphone", "speaker", "espnow"]

CODEOWNERS = ["@LumenSoftNL"]

CONF_AUTO_GAIN = "auto_gain"
CONF_NOISE_SUPPRESSION_LEVEL = "noise_suppression_level"
CONF_VOLUME_MULTIPLIER = "volume_multiplier"
CONF_SILENCE_DETECTION = "silence_detection"

CONF_ESPNOW = "espnow"

intercom_ns = cg.esphome_ns.namespace("intercom")
InterCom = intercom_ns.class_("InterCom", cg.Component, espnow.ESPNowReceivedPacketHandler)

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
            cv.Optional(
                CONF_MICROPHONE, default={}
            ): microphone.microphone_source_schema(
                min_bits_per_sample=16,
                max_bits_per_sample=16,
                min_channels=1,
                max_channels=1,
            ),
            cv.GenerateID(CONF_SPEAKER): cv.use_id(speaker.Speaker),

            cv.Optional(CONF_NOISE_SUPPRESSION_LEVEL, default=0): cv.int_range(0, 4),
            cv.Optional(CONF_AUTO_GAIN, default="0dBFS"): cv.All(
                cv.float_with_unit("decibel full scale", "(dBFS|dbfs|DBFS)"),
                cv.int_range(0, 31),
            ),
            cv.Optional(CONF_VOLUME_MULTIPLIER, default=1.0): cv.float_range(
                min=0.0, min_included=False
            ),

        }
    ).extend(cv.COMPONENT_SCHEMA),
)

FINAL_VALIDATE_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.Optional(
                CONF_MICROPHONE
            ): microphone.final_validate_microphone_source_schema(
                "InterCom", sample_rate=16000
            ),
        },
        extra=cv.ALLOW_EXTRA,
    ),
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    mic_source = await microphone.microphone_source_to_code(config[CONF_MICROPHONE])
    cg.add(var.set_microphone_source(mic_source))

    spkr = await cg.get_variable(config[CONF_SPEAKER])
    cg.add(var.set_speaker(spkr))

    cg.add(var.set_noise_suppression_level(config[CONF_NOISE_SUPPRESSION_LEVEL]))
    cg.add(var.set_auto_gain(config[CONF_AUTO_GAIN]))
    cg.add(var.set_volume_multiplier(config[CONF_VOLUME_MULTIPLIER]))

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
    "intercom.mode",
    IsModeCondition,
    INTERCOM_ACTION_SCHEMA
)
async def display_is_displaying_page_to_code(config, condition_id, template_arg, args):
    var = cg.new_Pvariable(condition_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    cg.add(var.set_mode(config[CONF_MODE]))
    return var
