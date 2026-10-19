#pragma once

#include "esphome/core/component.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"

#include <cmath>
#include <cstdlib>
#include <strings.h>

namespace esphome {
namespace duco_gateway {

// ─────────────────────────────────────────────────────────────────────────────
// Ventilation mode helpers
// ─────────────────────────────────────────────────────────────────────────────
static const char *vent_mode_to_str(int mode) {
  switch (mode) {
    case 0:  return "AUTO";
    case 1:  return "MAN1";
    case 2:  return "MAN2";
    case 3:  return "MAN3";
    case 4:  return "EMPT";
    case 5:  return "ALRM";
    case 11: return "CNT1";
    case 12: return "CNT2";
    case 13: return "CNT3";
    case 20: return "AUT0";
    case 21: return "AUT1";
    case 22: return "AUT2";
    case 23: return "AUT3";
    default: return "UNKN";
  }
}

// Map stat string to number (matches DucoStatusModes in P151)
static int vent_mode_from_str(const char *s) {
  const char *names[]   = {"AUTO","MAN1","MAN2","MAN3","EMPT","ALRM","CNT1","CNT2","CNT3","AUT0","AUT1","AUT2","AUT3"};
  const int   numbers[] = {0,1,2,3,4,5,11,12,13,20,21,22,23};
  for (int i = 0; i < 13; i++) {
    if (strstr(s, names[i]) != nullptr) return numbers[i];
  }
  return -1;
}

// ─────────────────────────────────────────────────────────────────────────────
// External sensor nodes (read with nodeparaget)
// ─────────────────────────────────────────────────────────────────────────────
static constexpr uint8_t MAX_EXT_SENSORS = 8;  // Keep in sync with __init__.py

// Parameters read per node, in polling order
enum ExtParam : uint8_t {
  EXT_PARAM_CO2,    // nodeparaget <node> 74  → CO2 ppm
  EXT_PARAM_TEMP,   // nodeparaget <node> 73  → temp /10
  EXT_PARAM_RH,     // nodeparaget <node> 75  → RH /100
  EXT_PARAM_COUNT,
};

static const uint8_t EXT_PARAM_IDS[EXT_PARAM_COUNT] = {74, 73, 75};

struct ExtSensorNode {
  uint8_t         node {0};
  sensor::Sensor *sensors[EXT_PARAM_COUNT] {nullptr, nullptr, nullptr};  // Indexed by ExtParam
};

// ─────────────────────────────────────────────────────────────────────────────
// Zones (zone valve nodes, e.g. VLV): mode and % from their row in the Network table
// ─────────────────────────────────────────────────────────────────────────────
static constexpr uint8_t MAX_ZONES = 4;  // Keep in sync with __init__.py

struct Zone {
  uint8_t                  node       {0};
  text_sensor::TextSensor *mode       {nullptr};
  sensor::Sensor          *percentage {nullptr};
};

// ─────────────────────────────────────────────────────────────────────────────
// Info commands, polled every info_poll_interval (DucoBox Energy; other boxes
// may answer "Unknown Cmd", after which that command is no longer sent).
// Most of these replies have no "Done" line, so a reply is considered complete
// after INFO_QUIET_MS without new data.
// ─────────────────────────────────────────────────────────────────────────────
enum InfoCmd : uint8_t {
  INFO_FAN,          // FanInfo            → exhaust / supply fan speed
  INFO_TEMPERATURE,  // TemperatureInfo    → ODA / SUP / ETA / EHA air temperatures
  INFO_BYPASS,       // BypassInfo         → bypass position, requested temperature
  INFO_FAN_CTRL,     // FanCtrlInfo        → exhaust / supply pressure
  INFO_FREEZE,       // FreezeProtectInfo  → frost protection state, heater level
  INFO_STATUS,       // StatusMonitorInfo  → active warnings
  INFO_FILTER,       // FilterInfo         → filter remaining days / %
  INFO_RTC,          // RtcGetTime         → box clock
  INFO_COUNT,
};

static const char *const INFO_COMMANDS[INFO_COUNT] = {
    "FanInfo", "TemperatureInfo", "BypassInfo", "FanCtrlInfo",
    "FreezeProtectInfo", "StatusMonitorInfo", "FilterInfo", "RtcGetTime"};

// ─────────────────────────────────────────────────────────────────────────────
// Phase state machine
//
// IDLE → LOGLEVEL → NETWORK → SENSORINFO → NODEPARA (for each ext node:
//        CO2 → TEMP → RH) → FANSPEED → INFO (each info command, when due) → IDLE
//
// Phases, parameters and info commands that have no sensor configured are skipped automatically.
// ─────────────────────────────────────────────────────────────────────────────
enum SerialPhase {
  PH_IDLE,
  PH_LOGLEVEL,      // coreloglevel 0x10
  PH_NETWORK,       // Network  → tabel met stat / %dbt / cval / cntdwn
  PH_SENSORINFO,    // sensorinfo → CO2 / RH / TEMP (boxsensor)
  PH_NODEPARA,      // nodeparaget <node> <param> for ext_sensors_[ext_idx_], param ext_param_
  PH_CUSTOM,        // command entered from HA text entity
  PH_FANSPEED,      // fanspeed → RPM
  PH_INFO,          // INFO_COMMANDS[info_idx_]
};

// ─────────────────────────────────────────────────────────────────────────────
class DucoGateway : public Component, public uart::UARTDevice {
 public:
  // ── Sensor setters ────────────────────────────────────────────────────────
  // Any of co2/temp/rh may be nullptr; that parameter is then not polled.
  void add_ext_sensor(uint8_t node, sensor::Sensor *co2, sensor::Sensor *temp, sensor::Sensor *rh) {
    if (ext_sensor_count_ >= MAX_EXT_SENSORS) {
      ESP_LOGE(TAG, "Too many ext_sensors (max %u); ignoring node %u", MAX_EXT_SENSORS, node);
      return;
    }
    ExtSensorNode &e = ext_sensors_[ext_sensor_count_++];
    e.node = node;
    e.sensors[EXT_PARAM_CO2]  = co2;
    e.sensors[EXT_PARAM_TEMP] = temp;
    e.sensors[EXT_PARAM_RH]   = rh;
  }
  void set_vent_mode_sensor(sensor::Sensor *s)               { vent_mode_ = s; }
  void set_vent_percentage_sensor(sensor::Sensor *s)         { vent_pct_ = s; }
  void set_fan_speed_sensor(sensor::Sensor *s)               { fan_speed_ = s; }
  void set_countdown_sensor(sensor::Sensor *s)               { countdown_ = s; }
  void set_box_temperature_sensor(sensor::Sensor *s)         { box_temp_ = s; }
  void set_box_humidity_sensor(sensor::Sensor *s)            { box_rh_ = s; }
  void set_vent_mode_text_sensor(text_sensor::TextSensor *s) { vent_mode_txt_ = s; }
  void set_poll_interval_ms(uint32_t ms)                     { poll_interval_ms_ = ms; }
  void set_info_poll_interval_ms(uint32_t ms)                { info_poll_interval_ms_ = ms; }

  // Either mode or percentage may be nullptr.
  void add_zone(uint8_t node, text_sensor::TextSensor *mode, sensor::Sensor *percentage) {
    if (zone_count_ >= MAX_ZONES) {
      ESP_LOGE(TAG, "Too many zones (max %u); ignoring node %u", MAX_ZONES, node);
      return;
    }
    Zone &z = zones_[zone_count_++];
    z.node = node;
    z.mode = mode;
    z.percentage = percentage;
  }

  // Info command sensors
  void set_exhaust_fan_speed_sensor(sensor::Sensor *s)               { exhaust_fan_speed_ = s; }
  void set_supply_fan_speed_sensor(sensor::Sensor *s)                { supply_fan_speed_ = s; }
  void set_outdoor_temperature_sensor(sensor::Sensor *s)             { temp_oda_ = s; }
  void set_supply_temperature_sensor(sensor::Sensor *s)              { temp_sup_ = s; }
  void set_extract_temperature_sensor(sensor::Sensor *s)             { temp_eta_ = s; }
  void set_exhaust_temperature_sensor(sensor::Sensor *s)             { temp_eha_ = s; }
  void set_bypass_position_sensor(sensor::Sensor *s)                 { bypass_position_ = s; }
  void set_bypass_target_temperature_sensor(sensor::Sensor *s)       { bypass_target_temp_ = s; }
  void set_exhaust_pressure_sensor(sensor::Sensor *s)                { exhaust_pressure_ = s; }
  void set_supply_pressure_sensor(sensor::Sensor *s)                 { supply_pressure_ = s; }
  void set_frost_protection_text_sensor(text_sensor::TextSensor *s)  { frost_state_txt_ = s; }
  void set_heater_level_sensor(sensor::Sensor *s)                    { heater_level_ = s; }
  void set_warnings_text_sensor(text_sensor::TextSensor *s)          { warnings_txt_ = s; }
  void set_problem_binary_sensor(binary_sensor::BinarySensor *s)     { problem_ = s; }
  void set_filter_warning_binary_sensor(binary_sensor::BinarySensor *s) { filter_warning_ = s; }
  void set_filter_remaining_days_sensor(sensor::Sensor *s)           { filter_days_ = s; }
  void set_filter_remaining_percent_sensor(sensor::Sensor *s)        { filter_pct_ = s; }
  void set_device_time_text_sensor(text_sensor::TextSensor *s)       { device_time_txt_ = s; }

  // Request an immediate network dump with all discovered nodes in logs.
  void log_all_nodes() {
    log_all_nodes_requested_ = true;
  }

  // Queue a custom command to be sent when the gateway is idle.
  void send_custom_command(const std::string &command) {
    if (phase_ != PH_IDLE || custom_command_requested_) {
      ESP_LOGW(TAG, "Cannot queue custom command while busy (phase %d)", (int) phase_);
      return;
    }

    size_t start = command.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
      ESP_LOGW(TAG, "Ignoring empty custom command");
      return;
    }

    size_t end = command.find_last_not_of(" \t\r\n");
    std::string trimmed = command.substr(start, end - start + 1);

    if (trimmed.size() >= sizeof(custom_command_)) {
      ESP_LOGW(TAG, "Custom command too long (%u >= %u)", (unsigned) trimmed.size(), (unsigned) sizeof(custom_command_));
      return;
    }

    strncpy(custom_command_, trimmed.c_str(), sizeof(custom_command_) - 1);
    custom_command_[sizeof(custom_command_) - 1] = '\0';
    custom_command_requested_ = true;
    ESP_LOGI(TAG, "Queued custom command: %s", custom_command_);
  }

  // ── ESPHome lifecycle ─────────────────────────────────────────────────────
  float get_setup_priority() const override { return setup_priority::DATA; }

  void setup() override {
    ESP_LOGI(TAG, "DucoGateway setup()");

    delay(200);
    flush_rx();
    setup_done_ = true;
  }

  void loop() override {
    // First loop() call: send coreloglevel here (not in setup) so rx_pump() runs.
    if (setup_done_ && !loglevel_sent_) {
      loglevel_sent_ = true;
      ESP_LOGD(TAG, "Sending coreloglevel 0x10");
      send_command("coreloglevel 0x10\r\n", PH_LOGLEVEL);
      return;
    }

    rx_pump();

    if (phase_ == PH_IDLE) {
      if (custom_command_requested_) {
        custom_command_requested_ = false;
        char cmd[136];
        snprintf(cmd, sizeof(cmd), "%s\r\n", custom_command_);
        send_command(cmd, PH_CUSTOM);
        return;
      }

      if (log_all_nodes_requested_) {
        log_all_nodes_requested_ = false;
        log_all_nodes_active_ = true;
        send_command("Network\r\n", PH_NETWORK);
        return;
      }

      if ((millis() - last_poll_ms_) >= poll_interval_ms_) {
        last_poll_ms_ = millis();
        log_all_nodes_active_ = false;
        send_command("Network\r\n", PH_NETWORK);
      }
    }
  }

 protected:
  static constexpr const char *TAG               = "duco_serial";
  static constexpr uint32_t    CMD_TIMEOUT_MS    = 8000;   // Should be enough even for slow responses, but short enough to recover from errors. Ducobox is usually very fast (responds in <100ms) but can be slow when the network table grows large or communication is bad with one of the nodes. Duco fails the command after roughly 5 seconds, but we set a slightly longer timeout here to also cover the case where we miss the "Failed" response.
  static constexpr uint32_t    INFO_QUIET_MS     = 300;    // Info reply complete after this long without data (the box prints a whole reply in well under 100 ms)

  SerialPhase phase_        {PH_IDLE};
  uint32_t    cmd_sent_ms_  {0};
  uint32_t    last_rx_ms_   {0};
  uint32_t    last_poll_ms_ {0};
  bool        got_done_     {false};
  bool        had_rx_       {false};
  bool        setup_done_   {false};
  bool        loglevel_sent_{false};
  bool        log_all_nodes_requested_{false};
  bool        log_all_nodes_active_{false};
  bool        nodeparaget_in_flight_{false};
  bool        phase_timed_out_{false};
  bool        custom_command_requested_{false};
  bool        fanspeed_unsupported_{false};
  uint32_t    poll_interval_ms_{15000};
  uint16_t    reply_lines_  {0};   // Lines received for the current command (including the echo)
  char        custom_command_[128] {0};

  // Info commands
  uint32_t    info_poll_interval_ms_{60000};
  uint32_t    last_info_poll_ms_ {0};
  bool        info_polled_       {false};
  uint8_t     info_idx_          {0};    // InfoCmd currently in flight
  uint8_t     info_unsupported_  {0};    // Bit per InfoCmd: box answered "Unknown Cmd"
  uint8_t     fan_section_       {0};    // FanInfo: 1 = exhaust, 2 = supply
  uint8_t     status_rows_       {0};    // StatusMonitorInfo: item rows seen
  bool        status_filter_     {false};
  char        status_active_[128] {0};   // StatusMonitorInfo: "item (level), ..."

  // Network table column indices (resolved from header row)
  uint8_t col_stat_   {255};
  uint8_t col_dbt_    {255};
  uint8_t col_cval_   {255};
  uint8_t col_cntdwn_ {255};

  char     line_buf_[256];
  uint16_t line_pos_ {0};

  ExtSensorNode ext_sensors_[MAX_EXT_SENSORS];
  uint8_t       ext_sensor_count_ {0};
  // nodeparaget cursor: entry in ext_sensors_ and ExtParam currently in flight
  uint8_t       ext_idx_   {0};
  uint8_t       ext_param_ {0};

  sensor::Sensor          *vent_mode_    {nullptr};
  sensor::Sensor          *vent_pct_     {nullptr};
  sensor::Sensor          *fan_speed_    {nullptr};
  sensor::Sensor          *countdown_    {nullptr};
  sensor::Sensor          *box_temp_     {nullptr};
  sensor::Sensor          *box_rh_       {nullptr};
  text_sensor::TextSensor *vent_mode_txt_{nullptr};

  Zone    zones_[MAX_ZONES];
  uint8_t zone_count_ {0};

  sensor::Sensor              *exhaust_fan_speed_ {nullptr};
  sensor::Sensor              *supply_fan_speed_  {nullptr};
  sensor::Sensor              *temp_oda_          {nullptr};
  sensor::Sensor              *temp_sup_          {nullptr};
  sensor::Sensor              *temp_eta_          {nullptr};
  sensor::Sensor              *temp_eha_          {nullptr};
  sensor::Sensor              *bypass_position_   {nullptr};
  sensor::Sensor              *bypass_target_temp_{nullptr};
  sensor::Sensor              *exhaust_pressure_  {nullptr};
  sensor::Sensor              *supply_pressure_   {nullptr};
  text_sensor::TextSensor     *frost_state_txt_   {nullptr};
  sensor::Sensor              *heater_level_      {nullptr};
  text_sensor::TextSensor     *warnings_txt_      {nullptr};
  binary_sensor::BinarySensor *problem_           {nullptr};
  binary_sensor::BinarySensor *filter_warning_    {nullptr};
  sensor::Sensor              *filter_days_       {nullptr};
  sensor::Sensor              *filter_pct_        {nullptr};
  text_sensor::TextSensor     *device_time_txt_   {nullptr};

  // ── Serial helpers ────────────────────────────────────────────────────────
  bool is_nodeparaget_phase_(SerialPhase p) const {
    return p == PH_NODEPARA;
  }

  void flush_rx() {
    while (available())
      read();
  }

  void send_command(const char *cmd, SerialPhase next_phase) {
    if (is_nodeparaget_phase_(next_phase) && nodeparaget_in_flight_) {
      ESP_LOGW(TAG, "Blocked overlapping nodeparaget command in phase %d", (int) next_phase);
      return;
    }

    flush_rx();
    line_pos_    = 0;
    reply_lines_ = 0;
    got_done_    = false;
    had_rx_      = false;
    phase_timed_out_ = false;
    phase_       = next_phase;
    cmd_sent_ms_ = millis();
    last_rx_ms_  = millis();
    // Send byte-by-byte with small delay — Ducobox RX buffer is very small
    for (const char *p = cmd; *p; p++) {
      write_byte((uint8_t)*p);
      delay(5);
    }
    char log_cmd[64];
    strncpy(log_cmd, cmd, sizeof(log_cmd) - 1);
    log_cmd[sizeof(log_cmd)-1] = '\0';
    for (char *p = log_cmd; *p; p++) if (*p=='\r'||*p=='\n') { *p='\0'; break; }
    if (is_nodeparaget_phase_(next_phase)) {
      nodeparaget_in_flight_ = true;
    }
    ESP_LOGD(TAG, "TX >> \"%s\"  (phase %d)", log_cmd, (int)next_phase);
  }

  void rx_pump() {
    if (phase_ == PH_IDLE) return;

    if ((millis() - cmd_sent_ms_) > CMD_TIMEOUT_MS) {
      ESP_LOGW(TAG, "Timeout in phase %d (had_rx=%d)", (int)phase_, (int)had_rx_);
      phase_timed_out_ = true;
      advance_phase();
      return;
    }

    while (available()) {
      char c = (char)read();
      had_rx_     = true;
      last_rx_ms_ = millis();

      // Ducobox uses \r as line terminator (sometimes \r\n, sometimes just \r)
      if (c == '\r' || c == '\n') {
        if (line_pos_ > 0) {
          line_buf_[line_pos_] = '\0';
          reply_lines_++;
          process_line(line_buf_);
          line_pos_ = 0;
        }
      } else {
        if (line_pos_ < (sizeof(line_buf_) - 1))
          line_buf_[line_pos_++] = c;
      }
    }

    // Info replies mostly have no "Done": complete once more than the echo has
    // arrived and the box has been quiet for a moment.
    if (!got_done_ && phase_ == PH_INFO && reply_lines_ >= 2 && (millis() - last_rx_ms_) >= INFO_QUIET_MS)
      got_done_ = true;

    if (got_done_) advance_phase();
  }

  // ── Line parser ───────────────────────────────────────────────────────────
  void process_line(const char *line) {
    ESP_LOGD(TAG, "RX << \"%s\"", line);

    if (phase_ == PH_CUSTOM) {
      if (strstr(line, "Done") != nullptr) {
        ESP_LOGI(TAG, "CMD << Done");
        got_done_ = true;
        return;
      }
      if (strstr(line, "Failed") != nullptr) {
        ESP_LOGW(TAG, "CMD << Failed");
        got_done_ = true;
        return;
      }
      if (strstr(line, "Unknown Cmd") != nullptr) {
        ESP_LOGW(TAG, "CMD << Unknown Cmd");
        got_done_ = true;
        return;
      }

      ESP_LOGI(TAG, "CMD << %s", line);
      return;
    }

    // The box answers "^" / "Unknown Cmd" (no Done/Failed) to commands it doesn't have
    if (strstr(line, "Unknown Cmd") != nullptr) {
      mark_unsupported_();
      got_done_ = true;
      return;
    }
    if (strstr(line, "Done") != nullptr) { got_done_ = true; return; }
    if (strstr(line, "Failed") != nullptr) {
      ESP_LOGW(TAG, "Command failed in phase %d", (int) phase_);
      got_done_ = true;
      return;
    }
    if (phase_ == PH_LOGLEVEL) { return; }

    // ── Network table ─────────────────────────────────────────────────────
    if (phase_ == PH_NETWORK) {
      if (strstr(line, "--- end list ---") != nullptr) {
        got_done_ = true;
        return;
      }

      // Header row looks like: "Node| Zone| ...| stat| ...| %dbt| ...| cval| ...| cntdwn|..."
      // Data row for ducobox (node 1): "   1|  --|  --|AUTO|  --|  25|  --|  30|  --|   0|..."
      if (strstr(line, "stat") != nullptr && strstr(line, "cval") != nullptr) {
        // This is the column header row — find column indices
        parse_network_header(line);
        if (log_all_nodes_active_) {
          ESP_LOGI(TAG, "Header: %s", line);
        }
        return;
      }
      // Node data rows
      int node = -1;
      if (sscanf(line, " %d|", &node) == 1) {
        if (log_all_nodes_active_) {
          ESP_LOGI(TAG, "Node %d: %s", node, line);
        }
        // Node 1 (the box) updates the box-level sensors, zone nodes their zone sensors
        if (node == 1) {
          parse_network_row(line, vent_mode_, vent_mode_txt_, vent_pct_, countdown_);
        } else {
          for (uint8_t i = 0; i < zone_count_; i++) {
            if (zones_[i].node == node)
              parse_network_row(line, nullptr, zones_[i].mode, zones_[i].percentage, nullptr);
          }
        }
      }
      return;
    }

    // ── sensorinfo ────────────────────────────────────────────────────────
    if (phase_ == PH_SENSORINFO) {
      unsigned int raw = 0;
      // "  CO2 :  627 [ppm]" or "  CO2: 627 [ppm]"
      if (strncmp(line, "  CO", 4) == 0) {
        const char *colon = strchr(line, ':');
        if (colon && sscanf(colon+1, " %u", &raw) == 1) {
          ESP_LOGD(TAG, "sensorinfo CO2=%u ppm", raw);
          // box sensor CO2 — not used separately; ext_sensors_ are for external nodes
        }
      } else if (strncmp(line, "  RH", 4) == 0) {
        const char *colon = strchr(line, ':');
        if (colon && sscanf(colon+1, " %u", &raw) == 1) {
          float rh = (float)raw / 100.0f;
          ESP_LOGD(TAG, "sensorinfo RH raw=%u -> %.2f%%", raw, rh);
          if (box_rh_) box_rh_->publish_state(rh);
        }
      } else if (strncmp(line, "  TEMP", 6) == 0 && strstr(line, "ADJUST") == nullptr) {
        // "TEMP ADJUST." is a temperature offset setting, printed even without a box sensor
        const char *colon = strchr(line, ':');
        if (colon && sscanf(colon+1, " %u", &raw) == 1) {
          float temp = (float)raw / 10.0f;
          ESP_LOGD(TAG, "sensorinfo TEMP raw=%u -> %.1f C", raw, temp);
          if (box_temp_) box_temp_->publish_state(temp);
        }
      }
      return;
    }

    // ── nodeparaget response: "  --> 492" ─────────────────────────────────
    if (phase_ == PH_NODEPARA) {
      unsigned int raw = 0;
      if (sscanf(line, "  --> %u", &raw) == 1) {
        const ExtSensorNode &e = ext_sensors_[ext_idx_];
        sensor::Sensor *s = e.sensors[ext_param_];
        if (ext_param_ == EXT_PARAM_CO2) {
          ESP_LOGD(TAG, "node %u CO2=%u ppm", e.node, raw);
          if (s) s->publish_state((float)raw);
        } else if (ext_param_ == EXT_PARAM_TEMP) {
          float temp = (float)raw / 10.0f;
          ESP_LOGD(TAG, "node %u TEMP raw=%u -> %.1f C", e.node, raw, temp);
          if (s) s->publish_state(temp);
        } else if (ext_param_ == EXT_PARAM_RH) {
          float rh = (float)raw / 100.0f;
          ESP_LOGD(TAG, "node %u RH raw=%u -> %.2f%%", e.node, raw, rh);
          if (s) s->publish_state(rh);
        }
      }
      return;
    }

    // ── fanspeed ──────────────────────────────────────────────────────────
    if (phase_ == PH_FANSPEED) {
      unsigned int filtered = 0;
      // "  FanSpeed: Actual 389 [rpm] - Filtered 391 [rpm]"
      if (sscanf(line, "  FanSpeed: Actual %*u %*s %*s %*s %u", &filtered) == 1) {
        ESP_LOGD(TAG, "FanSpeed filtered=%u rpm", filtered);
        if (fan_speed_) fan_speed_->publish_state((float)filtered);
      }
      return;
    }

    if (phase_ == PH_INFO) {
      parse_info_line_(line);
      return;
    }
  }

  // Box answered "Unknown Cmd": stop sending a command it doesn't have.
  void mark_unsupported_() {
    if (phase_ == PH_FANSPEED) {
      fanspeed_unsupported_ = true;
      ESP_LOGW(TAG, "Box doesn't support \"fanspeed\"; fan_speed_sensor won't update");
    } else if (phase_ == PH_INFO) {
      info_unsupported_ |= (1u << info_idx_);
      ESP_LOGW(TAG, "Box doesn't support \"%s\"; its sensors won't update", INFO_COMMANDS[info_idx_]);
    } else {
      ESP_LOGW(TAG, "Unknown command in phase %d", (int) phase_);
    }
  }

  // ── Info command parsers ──────────────────────────────────────────────────
  static const char *skip_spaces_(const char *s) {
    while (*s == ' ' || *s == '\t') s++;
    return s;
  }

  static bool starts_with_(const char *s, const char *prefix) {
    return strncmp(s, prefix, strlen(prefix)) == 0;
  }

  // Copy s without leading/trailing whitespace into buf
  static void copy_trimmed_(char *buf, size_t size, const char *s) {
    s = skip_spaces_(s);
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t')) n--;
    if (n >= size) n = size - 1;
    memcpy(buf, s, n);
    buf[n] = '\0';
  }

  void parse_info_line_(const char *line) {
    const char *s = skip_spaces_(line);
    const char *colon = strchr(s, ':');
    int v = 0;

    switch (info_idx_) {
      case INFO_FAN:
        // "FanSpeed Exhaust:" / "FanSpeed Supply:" start a section; then "- Filtered :  307 [rpm]"
        if (starts_with_(s, "FanSpeed Exhaust")) {
          fan_section_ = 1;
        } else if (starts_with_(s, "FanSpeed Supply")) {
          fan_section_ = 2;
        } else if (starts_with_(s, "- Filtered") && colon && sscanf(colon + 1, "%d", &v) == 1) {
          sensor::Sensor *t = fan_section_ == 1 ? exhaust_fan_speed_ : fan_section_ == 2 ? supply_fan_speed_ : nullptr;
          ESP_LOGD(TAG, "FanInfo %s=%d rpm", fan_section_ == 1 ? "exhaust" : "supply", v);
          if (t) t->publish_state((float) v);
        }
        break;

      case INFO_TEMPERATURE:
        // "- SENSOR ETA:   20.0 [C]  (OK)"
        if (starts_with_(s, "- SENSOR ") && colon) {
          const char *code = s + 9;
          sensor::Sensor *t = starts_with_(code, "ODA") ? temp_oda_ : starts_with_(code, "SUP") ? temp_sup_
                            : starts_with_(code, "ETA") ? temp_eta_ : starts_with_(code, "EHA") ? temp_eha_ : nullptr;
          // strtof, not sscanf("%f"): ESPHome builds for ESP8266 without scanf float support
          char *end = nullptr;
          float temp = strtof(colon + 1, &end);
          if (end != colon + 1) {
            bool ok = strstr(colon, "(OK)") != nullptr;
            ESP_LOGD(TAG, "TemperatureInfo %.3s=%.1f C%s", code, temp, ok ? "" : " (sensor not OK)");
            if (t) t->publish_state(ok ? temp : NAN);
          }
        }
        break;

      case INFO_BYPASS:
        // "- Bypass position : 0 [%]" / "- Requested temperature : 213 [.1°C]"
        if (colon && sscanf(colon + 1, "%d", &v) == 1) {
          if (starts_with_(s, "- Bypass position")) {
            ESP_LOGD(TAG, "BypassInfo position=%d%%", v);
            if (bypass_position_) bypass_position_->publish_state((float) v);
          } else if (starts_with_(s, "- Requested temperature")) {
            ESP_LOGD(TAG, "BypassInfo requested temperature=%.1f C", v / 10.0f);
            if (bypass_target_temp_) bypass_target_temp_->publish_state(v / 10.0f);
          }
        }
        break;

      case INFO_FAN_CTRL:
        // "p exhaust:   105 dPa" / "p supply:    27 dPa" (published in Pa)
        if (colon && sscanf(colon + 1, "%d", &v) == 1) {
          if (starts_with_(s, "p exhaust:")) {
            ESP_LOGD(TAG, "FanCtrlInfo exhaust pressure=%.1f Pa", v / 10.0f);
            if (exhaust_pressure_) exhaust_pressure_->publish_state(v / 10.0f);
          } else if (starts_with_(s, "p supply:")) {
            ESP_LOGD(TAG, "FanCtrlInfo supply pressure=%.1f Pa", v / 10.0f);
            if (supply_pressure_) supply_pressure_->publish_state(v / 10.0f);
          }
        }
        break;

      case INFO_FREEZE:
        // "State : INACTIVE" / "Heater level : 0"
        if (colon && starts_with_(s, "State ")) {
          char state[24];
          copy_trimmed_(state, sizeof(state), colon + 1);
          ESP_LOGD(TAG, "FreezeProtectInfo state=%s", state);
          if (frost_state_txt_) frost_state_txt_->publish_state(state);
        } else if (colon && starts_with_(s, "Heater level") && sscanf(colon + 1, "%d", &v) == 1) {
          ESP_LOGD(TAG, "FreezeProtectInfo heater level=%d%%", v);
          if (heater_level_) heater_level_->publish_state((float) v);
        }
        break;

      case INFO_STATUS:
        parse_status_row_(s);
        break;

      case INFO_FILTER: {
        // "Remaining lifetime : 0 days (0%)"
        int pct = 0;
        if (colon && starts_with_(s, "Remaining lifetime") && sscanf(colon + 1, "%d days (%d%%)", &v, &pct) == 2) {
          ESP_LOGD(TAG, "FilterInfo remaining=%d days (%d%%)", v, pct);
          if (filter_days_) filter_days_->publish_state((float) v);
          if (filter_pct_) filter_pct_->publish_state((float) pct);
        }
        break;
      }

      case INFO_RTC:
        // "[RTC] SATURDAY 12/02/2124 13:36:32" (published as printed, without "[RTC]")
        if (starts_with_(s, "[RTC]")) {
          char time_str[40];
          copy_trimmed_(time_str, sizeof(time_str), s + 5);
          ESP_LOGD(TAG, "RtcGetTime %s", time_str);
          if (device_time_txt_) device_time_txt_->publish_state(time_str);
        }
        break;

      default:
        break;
    }
  }

  // StatusMonitorInfo row: "9     | Replace filters              | 09.19.01 | 3     | warning"
  // An item is active when its level is not "-".
  void parse_status_row_(const char *s) {
    int index = 0;
    if (sscanf(s, "%d", &index) != 1) return;  // header row

    char buf[96];
    copy_trimmed_(buf, sizeof(buf), s);
    char *fields[5] = {nullptr};
    uint8_t n = 0;
    for (char *tok = strtok(buf, "|"); tok != nullptr && n < 5; tok = strtok(nullptr, "|")) {
      while (*tok == ' ') tok++;
      char *end = tok + strlen(tok);
      while (end > tok && end[-1] == ' ') *--end = '\0';
      fields[n++] = tok;
    }
    if (n < 5) return;

    status_rows_++;
    const char *item = fields[1], *level = fields[4];
    if (strcmp(level, "-") == 0) return;

    ESP_LOGD(TAG, "StatusMonitorInfo active: %s (%s, code %s, count %s)", item, level, fields[2], fields[3]);
    size_t used = strlen(status_active_);
    snprintf(status_active_ + used, sizeof(status_active_) - used, "%s%s (%s)", used ? ", " : "", item, level);
    if (strstr(item, "filter") != nullptr || strstr(item, "Filter") != nullptr) status_filter_ = true;
  }

  // Called when an info command has finished (Done, quiet or timeout)
  void finish_info_() {
    if (info_idx_ == INFO_STATUS && status_rows_ > 0) {
      bool active = status_active_[0] != '\0';
      if (warnings_txt_) warnings_txt_->publish_state(active ? status_active_ : "None");
      if (problem_) problem_->publish_state(active);
      if (filter_warning_) filter_warning_->publish_state(status_filter_);
    }
  }

  // ── Network header parser ─────────────────────────────────────────────────
  // Finds column positions of stat / %dbt / cval / cntdwn in the pipe-delimited header
  void parse_network_header(const char *line) {
    col_stat_ = col_dbt_ = col_cval_ = col_cntdwn_ = 255;

    // The header starts with the node column. If it doesn't, the start of the
    // reply was lost (e.g. serial buffer overflow) and every column index would
    // be off, so skip the table rather than publish values from wrong columns.
    if (strncasecmp(skip_spaces_(line), "node", 4) != 0) {
      ESP_LOGW(TAG, "Incomplete Network header (start of reply lost); skipping this table");
      return;
    }

    char buf[256];
    strncpy(buf, line, sizeof(buf)-1);
    buf[sizeof(buf)-1] = '\0';
    uint8_t col = 0;
    char *tok = strtok(buf, "|");
    while (tok != nullptr) {
      if (strstr(tok, "stat"))   col_stat_   = col;
      if (strstr(tok, "%dbt"))   col_dbt_    = col;
      if (strstr(tok, "cval"))   col_cval_   = col;
      if (strstr(tok, "cntdwn")) col_cntdwn_ = col;
      col++;
      tok = strtok(nullptr, "|");
    }
    ESP_LOGD(TAG, "Network header: stat=%d dbt=%d cval=%d cntdwn=%d",
             col_stat_, col_dbt_, col_cval_, col_cntdwn_);

    if (log_all_nodes_active_) {
      ESP_LOGI(TAG, "==== Duco Node List (Network) ====");
    }
  }

  // ── Network data row parser ───────────────────────────────────────────────
  // Publishes the row's stat / %dbt / cntdwn to the given targets (any may be nullptr)
  void parse_network_row(const char *line, sensor::Sensor *mode_sensor, text_sensor::TextSensor *mode_txt,
                         sensor::Sensor *pct_sensor, sensor::Sensor *countdown_sensor) {
    char buf[256];
    strncpy(buf, line, sizeof(buf)-1);
    buf[sizeof(buf)-1] = '\0';
    uint8_t col = 0;
    char *tok = strtok(buf, "|");
    while (tok != nullptr) {
      // Trim leading/trailing spaces
      while (*tok == ' ') tok++;
      char *end = tok + strlen(tok) - 1;
      while (end > tok && *end == ' ') { *end = '\0'; end--; }

      if (col == col_stat_ && col_stat_ != 255) {
        int mode = vent_mode_from_str(tok);
        ESP_LOGD(TAG, "Network stat=\"%s\" -> %d", tok, mode);
        if (mode >= 0) {
          if (mode_sensor) mode_sensor->publish_state((float)mode);
          if (mode_txt)    mode_txt->publish_state(vent_mode_to_str(mode));
        }
      }
      if (col == col_dbt_ && col_dbt_ != 255) {
        int v = 0;
        if (sscanf(tok, "%d", &v) == 1) {
          ESP_LOGD(TAG, "Network %%dbt=%d", v);
          if (pct_sensor) pct_sensor->publish_state((float)v);
        }
      }
      if (col == col_cval_ && col_cval_ != 255) {
        // cval in Network table is fan % (not mode number)
        int v = 0;
        if (sscanf(tok, "%d", &v) == 1) {
          ESP_LOGD(TAG, "Network cval=%d", v);
          // cval = current fan value (%) — map to countdown if no dedicated sensor
        }
      }
      if (col == col_cntdwn_ && col_cntdwn_ != 255) {
        int v = 0;
        if (sscanf(tok, "%d", &v) == 1) {
          ESP_LOGD(TAG, "Network cntdwn=%d", v);
          if (countdown_sensor) countdown_sensor->publish_state((float)v);
        }
      }
      col++;
      tok = strtok(nullptr, "|");
    }
  }

  // ── Phase sequencer ───────────────────────────────────────────────────────
  void advance_phase() {
    // Clear in-flight marker as soon as a nodeparaget phase completes
    // (either by Done, Failed or timeout) before issuing the next request.
    if (is_nodeparaget_phase_(phase_)) {
      nodeparaget_in_flight_ = false;

      // If a nodeparaget command timed out, abort the chained parameter reads.
      // Late replies are ambiguous ("--> <value>") and can otherwise be
      // misattributed to the next read (e.g. CO2 interpreted as TEMP, or one
      // node's value published for the next node).
      if (phase_timed_out_) {
        ESP_LOGW(TAG, "Aborting nodeparaget chain after timeout (node %u, param %u)",
                 ext_sensors_[ext_idx_].node, EXT_PARAM_IDS[ext_param_]);
        phase_timed_out_ = false;
        phase_ = PH_IDLE;
        last_poll_ms_ = millis();
        return;
      }
    }

    switch (phase_) {

      case PH_LOGLEVEL:
        if (had_rx_) {
          ESP_LOGD(TAG, "coreloglevel 0x10: got response");
        } else {
          ESP_LOGW(TAG, "coreloglevel 0x10: NO response! Check wiring/GPIO2.");
        }
        last_poll_ms_ = millis() - poll_interval_ms_;  // poll immediately
        phase_ = PH_IDLE;
        break;

      case PH_NETWORK:
        log_all_nodes_active_ = false;
        // Next: sensorinfo (box sensor) if any box sensor is configured
        if (box_temp_ || box_rh_) {
          send_command("sensorinfo\r\n", PH_SENSORINFO);
        } else {
          go_to_ext_sensor();
        }
        break;

      case PH_SENSORINFO:
        go_to_ext_sensor();
        break;

      case PH_NODEPARA:
        send_next_nodepara(ext_idx_, ext_param_ + 1);
        break;

      case PH_CUSTOM:
        phase_ = PH_IDLE;
        break;

      case PH_FANSPEED:
        go_to_info();
        break;

      case PH_INFO:
        finish_info_();
        send_next_info(info_idx_ + 1);
        break;

      default:
        phase_ = PH_IDLE;
        break;
    }
  }

  void go_to_ext_sensor() {
    send_next_nodepara(0, 0);
  }

  // Send the first configured nodeparaget at or after cursor (idx, param),
  // or move on to fanspeed when no external reads are left in this cycle.
  void send_next_nodepara(uint8_t idx, uint8_t param) {
    for (; idx < ext_sensor_count_; idx++, param = 0) {
      for (; param < EXT_PARAM_COUNT; param++) {
        if (ext_sensors_[idx].sensors[param] == nullptr) continue;
        ext_idx_   = idx;
        ext_param_ = param;
        char cmd[32];
        snprintf(cmd, sizeof(cmd), "nodeparaget %u %u\r\n", ext_sensors_[idx].node, EXT_PARAM_IDS[param]);
        send_command(cmd, PH_NODEPARA);
        return;
      }
    }
    go_to_fanspeed();
  }

  void go_to_fanspeed() {
    if (fan_speed_ && !fanspeed_unsupported_) {
      send_command("fanspeed\r\n", PH_FANSPEED);
    } else {
      go_to_info();
    }
  }

  // Run the info commands when they are due (and on the first cycle after boot).
  // Compares poll cycle start times, so an info_poll_interval that is a multiple
  // of poll_interval runs every Nth cycle instead of slipping one cycle.
  void go_to_info() {
    if (!info_polled_ || (last_poll_ms_ - last_info_poll_ms_) >= info_poll_interval_ms_) {
      info_polled_ = true;
      last_info_poll_ms_ = last_poll_ms_;
      send_next_info(0);
      return;
    }
    ESP_LOGD(TAG, "Poll cycle complete");
    phase_ = PH_IDLE;
  }

  bool info_wanted_(uint8_t idx) const {
    switch (idx) {
      case INFO_FAN:         return exhaust_fan_speed_ || supply_fan_speed_;
      case INFO_TEMPERATURE: return temp_oda_ || temp_sup_ || temp_eta_ || temp_eha_;
      case INFO_BYPASS:      return bypass_position_ || bypass_target_temp_;
      case INFO_FAN_CTRL:    return exhaust_pressure_ || supply_pressure_;
      case INFO_FREEZE:      return frost_state_txt_ || heater_level_;
      case INFO_STATUS:      return warnings_txt_ || problem_ || filter_warning_;
      case INFO_FILTER:      return filter_days_ || filter_pct_;
      case INFO_RTC:         return device_time_txt_ != nullptr;
      default:               return false;
    }
  }

  // Send the first wanted, supported info command at or after idx; end the cycle when none is left.
  void send_next_info(uint8_t idx) {
    for (; idx < INFO_COUNT; idx++) {
      if (!info_wanted_(idx) || (info_unsupported_ & (1u << idx))) continue;
      info_idx_ = idx;
      fan_section_ = 0;
      status_rows_ = 0;
      status_filter_ = false;
      status_active_[0] = '\0';
      char cmd[32];
      snprintf(cmd, sizeof(cmd), "%s\r\n", INFO_COMMANDS[idx]);
      send_command(cmd, PH_INFO);
      return;
    }
    ESP_LOGD(TAG, "Poll cycle complete");
    phase_ = PH_IDLE;
  }
};

}  // namespace duco_gateway
}  // namespace esphome