import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import uart, sensor, text_sensor, binary_sensor
from esphome.const import CONF_CO2, CONF_HUMIDITY, CONF_ID, CONF_TEMPERATURE

DEPENDENCIES = ["uart"]
AUTO_LOAD = ["sensor", "text_sensor", "binary_sensor"]

duco_gateway_ns = cg.esphome_ns.namespace("duco_gateway")
DucoGateway = duco_gateway_ns.class_("DucoGateway", cg.Component, uart.UARTDevice)

CONF_POLL_INTERVAL = "poll_interval"
CONF_INFO_POLL_INTERVAL = "info_poll_interval"

# External sensor nodes (nodeparaget)
CONF_EXT_SENSORS = "ext_sensors"
CONF_NODE = "node"
MAX_EXT_SENSORS = 8  # Keep in sync with MAX_EXT_SENSORS in duco_gateway.h

# Legacy single-node keys, converted into ext_sensors entries.
# Defaults match the node numbers the C++ code used before ext_sensors existed.
CONF_EXT_SENSOR_NODE = "ext_sensor_node"
CONF_RH_SENSOR_NODE = "rh_sensor_node"
LEGACY_EXT_SENSOR_NODE_DEFAULT = 3
LEGACY_RH_SENSOR_NODE_DEFAULT = 4

# Sensor IDs
CONF_VENT_MODE_SENSOR = "vent_mode_sensor"
CONF_VENT_MODE_TEXT = "vent_mode_text_sensor"
CONF_VENT_PCT_SENSOR = "vent_percentage_sensor"
CONF_FAN_SPEED_SENSOR = "fan_speed_sensor"
CONF_COUNTDOWN_SENSOR = "countdown_sensor"
CONF_BOX_TEMP_SENSOR = "box_temperature_sensor"
CONF_BOX_RH_SENSOR = "box_humidity_sensor"
CONF_EXT_CO2_SENSOR = "ext_co2_sensor"
CONF_EXT_TEMP_SENSOR = "ext_temperature_sensor"
CONF_EXT_RH_SENSOR = "ext_humidity_sensor"

# Zones (zone valve nodes in the Network table)
CONF_ZONES = "zones"
CONF_VENTILATION_MODE = "ventilation_mode"
CONF_VENTILATION_PERCENTAGE = "ventilation_percentage"
MAX_ZONES = 4  # Keep in sync with MAX_ZONES in duco_gateway.h

# Info commands (polled every info_poll_interval): config key -> (C++ setter, entity class)
INFO_SENSORS = {
    # FanInfo
    "exhaust_fan_speed_sensor": ("set_exhaust_fan_speed_sensor", sensor.Sensor),
    "supply_fan_speed_sensor": ("set_supply_fan_speed_sensor", sensor.Sensor),
    # TemperatureInfo
    "outdoor_temperature_sensor": ("set_outdoor_temperature_sensor", sensor.Sensor),    # ODA
    "supply_temperature_sensor": ("set_supply_temperature_sensor", sensor.Sensor),      # SUP
    "extract_temperature_sensor": ("set_extract_temperature_sensor", sensor.Sensor),    # ETA
    "exhaust_temperature_sensor": ("set_exhaust_temperature_sensor", sensor.Sensor),    # EHA
    # BypassInfo
    "bypass_position_sensor": ("set_bypass_position_sensor", sensor.Sensor),
    "bypass_target_temperature_sensor": ("set_bypass_target_temperature_sensor", sensor.Sensor),
    # FanCtrlInfo
    "exhaust_pressure_sensor": ("set_exhaust_pressure_sensor", sensor.Sensor),
    "supply_pressure_sensor": ("set_supply_pressure_sensor", sensor.Sensor),
    # FreezeProtectInfo
    "frost_protection_text_sensor": ("set_frost_protection_text_sensor", text_sensor.TextSensor),
    "heater_level_sensor": ("set_heater_level_sensor", sensor.Sensor),
    # StatusMonitorInfo
    "warnings_text_sensor": ("set_warnings_text_sensor", text_sensor.TextSensor),
    "problem_binary_sensor": ("set_problem_binary_sensor", binary_sensor.BinarySensor),
    "filter_warning_binary_sensor": ("set_filter_warning_binary_sensor", binary_sensor.BinarySensor),
    # FilterInfo
    "filter_remaining_days_sensor": ("set_filter_remaining_days_sensor", sensor.Sensor),
    "filter_remaining_percent_sensor": ("set_filter_remaining_percent_sensor", sensor.Sensor),
    # RtcGetTime
    "device_time_text_sensor": ("set_device_time_text_sensor", text_sensor.TextSensor),
}

LEGACY_KEYS = (
    CONF_EXT_SENSOR_NODE,
    CONF_RH_SENSOR_NODE,
    CONF_EXT_CO2_SENSOR,
    CONF_EXT_TEMP_SENSOR,
    CONF_EXT_RH_SENSOR,
)


def _validate_unique_nodes(value):
    nodes = [entry[CONF_NODE] for entry in value]
    duplicates = sorted({n for n in nodes if nodes.count(n) > 1})
    if duplicates:
        raise cv.Invalid(
            f"Each node may only appear once in this list; duplicated: {duplicates}"
        )
    return value


EXT_SENSOR_SCHEMA = cv.All(
    cv.Schema({
        cv.Required(CONF_NODE): cv.uint8_t,
        cv.Optional(CONF_CO2): cv.use_id(sensor.Sensor),          # nodeparaget <node> 74
        cv.Optional(CONF_TEMPERATURE): cv.use_id(sensor.Sensor),  # nodeparaget <node> 73
        cv.Optional(CONF_HUMIDITY): cv.use_id(sensor.Sensor),     # nodeparaget <node> 75
    }),
    cv.has_at_least_one_key(CONF_CO2, CONF_TEMPERATURE, CONF_HUMIDITY),
)


def _convert_legacy_ext_sensors(config):
    """Rewrite ext_sensor_node/rh_sensor_node + ext_*_sensor into ext_sensors entries."""
    if not any(key in config for key in LEGACY_KEYS):
        return config
    if CONF_EXT_SENSORS in config:
        raise cv.Invalid(
            f"Use either '{CONF_EXT_SENSORS}' or the legacy keys "
            f"({', '.join(LEGACY_KEYS)}), not both"
        )

    config = config.copy()
    ext_node = config.pop(CONF_EXT_SENSOR_NODE, LEGACY_EXT_SENSOR_NODE_DEFAULT)
    rh_node = config.pop(CONF_RH_SENSOR_NODE, LEGACY_RH_SENSOR_NODE_DEFAULT)

    # One entry per node; CO2/temperature and humidity share it if the nodes match.
    entries = {}
    for legacy_key, node, key in (
        (CONF_EXT_CO2_SENSOR, ext_node, CONF_CO2),
        (CONF_EXT_TEMP_SENSOR, ext_node, CONF_TEMPERATURE),
        (CONF_EXT_RH_SENSOR, rh_node, CONF_HUMIDITY),
    ):
        if legacy_key in config:
            entries.setdefault(node, {CONF_NODE: node})[key] = config.pop(legacy_key)

    config[CONF_EXT_SENSORS] = list(entries.values())
    return config


ZONE_SCHEMA = cv.All(
    cv.Schema({
        cv.Required(CONF_NODE): cv.uint8_t,
        cv.Optional(CONF_VENTILATION_MODE): cv.use_id(text_sensor.TextSensor),
        cv.Optional(CONF_VENTILATION_PERCENTAGE): cv.use_id(sensor.Sensor),
    }),
    cv.has_at_least_one_key(CONF_VENTILATION_MODE, CONF_VENTILATION_PERCENTAGE),
)


CONFIG_SCHEMA = cv.All(
    cv.Schema({
        cv.GenerateID(): cv.declare_id(DucoGateway),
        # Core behavior
        cv.Optional(CONF_POLL_INTERVAL, default="15s"): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_INFO_POLL_INTERVAL, default="60s"): cv.positive_time_period_milliseconds,

        # External sensor nodes (CO2 / temperature / humidity per node)
        cv.Optional(CONF_EXT_SENSORS): cv.All(
            cv.ensure_list(EXT_SENSOR_SCHEMA),
            cv.Length(max=MAX_EXT_SENSORS),
            _validate_unique_nodes,
        ),

        # Zone valve nodes: mode / % from their Network table row
        cv.Optional(CONF_ZONES): cv.All(
            cv.ensure_list(ZONE_SCHEMA),
            cv.Length(max=MAX_ZONES),
            _validate_unique_nodes,
        ),

        # Info command sensors (see INFO_SENSORS)
        **{cv.Optional(key): cv.use_id(cls) for key, (_, cls) in INFO_SENSORS.items()},

        # Legacy single-node keys (converted into ext_sensors)
        cv.Optional(CONF_EXT_SENSOR_NODE): cv.uint8_t,
        cv.Optional(CONF_RH_SENSOR_NODE): cv.uint8_t,

        # Optional sensors/text sensors
        cv.Optional(CONF_VENT_MODE_SENSOR): cv.use_id(sensor.Sensor),
        cv.Optional(CONF_VENT_MODE_TEXT): cv.use_id(text_sensor.TextSensor),
        cv.Optional(CONF_VENT_PCT_SENSOR): cv.use_id(sensor.Sensor),
        cv.Optional(CONF_FAN_SPEED_SENSOR): cv.use_id(sensor.Sensor),
        cv.Optional(CONF_COUNTDOWN_SENSOR): cv.use_id(sensor.Sensor),
        cv.Optional(CONF_BOX_TEMP_SENSOR): cv.use_id(sensor.Sensor),
        cv.Optional(CONF_BOX_RH_SENSOR): cv.use_id(sensor.Sensor),
        cv.Optional(CONF_EXT_CO2_SENSOR): cv.use_id(sensor.Sensor),
        cv.Optional(CONF_EXT_TEMP_SENSOR): cv.use_id(sensor.Sensor),
        cv.Optional(CONF_EXT_RH_SENSOR): cv.use_id(sensor.Sensor),
    })
    .extend(uart.UART_DEVICE_SCHEMA)
    .extend(cv.COMPONENT_SCHEMA),
    _convert_legacy_ext_sensors,
)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)

    cg.add(var.set_poll_interval_ms(config[CONF_POLL_INTERVAL].total_milliseconds))
    cg.add(var.set_info_poll_interval_ms(config[CONF_INFO_POLL_INTERVAL].total_milliseconds))

    for entry in config.get(CONF_EXT_SENSORS, []):
        sensors = []
        for key in (CONF_CO2, CONF_TEMPERATURE, CONF_HUMIDITY):
            sensors.append(await cg.get_variable(entry[key]) if key in entry else cg.nullptr)
        cg.add(var.add_ext_sensor(entry[CONF_NODE], *sensors))

    for entry in config.get(CONF_ZONES, []):
        mode = await cg.get_variable(entry[CONF_VENTILATION_MODE]) if CONF_VENTILATION_MODE in entry else cg.nullptr
        pct = (
            await cg.get_variable(entry[CONF_VENTILATION_PERCENTAGE])
            if CONF_VENTILATION_PERCENTAGE in entry
            else cg.nullptr
        )
        cg.add(var.add_zone(entry[CONF_NODE], mode, pct))

    for key, (method, _) in INFO_SENSORS.items():
        if key in config:
            s = await cg.get_variable(config[key])
            cg.add(getattr(var, method)(s))

    for conf_key, method in [
        (CONF_VENT_MODE_SENSOR, "set_vent_mode_sensor"),
        (CONF_VENT_MODE_TEXT, "set_vent_mode_text_sensor"),
        (CONF_VENT_PCT_SENSOR, "set_vent_percentage_sensor"),
        (CONF_FAN_SPEED_SENSOR, "set_fan_speed_sensor"),
        (CONF_COUNTDOWN_SENSOR, "set_countdown_sensor"),
        (CONF_BOX_TEMP_SENSOR, "set_box_temperature_sensor"),
        (CONF_BOX_RH_SENSOR, "set_box_humidity_sensor"),
    ]:
        if conf_key in config:
            s = await cg.get_variable(config[conf_key])
            cg.add(getattr(var, method)(s))