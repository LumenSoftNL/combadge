import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.core import coroutine_with_priority

CODEOWNERS = ["@LumenSoftNL"]
badgeid_ns = cg.esphome_ns.namespace("badge_id")

CONFIG_SCHEMA = cv.All(
    cv.Schema({}),
)

@coroutine_with_priority(1.0)
async def to_code(config):
    cg.add_define("USE_BADGE_ID")
    cg.add_global(badgeid_ns.using)
