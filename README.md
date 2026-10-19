# DucoBox-ESPHome
DucoBox ESPHome component based on the work of [arnemauer/Ducobox-ESPEasy-Plugin](https://github.com/arnemauer/Ducobox-ESPEasy-Plugin/). It uses native ESPHome components where possible, e.g. UART, SPI, CC1101. It was tested with a DucoBox Silent. It should work with other DucoBoxes that were supported by the ESPEasy plugin.

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
In order to determine the node IDs of other devices in the network, click "Log All Duco Nodes" after pairing to show the available nodes, e.g. BOX, UCCO2, UCBAT, SWITCH, UCRH. These can be used to configure the ESP to request additional information for each node via the serial gateway, e.g. temperature, humidity or CO2 concentration. The ESP will also show up as a CO2 remote (UCCO2).

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
