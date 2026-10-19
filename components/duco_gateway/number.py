"""duco_gateway number platform: box settings that can be changed from Home Assistant."""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import number
from esphome.const import (
    CONF_DEVICE_CLASS,
    CONF_ICON,
    CONF_TYPE,
    CONF_UNIT_OF_MEASUREMENT,
)

from . import CONF_DUCO_GATEWAY_ID, DucoGateway, duco_gateway_ns

DEPENDENCIES = ["duco_gateway"]

DucoNumber = duco_gateway_ns.class_("DucoNumber", number.Number, cg.Parented.template(DucoGateway))

CONF_NODE = "node"
CONF_ZONE = "zone"

SOURCE_NODE_CONFIG = 0  # NodeConfigGet/Set <node> <param>
SOURCE_GENERAL = 1      # SettingsParaGet/Set <param>

# type: box parameter and entity defaults. Ranges stay within what the box reports
# (NodeConfigGet prints [min:step:max]); the box value is read back after each write.
TYPES = {
    "co2_setpoint": dict(source=SOURCE_NODE_CONFIG, param=4, min=400, max=2000, step=10,
                         unit="ppm", device_class="carbon_dioxide", icon="mdi:molecule-co2"),
    "auto_min": dict(source=SOURCE_NODE_CONFIG, param=1, min=10, max=100, step=5, unit="%",
                     icon="mdi:fan-chevron-down"),
    "auto_max": dict(source=SOURCE_NODE_CONFIG, param=2, min=10, max=100, step=5, unit="%",
                     icon="mdi:fan-chevron-up"),
    "manual_1": dict(source=SOURCE_NODE_CONFIG, param=7, min=0, max=50, step=5, unit="%",
                     icon="mdi:fan-speed-1"),
    "manual_2": dict(source=SOURCE_NODE_CONFIG, param=8, min=15, max=100, step=5, unit="%",
                     icon="mdi:fan-speed-2"),
    "manual_3": dict(source=SOURCE_NODE_CONFIG, param=9, min=50, max=100, step=5, unit="%",
                     icon="mdi:fan-speed-3"),
    "manual_timeout": dict(source=SOURCE_NODE_CONFIG, param=10, min=5, max=9995, step=5,
                           unit="min", device_class="duration", icon="mdi:timer-outline"),
    # SettingsParaSet 5 (zone 1) / 6 (zone 2), in 0.1 °C
    "comfort_temperature": dict(source=SOURCE_GENERAL, param=None, scale=0.1, min=15, max=30, step=0.1,
                                unit="°C", device_class="temperature", icon="mdi:home-thermometer"),
}


def _set_type_defaults(config):
    """Fill in unit / device class / icon for the type, unless given in YAML."""
    t = TYPES.get(str(config.get(CONF_TYPE, "")).lower())
    if t is None:
        return config
    config = config.copy()
    config.setdefault(CONF_UNIT_OF_MEASUREMENT, t["unit"])
    config.setdefault(CONF_ICON, t["icon"])
    if "device_class" in t:
        config.setdefault(CONF_DEVICE_CLASS, t["device_class"])
    return config


def _validate_target(config):
    t = TYPES[config[CONF_TYPE]]
    if t["source"] == SOURCE_GENERAL:
        if CONF_ZONE not in config or CONF_NODE in config:
            raise cv.Invalid(f"type '{config[CONF_TYPE]}' needs 'zone' (1 or 2), not 'node'")
    elif CONF_NODE not in config or CONF_ZONE in config:
        raise cv.Invalid(f"type '{config[CONF_TYPE]}' needs 'node', not 'zone'")
    return config


CONFIG_SCHEMA = cv.All(
    _set_type_defaults,
    number.number_schema(DucoNumber).extend({
        cv.GenerateID(CONF_DUCO_GATEWAY_ID): cv.use_id(DucoGateway),
        cv.Required(CONF_TYPE): cv.one_of(*TYPES, lower=True),
        cv.Optional(CONF_NODE): cv.int_range(min=1, max=255),
        cv.Optional(CONF_ZONE): cv.int_range(min=1, max=2),
    }),
    _validate_target,
)


async def to_code(config):
    t = TYPES[config[CONF_TYPE]]
    var = await number.new_number(config, min_value=t["min"], max_value=t["max"], step=t["step"])
    await cg.register_parented(var, config[CONF_DUCO_GATEWAY_ID])
    parent = await cg.get_variable(config[CONF_DUCO_GATEWAY_ID])
    if t["source"] == SOURCE_GENERAL:
        node, param = 0, 4 + config[CONF_ZONE]
    else:
        node, param = config[CONF_NODE], t["param"]
    cg.add(parent.add_setting_number(var, t["source"], node, param, t.get("scale", 1.0)))
