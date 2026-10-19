"""duco_gateway select platform: bypass (heat recovery) mode."""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import select
from esphome.const import CONF_TYPE

from . import CONF_DUCO_GATEWAY_ID, DucoGateway, duco_gateway_ns

DEPENDENCIES = ["duco_gateway"]

DucoSelect = duco_gateway_ns.class_("DucoSelect", select.Select, cg.Parented.template(DucoGateway))

# Index = BypassParaSet 0 <n>: 0 = AUTO, 1 = MANUAL CLOSED, 2 = MANUAL OPEN
BYPASS_MODE_OPTIONS = ["Auto", "Closed (heat recovery)", "Open (bypass)"]

CONFIG_SCHEMA = select.select_schema(DucoSelect, icon="mdi:valve").extend({
    cv.GenerateID(CONF_DUCO_GATEWAY_ID): cv.use_id(DucoGateway),
    cv.Required(CONF_TYPE): cv.one_of("bypass_mode", lower=True),
})


async def to_code(config):
    var = await select.new_select(config, options=BYPASS_MODE_OPTIONS)
    await cg.register_parented(var, config[CONF_DUCO_GATEWAY_ID])
    parent = await cg.get_variable(config[CONF_DUCO_GATEWAY_ID])
    cg.add(parent.set_bypass_mode_select(var))
