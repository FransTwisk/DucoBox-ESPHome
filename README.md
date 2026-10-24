# DucoBox-ESPHome
DucoBox ESPHome component based on the work of [arnemauer/Ducobox-ESPEasy-Plugin](https://github.com/arnemauer/Ducobox-ESPEasy-Plugin/). It uses native ESPHome components where possible, e.g. UART, SPI, CC1101. It was tested with a DucoBox Silent. The box information, settings and zone control were developed and tested on a DucoBox Energy Premium (box software `19156.3.3.0`). It should work with other DucoBoxes that were supported by the ESPEasy plugin; see the [serial console reference](#serial-console-reference) for what differs per model.

*Note: by default this ESPHome implementation uses the GDO0 pin of the CC1101, while the ESPEasy plugin uses the GDO2 pin. If your board only connects GDO2 to the ESP (e.g. the ESPEasy Duco shield), see [Using GDO2 instead of GDO0](#using-gdo2-instead-of-gdo0).*

## Example configurations
- `ducobox.yaml`: full gateway + RF example (ESP32 active, ESP8266 alternative commented in-file).
- `ducobox-simple.yaml`: simplified full gateway + RF example (ESP32 active, ESP8266 alternative commented in-file).
- `ducobox-rf.yaml`: RF-only example (ESP32 active, ESP8266 alternative commented in-file).
- `ducobox-rf-simple.yaml`: simplified RF-only example (ESP32 active, ESP8266 alternative commented in-file).
- `ducobox-gateway.yaml`: serial gateway-only example for ESP32(-S3) (read-only ventilation telemetry).
- `ducobox-diag.yaml`: diagnostic variant of `ducobox.yaml` with extra logging and a custom command text entity for troubleshooting.

## Board selection
The examples use ESP32 by default and include commented ESP8266 alternatives in the same file.

To switch one of the combined examples from ESP32 to ESP8266:
1. Comment the `esp32:` block and uncomment the `esp8266:` block.
2. Update the pin values in the `substitutions:` block at the top of the file.

## Pin configuration via substitutions
Pinning is configured through substitutions near the top of each example so hardware changes are made in one place.

Typical substitutions look like this:

```yaml
substitutions:
    uart_tx_pin: GPIO43
    uart_rx_pin: GPIO44
    cc1101_cs_pin: GPIO07
    cc1101_gdo0_pin: GPIO06
    spi_clk_pin: GPIO02
    spi_miso_pin: GPIO04
    spi_mosi_pin: GPIO01
    # ESP8266 alternative pins:
    # uart_tx_pin: GPIO1
    # uart_rx_pin: GPIO3
    # cc1101_cs_pin: GPIO15
    # cc1101_gdo0_pin: GPIO02
    # spi_clk_pin: GPIO14
    # spi_miso_pin: GPIO12
    # spi_mosi_pin: GPIO13
```

The gateway-only example only uses the UART substitutions. The RF-only examples only use the CC1101 and SPI substitutions.

# Usage
In order to use the component perform the following steps:
1. Flash one of the example ESPHome configurations onto your device.
2. Put your ventilation system into installer mode:
    1. **Via the DucoBox itself:** Press the 'INST' button on the unit.
    2. **Via a Control/Sensor:** Long-press two diagonal buttons simultaneously on a paired user controller.
3. Click the "Pair" button, either in HA or on the web server of the ESP itself.
    1. Your ESP will save the network ID and node address automatically and persistently, so no update of the YAML config after pairing is needed.
4. Click on "Disable Installer Mode" to exit the installer mode and restart normal operation of the ventilation unit.

If the ESP was already paired using the ESPEasy plugin, you can skip pairing and reuse that pairing instead, see [Taking over an existing pairing](#taking-over-an-existing-pairing).

# Determine node IDs
In order to determine the node IDs of other devices in the network, click "Log All Duco Nodes" after pairing to show the available nodes, e.g. BOX, UCCO2, UCBAT, SWITCH, UCRH. These can be used to configure the ESP to request additional information for each node via the serial gateway, e.g. temperature, humidity or CO2 concentration. The ESP will also show up as a CO2 remote (UCCO2). Boxes with zones also list a zone valve per zone (`VLV`); the `asso` column shows which zone valve a room controller belongs to (see [Ventilation per zone](#ventilation-per-zone)).

In order to reconfigure these in the ESP, add the node IDs to `ext_sensors` in the configuration file (see [External sensor nodes](#external-sensor-nodes)) and reprogram the ESP. Afterwards the information from the nodes should become available in the ESP as well.

# Configuration options

## External sensor nodes
`duco_gateway` can read CO2, temperature and humidity from up to 8 other nodes (e.g. CO2 controllers or humidity sensors) via the serial gateway. Add one entry per node to `ext_sensors`; each entry needs a `node` and at least one sensor:

```yaml
duco_gateway:
  # ...
  ext_sensors:
    - node: 3                      # Bedroom CO2 controller (UCCO2)
      co2: bedroom_co2
      temperature: bedroom_temperature
    - node: 4                      # Living room CO2 controller (UCCO2)
      co2: livingroom_co2
      temperature: livingroom_temperature
    - node: 6                      # Bathroom humidity sensor (UCRH)
      humidity: bathroom_humidity
```

The values refer to `sensor` IDs in your configuration (e.g. template sensors). Each poll cycle reads the configured values node by node:

| Key           | Command                    | Value          |
|---------------|----------------------------|----------------|
| `co2`         | `nodeparaget <node> 74`    | CO2 in ppm     |
| `temperature` | `nodeparaget <node> 73`    | raw / 10 → °C  |
| `humidity`    | `nodeparaget <node> 75`    | raw / 100 → %  |

Each node may only appear once. If a `nodeparaget` command times out, the remaining reads of that poll cycle are skipped, because a late reply can't be matched to the right node or value. The next cycle starts again with the first node.

The older single-node keys still work and are converted into `ext_sensors` entries: `ext_sensor_node` (default 3) with `ext_co2_sensor` and `ext_temperature_sensor`, and `rh_sensor_node` (default 4) with `ext_humidity_sensor`. They can't be combined with `ext_sensors` in the same configuration.

## Box information (fans, temperatures, bypass, filter, warnings)
Newer boxes such as the DucoBox Energy (Premium) report more through their serial console than the `Network` table. `duco_gateway` reads these with a few info commands, every `info_poll_interval` (default 60s, and once right after boot). Only the commands for which you configure a sensor are sent:

| Key | Type | Command | Value |
|-----|------|---------|-------|
| `exhaust_fan_speed_sensor`, `supply_fan_speed_sensor` | sensor | `FanInfo` | Fan speed in rpm (filtered) |
| `outdoor_temperature_sensor`, `supply_temperature_sensor`, `extract_temperature_sensor`, `exhaust_temperature_sensor` | sensor | `TemperatureInfo` | Air temperatures in °C: outdoor air (ODA), supply air into the house (SUP), extract air from the house (ETA), exhaust air to outside (EHA). No value while the box reports a sensor as not OK |
| `bypass_position_sensor` | sensor | `BypassInfo` | Bypass position in %. 0 % means the bypass is closed, so all air passes the heat exchanger |
| `bypass_target_temperature_sensor` | sensor | `BypassInfo` | Temperature the bypass control aims for, °C |
| `exhaust_pressure_sensor`, `supply_pressure_sensor` | sensor | `FanCtrlInfo` | Pressure in Pa |
| `frost_protection_text_sensor` | text sensor | `FreezeProtectInfo` | Frost protection state, e.g. `INACTIVE` |
| `heater_level_sensor` | sensor | `FreezeProtectInfo` | Frost protection heater level in % |
| `warnings_text_sensor` | text sensor | `StatusMonitorInfo` | Active warnings, e.g. `Replace filters (warning)`, or `None` |
| `problem_binary_sensor` | binary sensor | `StatusMonitorInfo` | On while any warning is active |
| `filter_warning_binary_sensor` | binary sensor | `StatusMonitorInfo` | On while a filter warning is active |
| `filter_remaining_days_sensor`, `filter_remaining_percent_sensor` | sensor | `FilterInfo` | Remaining filter lifetime in days and % |
| `device_time_text_sensor` | text sensor | `RtcGetTime` | The box's clock as it prints it, e.g. `SATURDAY 12/02/2124 13:36:32` |

If the box answers `Unknown Cmd` to one of these commands (or to `fanspeed`, which the Energy boxes don't have), it is not sent again until the next reboot, and a warning is logged.

Zone valves (`VLV` nodes in the `Network` table) report their own mode, percentage and countdown (seconds left of a manual mode set for that zone, e.g. from a room controller). The zone valve of a controller is shown in its `asso` column:

```yaml
duco_gateway:
  # ...
  zones:
    - node: 67                       # Bedroom zone valve
      ventilation_mode: bedroom_zone_mode          # text sensor, e.g. AUTO / MAN2
      ventilation_percentage: bedroom_zone_percentage
      countdown: bedroom_zone_countdown            # seconds
```

The `Network` reply is about 1.5 KB. Set `rx_buffer_size: 2048` on the `uart` used by `duco_gateway`; with ESPHome's default of 256 bytes the start of a reply can get lost. A `Network` table with a damaged header is skipped instead of being read from the wrong columns.

## Settings
Some box settings can be changed from Home Assistant through the serial gateway (tested on a DucoBox Energy Premium). A change is queued and written with the box's own `...Set` command; afterwards the value is read back from the box, and the entity always shows the value the box reports. If the box rejects a value, the entity keeps showing the current one. Settings are also read back every `settings_poll_interval` (default 1h), so changes made on the box or in the Duco app show up too.

```yaml
duco_gateway:
  # ...
  time_id: ha_time              # keep the box clock right
  settings_poll_interval: 1h

time:
  - platform: homeassistant
    id: ha_time

select:
  - platform: duco_gateway
    type: bypass_mode
    name: "Bypass Mode"

number:
  - platform: duco_gateway
    type: co2_setpoint
    node: 3                     # CO2 controller
    name: "Bedroom CO2 Setpoint"
  - platform: duco_gateway
    type: comfort_temperature
    zone: 1
    name: "Comfort Temperature Zone 1"
  - platform: duco_gateway
    type: manual_1
    node: 1                     # the box; zone valves have their own levels (e.g. 67, 68)
    name: "Box Speed 1 Level"
```

| Platform / `type` | Target | Box command | Range |
|---|---|---|---|
| select `bypass_mode` | – | `BypassParaSet 0 <n>` | `Auto`, `Closed (heat recovery)`, `Open (bypass)` |
| select `zone_ventilation` | `node` (zone valve) | `NodeSetOverrule <node> <%>` | `Auto`, `Min`, `Med`, `Max`: see below |
| switch `zone_permanent` | `node` (zone valve) | – (keeps the override) | on / off: see below |
| number `co2_setpoint` | `node` | `NodeConfigSet <node> 4` | 400–2000 ppm |
| number `comfort_temperature` | `zone` (1 or 2) | `SettingsParaSet 5/6` | 15–30 °C |
| number `manual_1`, `manual_2`, `manual_3` | `node` | `NodeConfigSet <node> 7/8/9` | % for speed 1 (0–50), 2 (15–100), 3 (50–100) |
| number `manual_timeout` | `node` | `NodeConfigSet <node> 10` | 5–9995 min: how long a manual speed lasts |
| number `auto_min`, `auto_max` | `node` | `NodeConfigSet <node> 1/2` | 10–100 %: range in automatic mode |

### Ventilation per zone
The ESP is paired as a remote for the whole box, so its RF commands (and the fan / select in your configuration that send them) set the ventilation of the box: every zone runs at the box's level, while each zone's own mode stays `AUTO`. A wall controller in a room sets only its own zone (`MAN1`–`MAN3` in that zone's row of the `Network` table, or `CNT1`–`CNT3` when set permanently); the box then runs at the average of the zones. On a box with two zone valves of the same capacity, the box's `%dbt` was always exactly that average: 10 % + 10 % → 10 %, 10 % + 100 % → 55 %, 10 % + 50 % → 30 %.

To set a single zone from Home Assistant, add a `zone_ventilation` select per zone valve:

```yaml
select:
  - platform: duco_gateway
    type: zone_ventilation
    node: 67                    # zone valve (VLV) of the bedroom
    name: "Bedroom Ventilation"
```

`Min`, `Med` and `Max` override the zone with its own speed 1/2/3 level (read from the zone with `NodeConfigGet`, e.g. 10/50/100 %), using `NodeSetOverrule <node> <%>`. The box itself has no time limit on such an override, so the gateway ends it like a wall controller does: after the zone's manual timeout (`NodeConfigGet` param 10, e.g. 15 min), it sends `NodeSetOverrule <node> 255` (no override). The zone's `countdown` sensor shows the time left, and choosing the same level again restarts it. `Auto` ends the override right away. If the gateway finds an override it didn't set (for example one left from before the ESP restarted), it ends that one after the manual timeout too. While an override is active, the zone's mode shows `EXTN` (the box shows that in the row's info column; its `stat` stays `AUTO`), and the select shows the nearest level. The select also shows `Min`/`Med`/`Max` while a wall controller has set that zone.

`Auto` only ends an override set with `NodeSetOverrule`; it doesn't cancel a speed chosen on a wall controller.

To keep a zone's speed until you change it, add a `zone_permanent` switch for the zone valve:

```yaml
switch:
  - platform: duco_gateway
    type: zone_permanent
    node: 67                    # zone valve (VLV) of the bedroom
    name: "Bedroom Ventilation Permanent"
```

Like the box's permanent mode, it applies to the current `Min`, `Med` or `Max`: choose the speed with the `zone_ventilation` select, then turn the switch on, and the gateway no longer ends the override after the manual timeout. A speed chosen while the switch is on stays too. Turning it off starts the manual timeout from that moment; `Auto` ends the override and turns the switch off. In `Auto` there is nothing to keep, so turning the switch on has no effect. The gateway remembers the switch across an ESP restart (in the ESP's preferences), so a permanent override it finds after a restart isn't ended; if the override was ended elsewhere in the meantime, the switch turns off. The switch is also on while a wall controller has set the zone permanently (`CNT1`–`CNT3`); that mode can only be ended on the controller.

`Closed (heat recovery)` keeps the bypass closed so all air passes the heat exchanger; `Open (bypass)` opens it fully. In `Auto` the box moves the bypass gradually (about 5 % every 2 minutes), so it can take a while to settle after switching back.

With `time_id`, the box clock is set from that time source once it is valid after boot, and daily from 04:00 (so daylight saving changes are followed). A wrong box clock can make the filter lifetime count wrong: on a box whose clock had drifted to the year 2124, `FilterInfo` reported 0 days left and raised "Replace filters" until the clock was set.

After a write the box sometimes ignores a command sent right away, so the gateway waits 1.5 seconds before the next one. Use `mode: box` on the numbers to avoid accidental changes from a slider.

## Taking over an existing pairing
Instead of pairing again, you can configure the network ID and device address of an existing pairing in `duco_rf`, e.g. the values shown in the ESPEasy Duco RF gateway plugin settings ("Network ID (HEX)" and "Device Address"):

```yaml
duco_rf:
  # ...
  network_id: "1A2B3C4D"   # 8 hex characters, in quotes
  device_address: 5        # 1-31
```

Both keys must be set together. Put `network_id` in quotes, otherwise YAML may read IDs that contain only digits as a number.

The configured values are only used while the ESP hasn't stored a network state of its own. After a Pair or Unpair from ESPHome, the stored result is used, and these values are ignored from then on.

## Using GDO2 instead of GDO0
The native ESPHome `cc1101` component expects the CC1101 GDO0 pin to be connected to the ESP (`gdo0_pin`). Some boards, such as the CC1101 shield used with the ESPEasy plugin, only connect GDO2. For those, set `use_gdo2: true` in `duco_rf` and set `gdo0_pin` to the ESP pin that is connected to GDO2:

```yaml
cc1101:
  # ...
  gdo0_pin: GPIO5          # ESP pin connected to the CC1101 GDO2 pin

duco_rf:
  # ...
  use_gdo2: true
```

`duco_rf` then configures GDO2 (register IOCFG2) to output the same packet-received signal that the `cc1101` component uses on GDO0. This is done after the `cc1101` component has finished its setup, and checked again every 60 seconds in case the CC1101 was reset. Look for `IOCFG2 (GDO2) set: 0x0D -> 0x01` in the log after boot.

With `use_gdo2: true`, don't use the `cc1101.begin_tx` action or `remote_transmitter` with this CC1101: they switch `gdo0_pin` to an output, which then conflicts with the GDO2 output of the CC1101. Sending Duco packets is not affected.

# Serial console reference
`duco_gateway` talks to the box's serial console: the same connection the ESPEasy plugin uses (115200 baud), where you type a command and the box prints a reply. Everything below was tested on a **DucoBox Energy Premium** (box software `19156.3.3.0`, board `BMB-NRG-L4`). Other models know other commands (a DucoBox Silent has `fanspeed`, for example), so check with `help` on your own box. To send commands yourself, use the "Gateway Command" text entity from `ducobox-diag.yaml`; the reply shows in the log as `CMD << ...`.

## How the console behaves
- **Commands a box doesn't have** are answered with `^` / `Unknown Cmd`, without `Done` or `Failed`. On the Energy Premium this is the case for `fanspeed`; the fan speeds come from `FanInfo` instead. `duco_gateway` stops sending a polled command after `Unknown Cmd`.
- **Many replies have no `Done` line**, e.g. `TemperatureInfo`, `FanCtrlInfo`, `StatusMonitorInfo` and `VentCoolInfo` end with just the prompt. The box prints a whole reply in well under 100 ms; `duco_gateway` treats a reply as complete after 300 ms without data.
- **`sensorinfo` without a box sensor** only prints `TEMP ADJUST. : 0 [.1°C]`, a temperature offset setting, not a measurement.
- **The `Network` reply is about 1.5 KB**, more than ESPHome's default serial receive buffer of 256 bytes (see [Box information](#box-information-fans-temperatures-bypass-filter-warnings)).
- **After a `...Set`** the box sometimes ignores a command sent right away; wait about 1.5 s.
- **A wrong box clock breaks the filter counter.** The filter install time is stored as a real date, but on a box whose clock had drifted to the year 2124 (`RtcGetTime`), `FilterInfo` reported `Remaining lifetime : 0 days (0%)` and the box raised "Replace filters" (`W.09.19.01` in the `Error` column of `Network`). After `RtcSetTime` it showed 35 days (19 %) and the warning cleared.

## Readable values
| Value | Command | Example from the reply | Unit |
|---|---|---|---|
| Ventilation mode, %, countdown and override (box and each zone) | `Network` | columns `stat`, `%dbt`, `cntdwn`, `ovrl`, one row per node; zone valves are `VLV` nodes | mode, %, **seconds**, % (`255` = no override) |
| CO2 of a room controller | `nodeparaget <node> 74` | `--> 462` | ppm |
| Temperature of a room controller | `nodeparaget <node> 73` | `--> 211` | / 10 → °C |
| Humidity (RH sensor) | `nodeparaget <node> 75` | (not tested, no RH sensor on the test box) | / 100 → % |
| Fan speed, exhaust and supply | `FanInfo` | `FanSpeed Exhaust:` … `- Filtered :  307 [rpm]`, then the same under `FanSpeed Supply:` | rpm |
| Air temperatures: outdoor (ODA), supply into the house (SUP), extract from the house (ETA), exhaust to outside (EHA) | `TemperatureInfo` | `- SENSOR ETA:   20.0 [C]  (OK)` | °C |
| Bypass position | `BypassInfo` | `- Bypass position : 0 [%]` (0 = closed, all air through the heat exchanger) | % |
| Bypass target temperature | `BypassInfo` | `- Requested temperature : 213 [.1°C]` | / 10 → °C |
| Exhaust and supply pressure | `FanCtrlInfo` | `p exhaust:   105 dPa`, `p supply:    27 dPa` | dPa |
| Frost protection state and heater level | `FreezeProtectInfo` | `State : INACTIVE`, `Heater level : 0` | text, % |
| Warnings (fans, pressure sensors, temperature sensors, filter) | `StatusMonitorInfo` | `9 \| Replace filters \| 09.19.01 \| 3 \| warning` (level `-` = not active) | text |
| Filter remaining | `FilterInfo` | `Remaining lifetime : 35 days (19%)` | days, % |
| Box clock | `RtcGetTime` | `[RTC] MONDAY 19/10/2026 13:55:09` | text |

Also useful, though not used by `duco_gateway`: `VentCoolInfo` (indoor and outdoor temperature, vent-cooling conditions), `NightBoostInfo` (temperature per zone), `CommSpiInfo` (motor positions, raw temperature and pressure sensors), `NetwVersion` (software version per node), `BoardInfo`, and `NodeParaList` (the `nodeparaget` parameters).

## Settings commands
| Setting | Read | Write | Values |
|---|---|---|---|
| Bypass mode | `BypassParaGet` (param 0) | `BypassParaSet 0 <n>` | `0` = AUTO, `1` = MANUAL CLOSED (0 %, heat recovery), `2` = MANUAL OPEN (100 %) |
| CO2 setpoint of a room controller | `NodeConfigGet <node>` (param 4) | `NodeConfigSet <node> 4 <ppm>` | 0–2000, step 10 |
| Comfort temperature zone 1 / 2 | `SettingsParaGet` (param 5 / 6) | `SettingsParaSet 5 <value>` / `SettingsParaSet 6 <value>` | in 0.1 °C, e.g. `205` = 20.5 °C |
| Manual timeout | `NodeConfigGet <node>` (param 10) | `NodeConfigSet <node> 10 <min>` | 5–9995 min |
| Speed 1 / 2 / 3 level | `NodeConfigGet <node>` (param 7 / 8 / 9) | `NodeConfigSet <node> 7\|8\|9 <%>` | speed 1: 0–50, speed 2: 10 or 15–100, speed 3: 50–100 (step 5) |
| Auto min / max | `NodeConfigGet <node>` (param 1 / 2) | `NodeConfigSet <node> 1\|2 <%>` | 10–100 (step 5) |
| Box clock | `RtcGetTime` | `RtcSetTime <day> <month> <year> <hour> <minute> <second>` | local time |
| Ventilation of one zone (override) | `Network` (column `ovrl`) | `NodeSetOverrule <zone valve> <%>` | 0–100 %, `255` = no override; the box puts no time limit on it |

`<node>` is the box (`1`), a zone valve (`VLV`) or a room controller. The ranges are what `NodeConfigGet` prints as `[min:step:max]`; they differ a bit per node.

## Test notes
- Tested on the box (read, changed, read back, restored): CO2 setpoint (param 4), manual timeout (param 10), comfort temperature, bypass mode, clock, and `NodeSetOverrule` (zone at 50 %, then back to `255`). Param 7 was written with its current value. Params 1, 2, 8 and 9 use the same `NodeConfigSet` command but weren't written in the tests.
- Not tested: whether settings survive a power cut of the box.
- The comfort temperature uses the box's own zone numbering. On the test box, zone 1 is most likely the first zone valve (`67`): the calibration data in `FanCtrlInfo` lists `ZONE 1` with 125 m³/h and `ZONE 2` with 150 m³/h, and `NightBoostInfo` lists `67` before `68`. This isn't proven.
- Deliberately left alone: `FanCalibStart`, `FanParaSet`, `FreezeProtectParaSet`, `MotorParaSet`, `NetworkClear`, `NodeReset`, `NodeLoadDefaults`, `DataClear`, `CommDllSetReg`.

<details>
<summary>Full <code>help</code> output (DucoBox Energy Premium, 19156.3.3.0)</summary>

```
BoardInfo                         BOARD info
BypassInfo                        BYPASS info
BypassParaGet                     BYPASS get para
BypassParaSet a b                 BYPASS set para
CommAlP2PInfo                     COMM P2P Info
CommDllDefaults                   Comm DLL set register to defaults
CommDllInfo                       Comm DLL info
CommDllSetReg a b                 Comm DLL set register a (0-46|100-107) to b
CommInfo                          COMM info
CommNlInfo                        Comm NL info
CommSpiInfo                       COMM SPI info
ConfigSetPressure a b             FAN set max calib pressure a:(0=supply, 1=exhaust), b:value in Pa
DataClear a                       DATAMNGR clear data
DataDefaults                      DATAMNGR load default data in RAM
DataInfo                          DATAMNGR info
DataSave                          DATAMNGR save data
FanCalibStart                     FAN start calibration
FanCalibStop                      FAN stop calibration
FanCtrlInfo                       FAN print info
FanInfo                           FAN print info
FanParaGet                        FAN get parameter values
FanParaSet a b                    FAN set parameter a to b
FanUseMax a                       FAN use max (1) or avg (0)
FilterInfo                        FILTER print info
FilterSetLifetime a               FILTER set lifetime in days
FreezeProtectInfo                 Freeze protection info
FreezeProtectParaGet              FREEZE get parameter values
FreezeProtectParaSet a b          FREEZE set parameter a (0-14) to b
InstallerSet a                    INSTALL enter (a=1) or leave (a=0) installer mode
IoParaGet                         IO get parameter values
IoParaSet a b c                   IO set parameter (b) of module (a) to (c)
LogClear                          LOG clear log
LogPrint                          LOG print log
MotorParaGet a                    COMM SPI get para of motor a
MotorParaSet a b c                COMM SPI set para b of motor a to c
MotorSetAllow a                   COMM SPI allow motion [1]
MotorSetTarget a b                COMM SPI set motor a to target b
MqttSendLoop a                    MQTT Send (a) nr of messages
MqttSendStatus a b c              MQTT Send message item (a), type (b), active_state (c)
Network                           NETW network table
NetworkClear                      NETW clear network (in installer mode only)
NetwSerial                        NETW serial number table
NetwVersion                       NETW version table
NightBoostInfo                    NIGHT BOOST Info
NightBoostParaGet                 NIGHT BOOST get parameters
NightBoostParaSet a b             NIGHT BOOST set parameters
NodeConfigGet a                   NODE get config para's of node a
NodeConfigSet a b c               NODE set config para b of node a to c
NodeInfo a                        NODE info of node (a)
NodeLoadDefaults a                NODE load defaults on node (a)
NodeParaGet a b                   NODE get para (b) of node (a)
NodeParaList                      NODE parameter list
NodeParaSet a b c                 NODE set para (b) of node (a) to (c)
NodeReset a                       NODE reset node (a)
NodeSaveData a                    NODE save data on node (a)
NodeSetAsso a b                   NODE set asso of node a to b
NodeSetOverrule a b               NODE set overrule of node a to b
NodeSetParent a b                 NODE set parent of node a to b
NodeShow a b                      NODE send msg to node (a) to show/hide (b)
PassiveHouseFreezeProtectReset    RESET Passive House Freeze Protection
PidInfo a                         Get info from PID controller a
RfArbitratorInfo                  Get info of the RF arbitrator
RfEnable a                        RF enable ON (a=1) or OFF (a = 0)
RfSwitchInfo                      RFSWITCH info
RtcGetTime                        Get time
RtcSetTime a b c d e f            Set time (a=day of month, b=month(1-12), c=year, d=hour, e=minute, f=second)
SensorCtrlInfo                    SENSOR CTRL get info
SensorCtrlParaGet                 SENSOR CTRL get parameter values
SensorCtrlParaSet a b             SENSOR CTRL set parameter a (0-31) to b
SensorInfo                        SENSOR get info
ServiceCode                       SERVICE CODE print
SettingsParaGet                   SETTINGS get parameter values
SettingsParaSet a b               SETTINGS set parameter a to b
StatusMonitorInfo                 Status monitor info
SwUploadRestart                   SWUPLOAD restart
SwUploadStatus                    SWUPLOAD status
TemperatureInfo                   Temperature sensor info
TimeVentAdd a b c d e             TimeVent add setting (zone, program, hour, minute, target)
TimeVentDel a b c                 TimeVent delete setting (zone, program, index)
TimeVentInfo                      TimeVent info
TimeVentModify a b c d e f        TimeVent set setting (zone, program, index, hour, minute, target)
TimeVentSetMode a b               TimeVent set mode (zone, mode)
VentCoolInfo                      VENT COOL Info
VentCoolParaGet                   VENT COOL get parameter values
VentCoolParaSet a b               VENT COOL set parameter a (0-11) to b
VentCtrlParaGet                   VENT CTRL get parameter values
VentCtrlParaSet a b               VENT CTRL set parameter a (0-4) to b
VentCtrlTempOverrule a b          VENT CTRL Set Temperature overule value, (a) node, (b) temperatue
WeatherStationInfo                Weather station info
WeatherStationTrigger             Weather station trigger
```

`nodeparaget` and `sensorinfo` aren't in this list, but both work.
</details>
