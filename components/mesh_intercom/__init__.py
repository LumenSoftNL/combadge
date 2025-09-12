import esphome.config_validation as cv
import esphome.codegen as cg

from esphome.const import (
    CONF_ADDRESS,
    CONF_ID,
    CONF_MICROPHONE,
    CONF_SPEAKER,
    CONF_MODE,
)
from esphome import automation
from esphome.automation import register_action
from esphome.components import microphone, speaker
from esphome.components.meshmesh import MeshmeshComponent


AUTO_LOAD = ["microphone", "speaker"]

CODEOWNERS = ["@LumenSoftNL"]


CONF_INTERCOM = "intercom"
CONF_ALLOW_BROADCAST = "allow_broadcast"
CONF_MESHMESH_ID = "meshmesh_id"

intercom_ns = cg.esphome_ns.namespace("intercom")
InterCom = intercom_ns.class_("InterCom", cg.Component)

ModeAction = intercom_ns.class_(
    "ModeAction", automation.Action, cg.Parented.template(InterCom)
)

ChangeAddressAction = intercom_ns.class_(
    "ChangeAddressAction", automation.Action, cg.Parented.template(InterCom)
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
            cv.Optional(CONF_MICROPHONE): microphone.microphone_source_schema(
                min_bits_per_sample=16,
                max_bits_per_sample=16,
                min_channels=1,
                max_channels=1,
            ),
            cv.Optional(CONF_SPEAKER): cv.use_id(speaker.Speaker),
            cv.Optional(CONF_MODE): cv.enum(MODE_ENUM, upper=True),
            cv.Optional(CONF_ALLOW_BROADCAST): cv.boolean,
            cv.GenerateID(CONF_MESHMESH_ID): cv.use_id(MeshmeshComponent),
            cv.Required(CONF_ADDRESS): cv.hex_uint32_t,
        }
    ).extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    meshmesh = await cg.get_variable(config[CONF_MESHMESH_ID])
    cg.add(var.set_parent(meshmesh))
    cg.add(var.set_address(config[CONF_ADDRESS]))

    if mic := config.get(CONF_MICROPHONE):
        mic_source = await microphone.microphone_source_to_code(mic)
        cg.add(var.set_microphone_source(mic_source))

    if output := config.get(CONF_SPEAKER):
        spkr = await cg.get_variable(output)
        cg.add(var.set_speaker(spkr))

    if value := config.get(CONF_MODE):
        cg.add(var.set_mode(value))

    cg.add(var.set_broadcast_allowed(config.get(CONF_ALLOW_BROADCAST, False)))

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


@automation.register_condition("intercom.mode", IsModeCondition, INTERCOM_ACTION_SCHEMA)
async def intercom_mode_change_action_code(config, condition_id, template_arg, args):
    var = cg.new_Pvariable(condition_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    cg.add(var.set_mode(config[CONF_MODE]))
    return var


@register_action(
    "intercom.address",
    ChangeAddressAction,
    cv.maybe_simple_value(
        {
            cv.GenerateID(): cv.use_id(InterCom),
            cv.Required(CONF_ADDRESS): cv.hex_uint32_t,
        },
        key=CONF_ADDRESS,
    ),
)
async def intercom_action_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    template_ = await cg.templatable(config[CONF_ADDRESS], args, cg.uint32)
    cg.add(var.set_frame(template_))
    cg.add(var.set_mode(config[CONF_MODE]))
    return var
