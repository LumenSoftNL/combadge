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


CODEOWNERS = ["@LumenSoftNL"]


CONF_MESHTEST = "meshtest"
CONF_ALLOW_BROADCAST = "allow_broadcast"
CONF_MESHMESH_ID = "meshmesh_id"

meshtest_ns = cg.esphome_ns.namespace("meshtest")
MeshTest = meshtest_ns.class_("MeshTest", cg.Component)

ModeAction = meshtest_ns.class_(
    "ModeAction", automation.Action, cg.Parented.template(MeshTest)
)

ChangeAddressAction = meshtest_ns.class_(
    "ChangeAddressAction", automation.Action, cg.Parented.template(MeshTest)
)


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(MeshTest),
            cv.Optional(CONF_MICROPHONE): microphone.microphone_source_schema(
                min_bits_per_sample=16,
                max_bits_per_sample=16,
                min_channels=1,
                max_channels=1,
            ),
            cv.Optional(CONF_SPEAKER): cv.use_id(speaker.Speaker),
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

    cg.add(var.set_broadcast_allowed(config.get(CONF_ALLOW_BROADCAST, False)))

    cg.add_define("USE_MESHTEST")


@register_action(
    "meshtest.mode",
    ModeAction,
    cv.maybe_simple_value(
        {
            cv.GenerateID(): cv.use_id(MeshTest),
            cv.Required(CONF_MODE): cv.boolean,
        },
        key=CONF_MODE,
    ),
)
async def meshtest_action_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    cg.add(var.set_send_frames(config[CONF_MODE]))

    return var


@register_action(
    "meshtest.address",
    ChangeAddressAction,
    cv.maybe_simple_value(
        {
            cv.GenerateID(): cv.use_id(MeshTest),
            cv.Required(CONF_ADDRESS): cv.hex_uint32_t,
        },
        key=CONF_ADDRESS,
    ),
)
async def meshtest_action_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    template_ = await cg.templatable(config[CONF_ADDRESS], args, cg.uint32)
    cg.add(var.set_address(template_))
    return var
