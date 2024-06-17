import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.core import coroutine_with_priority

CODEOWNERS = ["@LumenSoftNL"]
combadge_ns = cg.esphome_ns.namespace("combadge")

CONFIG_SCHEMA = cv.All(
    cv.Schema({}),
)

@coroutine_with_priority(1.0)
async def to_code(config):
    cg.add_define("USE_COMBADGES")
    cg.add_global(combadge_ns.using)
