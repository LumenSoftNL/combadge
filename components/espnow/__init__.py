from esphome import automation
import esphome.config_validation as cv
import esphome.codegen as cg
from esphome.const import CONF_ID, CONF_DATA, CONF_MAC_ADDRESS, CONF_TRIGGER_ID

CODEOWNERS = ["@jesserockz"]
DEPENDENCIES = ["wifi"]

esp_now_ns = cg.esphome_ns.namespace("esp_now")
ESPNowComponent = esp_now_ns.class_("ESPNowComponent", cg.Component)

ESPNowPacket = esp_now_ns.class_("ESPNowPacket")

ESPNowSendTrigger = esp_now_ns.class_("ESPNowSendTrigger", automation.Trigger.template())
ESPNowReceiveTrigger = esp_now_ns.class_("ESPNowReceiveTrigger", automation.Trigger.template())

SendAction = esp_now_ns.class_(
    "SendAction", automation.Action, cg.Parented.template(ESPNowComponent)
)

CONF_ESP_NOW = "esp_now"
CONF_ON_PACKET_RECEIVED = "on_packet_received"
CONF_ON_PACKET_SEND = "on_packet_send"

CONF_CHANNEL = "wifi_channel"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(ESPNowComponent),
        cv.Optional(CONF_CHANNEL, default=1): cv.int_range(1, 14),
        cv.Optional(CONF_ON_PACKET_RECEIVED): automation.validate_automation(
            {
                cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(ESPNowReceiveTrigger),
            }
        ),
        cv.Optional(CONF_ON_PACKET_SEND): automation.validate_automation(
            {
                cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(ESPNowSendTrigger),
            }
        ),

    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_wifi_channel(config[CONF_CHANNEL]))

    cg.add_define("USE_ESPNOW")

    for conf in config.get(CONF_ON_PACKET_SEND, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(trigger, [(ESPNowPacket, "packet")], conf)

    for conf in config.get(CONF_ON_PACKET_RECEIVED, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(trigger, [(ESPNowPacket, "packet")], conf)


@automation.register_action(
    "espnow.send",
    SendAction,
    cv.maybe_simple_value(
        {
            cv.GenerateID(): cv.use_id(ESPNowComponent),
            cv.Optional(CONF_MAC_ADDRESS): cv.templatable(cv.mac_address),
            cv.Required(CONF_DATA): cv.templatable(cv.ensure_list(cv.hex_uint8_t)),
        },
        key=CONF_DATA,
    ),
)
async def speaker_play_action(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    if CONF_MAC_ADDRESS in config:
        mac_address = config[CONF_MAC_ADDRESS].as_hex
        if cg.is_template(mac_address):
            templ = await cg.templatable(mac_address, args, cg.uint64)
            cg.add(var.set_mac_address_template(templ))
        else:
            cg.add(var.set_mac_address(mac_address))

    data = config[CONF_DATA]
    if cg.is_template(data):
        templ = await cg.templatable(data, args, cg.std_vector.template(cg.uint8))
        cg.add(var.set_data_template(templ))
    else:
        cg.add(var.set_data(data))
    return var
