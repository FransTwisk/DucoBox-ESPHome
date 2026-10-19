"""duco_gateway select platform: bypass (heat recovery) mode and ventilation per zone."""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import select
from esphome.const import CONF_ICON, CONF_TYPE

from . import CONF_DUCO_GATEWAY_ID, DucoGateway, duco_gateway_ns

DEPENDENCIES = ["duco_gateway"]

DucoSelect = duco_gateway_ns.class_("DucoSelect", select.Select, cg.Parented.template(DucoGateway))

CONF_NODE = "node"

TYPES = {
    # Index = BypassParaSet 0 <n>: 0 = AUTO, 1 = MANUAL CLOSED, 2 = MANUAL OPEN
    "bypass_mode": dict(options=["Auto", "Closed (heat recovery)", "Open (bypass)"], icon="mdi:valve", node=False),
    # NodeSetOverrule <zone> 255 / <speed 1|2|3 level of the zone>, ended after the zone's manual timeout
    "zone_ventilation": dict(options=["Auto", "Min", "Med", "Max"], icon="mdi:fan", node=True),
}


def _set_type_defaults(config):
    t = TYPES.get(str(config.get(CONF_TYPE, "")).lower())
    if t is None:
        return config
    config = config.copy()
    config.setdefault(CONF_ICON, t["icon"])
    return config


def _validate_node(config):
    needs_node = TYPES[config[CONF_TYPE]]["node"]
    if needs_node and CONF_NODE not in config:
        raise cv.Invalid(f"type '{config[CONF_TYPE]}' needs 'node' (the zone valve, e.g. 67)")
    if not needs_node and CONF_NODE in config:
        raise cv.Invalid(f"type '{config[CONF_TYPE]}' doesn't take 'node'")
    return config


CONFIG_SCHEMA = cv.All(
    _set_type_defaults,
    select.select_schema(DucoSelect).extend({
        cv.GenerateID(CONF_DUCO_GATEWAY_ID): cv.use_id(DucoGateway),
        cv.Required(CONF_TYPE): cv.one_of(*TYPES, lower=True),
        cv.Optional(CONF_NODE): cv.int_range(min=2, max=255),
    }),
    _validate_node,
)


async def to_code(config):
    t = TYPES[config[CONF_TYPE]]
    var = await select.new_select(config, options=t["options"])
    await cg.register_parented(var, config[CONF_DUCO_GATEWAY_ID])
    parent = await cg.get_variable(config[CONF_DUCO_GATEWAY_ID])
    if t["node"]:
        cg.add(var.set_zone(config[CONF_NODE]))
        cg.add(parent.add_zone_control(config[CONF_NODE], var))
    else:
        cg.add(parent.set_bypass_mode_select(var))
