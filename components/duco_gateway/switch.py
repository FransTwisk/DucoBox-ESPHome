"""duco_gateway switch platform: keep a zone's ventilation (zone_ventilation) until changed."""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import switch
from esphome.const import CONF_ICON, CONF_TYPE

from . import CONF_DUCO_GATEWAY_ID, DucoGateway, duco_gateway_ns

DEPENDENCIES = ["duco_gateway"]

DucoSwitch = duco_gateway_ns.class_("DucoSwitch", switch.Switch, cg.Parented.template(DucoGateway))

CONF_NODE = "node"

TYPES = {
    # Keeps the zone's NodeSetOverrule override instead of ending it after the manual timeout;
    # also on while a wall controller has set the zone permanently (CNT1-3)
    "zone_permanent": dict(icon="mdi:pin"),
}


def _set_type_defaults(config):
    t = TYPES.get(str(config.get(CONF_TYPE, "")).lower())
    if t is None:
        return config
    config = config.copy()
    config.setdefault(CONF_ICON, t["icon"])
    return config


CONFIG_SCHEMA = cv.All(
    _set_type_defaults,
    # The gateway keeps the state itself (across restarts too), so no switch restore
    switch.switch_schema(DucoSwitch, block_inverted=True, default_restore_mode="DISABLED").extend({
        cv.GenerateID(CONF_DUCO_GATEWAY_ID): cv.use_id(DucoGateway),
        cv.Required(CONF_TYPE): cv.one_of(*TYPES, lower=True),
        cv.Required(CONF_NODE): cv.int_range(min=2, max=255),
    }),
)


async def to_code(config):
    var = await switch.new_switch(config)
    await cg.register_parented(var, config[CONF_DUCO_GATEWAY_ID])
    parent = await cg.get_variable(config[CONF_DUCO_GATEWAY_ID])
    cg.add(var.set_zone(config[CONF_NODE]))
    cg.add(parent.add_zone_permanent(config[CONF_NODE], var))
