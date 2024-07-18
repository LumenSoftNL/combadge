from esphome import automation
import esphome.config_validation as cv
import esphome.codegen as cg
from esphome.const import CONF_ID, CONF_DATA, CONF_MAC_ADDRESS, CONF_TRIGGER_ID

CODEOWNERS = ["@LumenSoftNL"]


espnow_ns = cg.esphome_ns.namespace("esp_now")
ESPNowComponent = espnow_ns.class_("ESPNowComponent", cg.Component)
ESPNowListener = espnow_ns.class_("ESPNowListener")

ESPNowPackage = espnow_ns.class_("ESPNowPackage")

ESPNowSendTrigger = espnow_ns.class_("ESPNowSendTrigger", automation.Trigger.template())
ESPNowReceiveTrigger = espnow_ns.class_(
    "ESPNowReceiveTrigger", automation.Trigger.template()
)
ESPNowNewPeerTrigger = espnow_ns.class_(
    "ESPNowNewPeerTrigger", automation.Trigger.template()
)

SendAction = espnow_ns.class_("SendAction", automation.Action)
NewPeerAction = espnow_ns.class_("NewPeerAction", automation.Action)
DelPeerAction = espnow_ns.class_("DelPeerAction", automation.Action)

CONF_ESPNOW = "espnow"
CONF_ON_PACKAGE_RECEIVED = "on_package_received"
CONF_ON_PACKAGE_SEND = "on_package_send"
CONF_ON_NEW_PEER = "on_new_peer"
CONF_CHANNEL = "wifi_channel"
CONF_PEERS = "peers"
CONF_AUTO_NEW_PEER = "auto_new_peer"


def validate_raw_data(value):
    if isinstance(value, str):
        return value.encode("utf-8")
    if isinstance(value, list):
        return cv.Schema([cv.hex_uint8_t])(value)
    raise cv.Invalid(
        "data must either be a string wrapped in quotes or a list of bytes"
    )


CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(ESPNowComponent),
        cv.Optional(CONF_CHANNEL, default=0): cv.int_range(0, 14),
        cv.Optional(CONF_AUTO_NEW_PEER, default=False): cv.bool,
        cv.Optional(CONF_ON_PACKAGE_RECEIVED): automation.validate_automation(
            {
                cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(ESPNowReceiveTrigger),
            }
        ),
        cv.Optional(CONF_ON_PACKAGE_SEND): automation.validate_automation(
            {
                cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(ESPNowSendTrigger),
            }
        ),
        cv.Optional(CONF_ON_NEW_PEER): automation.validate_automation(
            {
                cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(ESPNowNewPeerTrigger),
            }
        ),
        cv.Optional(CONF_PEERS): cv.ensure_list(cv.mac_address),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(var, config):

    await cg.register_component(var, config)
    cg.add(var.set_wifi_channel(config[CONF_CHANNEL]))

    cg.add_define("USE_ESPNOW")

    for conf in config.get(CONF_ON_PACKAGE_SEND, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(trigger, [(ESPNowPackage, "it")], conf)

    for conf in config.get(CONF_ON_PACKAGE_RECEIVED, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(trigger, [(ESPNowPackage, "it")], conf)

    for conf in config.get(CONF_ON_NEW_PEER, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(trigger, [(ESPNowPackage, "it")], conf)

    for conf in config.get(CONF_PEERS, []):
        cg.add(var.add_set_wifi_channel(config[CONF_CHANNEL]))


@automation.register_action(
    "espnow.send",
    SendAction,
    cv.maybe_simple_value(
        {
            cv.GenerateID(): cv.use_id(ESPNowComponent),
            cv.Optional(CONF_MAC_ADDRESS): cv.templatable(cv.mac_address),
            cv.Required(CONF_DATA): cv.templatable(validate_raw_data),
        },
        key=CONF_DATA,
    ),
)
async def send_action(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    if CONF_MAC_ADDRESS in config:
        template_ = await cg.templatable(
            config[CONF_MAC_ADDRESS].as_hex, args, cg.uint64
        )
        cg.add(var.set_mac_address(template_))

    data = config.get(CONF_DATA, [])
    if isinstance(data, bytes):
        data = list(data)

    templ = await cg.templatable(data, args, cg.std_vector.template(cg.uint8))
    cg.add(var.set_data_template(templ))
    return var


@automation.register_action(
    "espnow.new.peer",
    NewPeerAction,
    cv.maybe_simple_value(
        {
            cv.GenerateID(): cv.use_id(ESPNowComponent),
            cv.Required(CONF_MAC_ADDRESS): cv.templatable(cv.mac_address),
        },
        key=CONF_MAC_ADDRESS,
    ),
)
async def new_peer_action(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    if CONF_MAC_ADDRESS in config:
        template_ = await cg.templatable(
            config[CONF_MAC_ADDRESS].as_hex, args, cg.uint64
        )
        cg.add(var.set_mac_address(template_))
    return var


@automation.register_action(
    "espnow.del.peer",
    DelPeerAction,
    cv.maybe_simple_value(
        {
            cv.GenerateID(): cv.use_id(ESPNowComponent),
            cv.Required(CONF_MAC_ADDRESS): cv.templatable(cv.mac_address),
        },
        key=CONF_MAC_ADDRESS,
    ),
)
async def del_peer_action(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    if CONF_MAC_ADDRESS in config:
        template_ = await cg.templatable(
            config[CONF_MAC_ADDRESS].as_hex, args, cg.uint64
        )
        cg.add(var.set_mac_address(template_))
    return var
