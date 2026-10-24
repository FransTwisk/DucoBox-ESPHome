#pragma once

#include "esphome/core/component.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/number/number.h"
#include "esphome/components/select/select.h"
#include "esphome/components/switch/switch.h"
#include "esphome/core/helpers.h"
#include "esphome/core/preferences.h"
#ifdef USE_TIME
#include "esphome/components/time/real_time_clock.h"
#endif

#include <algorithm>
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
// Zones (zone valve nodes, e.g. VLV): mode, % and countdown from their row in the Network table
// ─────────────────────────────────────────────────────────────────────────────
static constexpr uint8_t MAX_ZONES = 4;  // Keep in sync with __init__.py

struct Zone {
  uint8_t                  node       {0};
  text_sensor::TextSensor *mode       {nullptr};
  sensor::Sensor          *percentage {nullptr};
  sensor::Sensor          *countdown  {nullptr};  // seconds left of a manual mode
  select::Select          *control    {nullptr};  // Auto / Min / Med / Max (NodeSetOverrule)
  switch_::Switch         *permanent_switch {nullptr};  // keep the override until changed
  uint8_t                  levels[3]  {0, 0, 0};  // Manual 1/2/3 % (NodeConfig 7/8/9), 0 = not read yet
  uint16_t                 manual_timeout_min {0};  // NodeConfig 10, 0 = not read yet
  uint32_t                 override_start_ms  {0};
  uint32_t                 override_ms        {0};  // Override set from here runs this long; 0 = none
  bool                     write_pending      {false};  // NodeSetOverrule queued, not yet sent
  bool                     permanent          {false};  // the override isn't ended after the manual timeout
  bool                     overridden         {false};  // last Network row had an override (ovrl)
  bool                     controller_permanent {false};  // last Network row had CNT1-3 (wall controller)
  ESPPreferenceObject      permanent_pref;  // keeps `permanent` across an ESP restart
};

static constexpr uint8_t ZONE_OVERRIDE_NONE = 255;              // NodeSetOverrule value / ovrl column: no override
static const uint8_t     ZONE_DEFAULT_LEVELS[3] = {10, 50, 100};  // Until the zone's own levels have been read

// One row of the Network table; -1 = column missing or not a number
struct NetworkRow {
  int mode      {-1};  // vent_mode_from_str() of stat
  int pct       {-1};  // %dbt
  int countdown {-1};  // cntdwn (seconds)
  int ovrl      {-1};  // overrule (255 = none)
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
// Settings (number / select platforms). Written with the *Set commands through a
// small queue; read back with the *Get commands after each write and every
// settings_poll_interval, so the entities always show what the box reports.
// ─────────────────────────────────────────────────────────────────────────────
enum SettingSource : uint8_t {
  SETTING_NODE_CONFIG,  // NodeConfigGet <node> / NodeConfigSet <node> <param> <value>
  SETTING_GENERAL,      // SettingsParaGet / SettingsParaSet <param> <value>
};

struct SettingNumber {
  number::Number *number {nullptr};
  uint8_t         source {SETTING_NODE_CONFIG};
  uint8_t         node   {0};
  uint8_t         param  {0};
  float           scale  {1.0f};  // entity value = box value * scale
};

// Read-back groups, one bit each in settings_pending_
static constexpr uint8_t GROUP_BYPASS     = 0;     // BypassParaGet
static constexpr uint8_t GROUP_GENERAL    = 1;     // SettingsParaGet
static constexpr uint8_t GROUP_NODE_FIRST = 2;     // NodeConfigGet setting_nodes_[bit - GROUP_NODE_FIRST]
static constexpr uint8_t GROUP_NONE       = 0xFF;
static constexpr uint8_t GROUP_POLL       = 0xFE;  // After the write, poll the Network table right away

static constexpr uint8_t MAX_SETTING_NUMBERS = 24;  // Keep in sync with number.py
static constexpr uint8_t MAX_SETTING_NODES   = 6;   // Distinct nodes with NodeConfig numbers (bits 2..7)
static constexpr uint8_t SET_QUEUE_SIZE      = 6;

struct QueuedSet {
  char    cmd[40] {0};
  uint8_t group   {GROUP_NONE};  // read back after writing
};

// ─────────────────────────────────────────────────────────────────────────────
// Phase state machine
//
// IDLE → LOGLEVEL → NETWORK → SENSORINFO → NODEPARA (for each ext node:
//        CO2 → TEMP → RH) → FANSPEED → INFO (each info command, when due)
//        → SETTING (each settings Get, when due) → IDLE
//
// Queued settings writes (SET) and their read-back run between poll cycles.
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
  PH_SET,           // queued *Set command
  PH_SETTING,       // settings Get for read-back group setting_group_
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

  // Any of mode / percentage / countdown may be nullptr.
  void add_zone(uint8_t node, text_sensor::TextSensor *mode, sensor::Sensor *percentage, sensor::Sensor *countdown) {
    Zone *z = find_or_add_zone_(node);
    if (z == nullptr) return;
    z->mode = mode;
    z->percentage = percentage;
    z->countdown = countdown;
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

  // ── Settings ──────────────────────────────────────────────────────────────
  void set_settings_poll_interval_ms(uint32_t ms)                    { settings_poll_interval_ms_ = ms; }
  void set_bypass_mode_select(select::Select *s)                     { bypass_select_ = s; }
#ifdef USE_TIME
  void set_time(time::RealTimeClock *t)                              { time_ = t; }
#endif

  void add_setting_number(number::Number *n, uint8_t source, uint8_t node, uint8_t param, float scale) {
    if (setting_count_ >= MAX_SETTING_NUMBERS) {
      ESP_LOGE(TAG, "Too many duco_gateway numbers (max %u)", MAX_SETTING_NUMBERS);
      return;
    }
    if (source == SETTING_NODE_CONFIG && node_group_(node) == GROUP_NONE) {
      if (setting_node_count_ >= MAX_SETTING_NODES) {
        ESP_LOGE(TAG, "Numbers for too many nodes (max %u); ignoring node %u", MAX_SETTING_NODES, node);
        return;
      }
      setting_nodes_[setting_node_count_++] = node;
    }
    SettingNumber &s = settings_[setting_count_++];
    s.number = n;
    s.source = source;
    s.node = node;
    s.param = param;
    s.scale = scale;
  }

  // Called by DucoNumber / DucoSelect when changed from HA
  void write_setting_number(const number::Number *n, float value) {
    for (uint8_t i = 0; i < setting_count_; i++) {
      const SettingNumber &s = settings_[i];
      if (s.number != n) continue;
      long raw = lroundf(value / s.scale);
      char cmd[40];
      if (s.source == SETTING_NODE_CONFIG) {
        snprintf(cmd, sizeof(cmd), "NodeConfigSet %u %u %ld", s.node, s.param, raw);
        queue_set_(cmd, node_group_(s.node));
      } else {
        snprintf(cmd, sizeof(cmd), "SettingsParaSet %u %ld", s.param, raw);
        queue_set_(cmd, GROUP_GENERAL);
      }
      return;
    }
  }

  void write_bypass_mode(size_t index) {
    if (index > 2) return;
    char cmd[40];
    snprintf(cmd, sizeof(cmd), "BypassParaSet 0 %u", (unsigned) index);
    queue_set_(cmd, GROUP_BYPASS);
  }

  // Zone ventilation select (zone_ventilation): Auto / Min / Med / Max for one zone valve.
  // Min/Med/Max override the zone with its own speed 1/2/3 level and, like a wall
  // controller, end after the zone's manual timeout.
  void add_zone_control(uint8_t node, select::Select *s) {
    Zone *z = find_or_add_zone_(node);
    if (z == nullptr) return;
    z->control = s;
    // The zone's levels and manual timeout come from NodeConfigGet <node>
    if (node_group_(node) == GROUP_NONE) {
      if (setting_node_count_ >= MAX_SETTING_NODES) {
        ESP_LOGE(TAG, "Settings for too many nodes (max %u); zone %u uses default levels", MAX_SETTING_NODES, node);
        return;
      }
      setting_nodes_[setting_node_count_++] = node;
    }
  }

  void write_zone_control(uint8_t node, size_t index) {
    Zone *z = find_zone_(node);
    if (z == nullptr || index > 3) return;
    if (index == 0) {
      z->override_ms = 0;
      set_zone_permanent_(*z, false);
      publish_zone_permanent_(*z);
      queue_zone_override_(node, ZONE_OVERRIDE_NONE);
      return;
    }
    uint8_t level = z->levels[index - 1] != 0 ? z->levels[index - 1] : ZONE_DEFAULT_LEVELS[index - 1];
    if (z->permanent) {
      z->override_ms = 0;  // stays until changed
    } else {
      start_zone_override_timer_(*z);
    }
    queue_zone_override_(node, level);
  }

  // Zone permanent switch (zone_permanent): keeps the zone's override until it's changed,
  // instead of ending it after the zone's manual timeout. Like the box's permanent mode,
  // it applies to the current Min / Med / Max: in Auto there is nothing to keep.
  void add_zone_permanent(uint8_t node, switch_::Switch *s) {
    Zone *z = find_or_add_zone_(node);
    if (z == nullptr) return;
    z->permanent_switch = s;
  }

  void write_zone_permanent(uint8_t node, bool on) {
    Zone *z = find_zone_(node);
    if (z == nullptr) return;
    bool override_active = z->overridden || z->override_ms != 0;
    if (on) {
      if (override_active) {
        set_zone_permanent_(*z, true);
        z->override_ms = 0;
        ESP_LOGI(TAG, "Zone %u: override kept until changed", node);
      } else if (!z->controller_permanent) {
        ESP_LOGW(TAG, "Zone %u: nothing to keep; choose Min, Med or Max for the zone first", node);
      }
    } else {
      set_zone_permanent_(*z, false);
      if (override_active && z->override_ms == 0) {
        start_zone_override_timer_(*z);
        ESP_LOGI(TAG, "Zone %u: override ends after the manual timeout", node);
      }
      if (z->controller_permanent) {
        ESP_LOGW(TAG, "Zone %u: permanent mode was set on a wall controller; end it there", node);
      }
    }
    publish_zone_permanent_(*z);
  }

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

    // Zone permanent flags survive an ESP restart, so a permanent override found
    // in the first Network table isn't ended after the manual timeout.
    for (uint8_t i = 0; i < zone_count_; i++) {
      Zone &z = zones_[i];
      if (z.permanent_switch == nullptr) continue;
      z.permanent_pref = global_preferences->make_preference<bool>(
          fnv1_hash_extend(fnv1_hash("duco_zone_permanent"), z.node));
      bool on = false;
      if (z.permanent_pref.load(&on)) z.permanent = on;
      publish_zone_permanent_(z);
    }

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
      // After a write the box sometimes ignores the next command; give it a moment.
      if (settling_) {
        if ((millis() - settle_start_ms_) < SET_SETTLE_MS) return;
        settling_ = false;
      }

      if (custom_command_requested_) {
        custom_command_requested_ = false;
        char cmd[136];
        snprintf(cmd, sizeof(cmd), "%s\r\n", custom_command_);
        send_command(cmd, PH_CUSTOM);
        return;
      }

#ifdef USE_TIME
      check_clock_sync_();
#endif
      check_zone_overrides_();
      if (set_count_ > 0) {
        send_next_set_();
        return;
      }
      if (settings_pending_ != 0) {  // read-back after a write
        send_next_setting_get();
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
  static constexpr uint32_t    SET_SETTLE_MS     = 1500;   // Pause after a *Set before the next command (box sometimes ignores one sent right away)
  static constexpr const char *CLOCK_SYNC_CMD    = "@clock";  // Queue marker; RtcSetTime is formatted when sent

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

  // Settings
  SettingNumber   settings_[MAX_SETTING_NUMBERS];
  uint8_t         setting_count_       {0};
  uint8_t         setting_nodes_[MAX_SETTING_NODES] {0};
  uint8_t         setting_node_count_  {0};
  select::Select *bypass_select_       {nullptr};
  uint32_t        settings_poll_interval_ms_{3600000};
  uint32_t        last_settings_poll_ms_ {0};
  bool            settings_polled_     {false};
  uint8_t         settings_pending_    {0};           // Read-back groups still to send
  uint8_t         setting_group_       {GROUP_NONE};  // Group of the Get in flight
  QueuedSet       set_queue_[SET_QUEUE_SIZE];
  uint8_t         set_head_            {0};
  uint8_t         set_count_           {0};
  uint8_t         set_group_           {GROUP_NONE};  // Read-back group of the Set in flight
  char            set_cmd_[48]         {0};           // The Set in flight
  bool            settling_            {false};
  uint32_t        settle_start_ms_     {0};
#ifdef USE_TIME
  time::RealTimeClock *time_           {nullptr};
  bool            clock_synced_        {false};
  uint16_t        clock_sync_yday_     {0};
  uint32_t        last_clock_check_ms_ {0};
#endif

  // Network table column indices (resolved from header row)
  uint8_t col_stat_   {255};
  uint8_t col_dbt_    {255};
  uint8_t col_cval_   {255};
  uint8_t col_cntdwn_ {255};
  uint8_t col_ovrl_   {255};

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

    // Info and settings replies often have no "Done": complete once more than the
    // echo has arrived and the box has been quiet for a moment.
    if (!got_done_ && (phase_ == PH_INFO || phase_ == PH_SETTING) && reply_lines_ >= 2 &&
        (millis() - last_rx_ms_) >= INFO_QUIET_MS)
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
      if (phase_ == PH_SET) {
        ESP_LOGW(TAG, "Box rejected the setting (value is read back to show the current one)");
      } else {
        ESP_LOGW(TAG, "Command failed in phase %d", (int) phase_);
      }
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
        // Node 1 (the box) updates the box-level sensors, zone nodes their zone entities
        if (node == 1) {
          NetworkRow row = parse_network_row(line);
          if (row.mode >= 0) {
            if (vent_mode_)     vent_mode_->publish_state((float) row.mode);
            if (vent_mode_txt_) vent_mode_txt_->publish_state(vent_mode_to_str(row.mode));
          }
          if (row.pct >= 0 && vent_pct_)        vent_pct_->publish_state((float) row.pct);
          if (row.countdown >= 0 && countdown_) countdown_->publish_state((float) row.countdown);
        } else if (Zone *z = find_zone_(node)) {
          update_zone_(*z, parse_network_row(line));
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

    if (phase_ == PH_SETTING) {
      parse_setting_line_(line);
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
  // Finds column positions of stat / %dbt / cval / cntdwn / ovrl in the pipe-delimited header
  void parse_network_header(const char *line) {
    col_stat_ = col_dbt_ = col_cval_ = col_cntdwn_ = col_ovrl_ = 255;

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
      if (strstr(tok, "ovrl"))   col_ovrl_   = col;
      col++;
      tok = strtok(nullptr, "|");
    }
    ESP_LOGD(TAG, "Network header: stat=%d dbt=%d cval=%d cntdwn=%d ovrl=%d",
             col_stat_, col_dbt_, col_cval_, col_cntdwn_, col_ovrl_);

    if (log_all_nodes_active_) {
      ESP_LOGI(TAG, "==== Duco Node List (Network) ====");
    }
  }

  // ── Network data row parser ───────────────────────────────────────────────
  NetworkRow parse_network_row(const char *line) {
    NetworkRow row;
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
        row.mode = vent_mode_from_str(tok);
        ESP_LOGD(TAG, "Network stat=\"%s\" -> %d", tok, row.mode);
      }
      if (col == col_dbt_ && col_dbt_ != 255) {
        int v = 0;
        if (sscanf(tok, "%d", &v) == 1) {
          ESP_LOGD(TAG, "Network %%dbt=%d", v);
          row.pct = v;
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
          row.countdown = v;
        }
      }
      if (col == col_ovrl_ && col_ovrl_ != 255) {
        int v = 0;
        if (sscanf(tok, "%d", &v) == 1) row.ovrl = v;
      }
      col++;
      tok = strtok(nullptr, "|");
    }
    return row;
  }

  // ── Zones ─────────────────────────────────────────────────────────────────
  Zone *find_zone_(uint8_t node) {
    for (uint8_t i = 0; i < zone_count_; i++)
      if (zones_[i].node == node) return &zones_[i];
    return nullptr;
  }

  Zone *find_or_add_zone_(uint8_t node) {
    if (Zone *z = find_zone_(node)) return z;
    if (zone_count_ >= MAX_ZONES) {
      ESP_LOGE(TAG, "Too many zones (max %u); ignoring node %u", MAX_ZONES, node);
      return nullptr;
    }
    Zone &z = zones_[zone_count_++];
    z.node = node;
    return &z;
  }

  void start_zone_override_timer_(Zone &z) {
    z.override_start_ms = millis();
    z.override_ms = (uint32_t) (z.manual_timeout_min != 0 ? z.manual_timeout_min : 15) * 60000UL;
  }

  uint32_t zone_override_remaining_s_(const Zone &z) const {
    uint32_t elapsed = millis() - z.override_start_ms;
    return elapsed >= z.override_ms ? 0 : (z.override_ms - elapsed + 999) / 1000;
  }

  void queue_zone_override_(uint8_t node, uint8_t value) {
    char cmd[40];
    snprintf(cmd, sizeof(cmd), "NodeSetOverrule %u %u", node, value);
    if (!queue_set_(cmd, GROUP_POLL)) return;
    if (Zone *z = find_zone_(node)) z->write_pending = true;
  }

  void set_zone_permanent_(Zone &z, bool on) {
    if (z.permanent == on) return;
    z.permanent = on;
    z.permanent_pref.save(&z.permanent);
  }

  // The switch is on while the zone's setting stays until changed: an override kept
  // from here, or a wall controller's permanent mode (CNT1-3). Always published, also
  // when off (the switch drops repeats itself), so it has a state from the start.
  void publish_zone_permanent_(Zone &z) {
    if (z.permanent_switch == nullptr) return;
    z.permanent_switch->publish_state(z.permanent || z.controller_permanent);
  }

  // End overrides set from here once their manual timeout has passed (like a wall controller)
  void check_zone_overrides_() {
    for (uint8_t i = 0; i < zone_count_; i++) {
      Zone &z = zones_[i];
      if (z.override_ms == 0 || (millis() - z.override_start_ms) < z.override_ms) continue;
      z.override_ms = 0;
      ESP_LOGI(TAG, "Zone %u: override timed out, back to auto", z.node);
      queue_zone_override_(z.node, ZONE_OVERRIDE_NONE);
    }
  }

  void update_zone_(Zone &z, const NetworkRow &row) {
    bool overridden = row.ovrl >= 0 && row.ovrl != ZONE_OVERRIDE_NONE;

    // Rows read while a write for this zone is still queued show the old state; skip them here
    if (z.control != nullptr && !z.write_pending) {
      if (overridden && z.override_ms == 0 && !z.permanent) {
        // An override this gateway doesn't know about (e.g. set before a reboot):
        // give it the zone's manual timeout from now, so it can't stay on forever.
        ESP_LOGI(TAG, "Zone %u: found override of %d%%; ending it after the manual timeout", z.node, row.ovrl);
        start_zone_override_timer_(z);
      } else if (!overridden && row.ovrl >= 0) {
        z.override_ms = 0;  // ended elsewhere
        if (z.permanent) {
          ESP_LOGI(TAG, "Zone %u: override ended elsewhere, no longer permanent", z.node);
          set_zone_permanent_(z, false);
        }
      }
    }
    if (!z.write_pending) {
      if (row.ovrl >= 0) z.overridden = overridden;
      if (row.mode >= 0) z.controller_permanent = row.mode >= 11 && row.mode <= 13;  // CNT1-3
      publish_zone_permanent_(z);
    }

    if (z.mode != nullptr) {
      if (overridden) {
        z.mode->publish_state("EXTN");  // the box's own term; its stat column stays AUTO
      } else if (row.mode >= 0) {
        z.mode->publish_state(vent_mode_to_str(row.mode));
      }
    }
    if (row.pct >= 0 && z.percentage != nullptr) z.percentage->publish_state((float) row.pct);
    if (z.countdown != nullptr && (row.countdown >= 0 || z.override_ms != 0)) {
      uint32_t cd = row.countdown > 0 ? (uint32_t) row.countdown : 0;
      if (z.override_ms != 0) cd = std::max(cd, zone_override_remaining_s_(z));
      z.countdown->publish_state((float) cd);
    }

    if (z.control != nullptr && !z.write_pending) {
      size_t idx = 0;  // Auto
      if (overridden) {
        // Nearest of the zone's speed levels
        int best = 1 << 30;
        for (size_t i = 0; i < 3; i++) {
          int level = z.levels[i] != 0 ? z.levels[i] : ZONE_DEFAULT_LEVELS[i];
          if (abs(level - row.ovrl) < best) {
            best = abs(level - row.ovrl);
            idx = i + 1;
          }
        }
      } else if (row.mode == 1 || row.mode == 11) {
        idx = 1;  // MAN1 / CNT1 from a wall controller
      } else if (row.mode == 2 || row.mode == 12) {
        idx = 2;
      } else if (row.mode == 3 || row.mode == 13) {
        idx = 3;
      }
      auto current = z.control->active_index();
      if (!current.has_value() || current.value() != idx) z.control->publish_state(idx);
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

      case PH_SET:
        settling_ = true;
        settle_start_ms_ = millis();
        if (set_group_ == GROUP_POLL) {
          last_poll_ms_ = millis() - poll_interval_ms_;  // read the Network table right after settling
          unsigned node = 0;
          if (sscanf(set_cmd_, "NodeSetOverrule %u", &node) == 1) {
            if (Zone *z = find_zone_(node)) z->write_pending = false;
          }
        } else if (set_group_ != GROUP_NONE) {
          settings_pending_ |= (1u << set_group_);
        }
        phase_ = PH_IDLE;
        break;

      case PH_SETTING:
        send_next_setting_get();
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
    go_to_settings();
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
    go_to_settings();
  }

  // ── Settings: read-back ───────────────────────────────────────────────────
  uint8_t node_group_(uint8_t node) const {
    for (uint8_t i = 0; i < setting_node_count_; i++)
      if (setting_nodes_[i] == node) return GROUP_NODE_FIRST + i;
    return GROUP_NONE;
  }

  uint8_t all_setting_groups_() const {
    uint8_t mask = 0;
    if (bypass_select_) mask |= 1u << GROUP_BYPASS;
    for (uint8_t i = 0; i < setting_count_; i++)
      if (settings_[i].source == SETTING_GENERAL) mask |= 1u << GROUP_GENERAL;
    for (uint8_t i = 0; i < setting_node_count_; i++) mask |= 1u << (GROUP_NODE_FIRST + i);
    return mask;
  }

  // End of the poll cycle: read all settings when due (and on the first cycle after boot)
  void go_to_settings() {
    uint8_t all = all_setting_groups_();
    if (all != 0 && (!settings_polled_ || (last_poll_ms_ - last_settings_poll_ms_) >= settings_poll_interval_ms_)) {
      settings_polled_ = true;
      last_settings_poll_ms_ = last_poll_ms_;
      settings_pending_ |= all;
    }
    send_next_setting_get();
  }

  // Send the Get for the lowest pending read-back group; end the cycle when none is left.
  void send_next_setting_get() {
    for (uint8_t g = 0; g < 8; g++) {
      if (!(settings_pending_ & (1u << g))) continue;
      settings_pending_ &= ~(1u << g);
      setting_group_ = g;
      char cmd[32];
      if (g == GROUP_BYPASS) {
        snprintf(cmd, sizeof(cmd), "BypassParaGet\r\n");
      } else if (g == GROUP_GENERAL) {
        snprintf(cmd, sizeof(cmd), "SettingsParaGet\r\n");
      } else {
        snprintf(cmd, sizeof(cmd), "NodeConfigGet %u\r\n", setting_nodes_[g - GROUP_NODE_FIRST]);
      }
      send_command(cmd, PH_SETTING);
      return;
    }
    ESP_LOGD(TAG, "Poll cycle complete");
    phase_ = PH_IDLE;
  }

  // "4. CO2 Setpoint   :  800 ppm  [0:10:2000]" (NodeConfigGet)
  // "5. COMFORT TEMP Z1    : 205"               (SettingsParaGet)
  // "0. BYPASS MODE            : AUTO (0) [0-2]" (BypassParaGet)
  void parse_setting_line_(const char *line) {
    const char *s = skip_spaces_(line);
    const char *colon = strchr(s, ':');
    int param = 0, value = 0;
    if (colon == nullptr || sscanf(s, "%d.", &param) != 1) return;

    if (setting_group_ == GROUP_BYPASS) {
      const char *paren = strchr(colon, '(');
      if (param == 0 && paren != nullptr && sscanf(paren + 1, "%d", &value) == 1 && bypass_select_ != nullptr &&
          value >= 0 && (size_t) value < bypass_select_->size()) {
        ESP_LOGD(TAG, "Bypass mode=%d", value);
        bypass_select_->publish_state((size_t) value);
      }
      return;
    }

    if (sscanf(colon + 1, "%d", &value) != 1) return;
    uint8_t source = setting_group_ == GROUP_GENERAL ? SETTING_GENERAL : SETTING_NODE_CONFIG;
    uint8_t node = source == SETTING_NODE_CONFIG ? setting_nodes_[setting_group_ - GROUP_NODE_FIRST] : 0;
    for (uint8_t i = 0; i < setting_count_; i++) {
      const SettingNumber &n = settings_[i];
      if (n.source != source || n.node != node || n.param != param) continue;
      ESP_LOGD(TAG, "Setting node %u param %u = %d", node, param, value);
      n.number->publish_state(value * n.scale);
    }

    // Speed levels and manual timeout of a zone with a zone_ventilation select
    Zone *z = source == SETTING_NODE_CONFIG ? find_zone_(node) : nullptr;
    if (z != nullptr) {
      if (param >= 7 && param <= 9 && value > 0 && value <= 100) z->levels[param - 7] = (uint8_t) value;
      if (param == 10 && value > 0) z->manual_timeout_min = (uint16_t) value;
    }
  }

  // ── Settings: writes ──────────────────────────────────────────────────────
  // Queue a *Set command. A pending write for the same setting (same text up to
  // the last space, e.g. "NodeConfigSet 3 4 ") is replaced instead of queued again.
  // Returns false if the queue was full and the write was dropped.
  bool queue_set_(const char *cmd, uint8_t group) {
    const char *last_space = strrchr(cmd, ' ');
    size_t key_len = last_space != nullptr ? (size_t) (last_space - cmd + 1) : strlen(cmd);
    for (uint8_t i = 0; i < set_count_; i++) {
      QueuedSet &q = set_queue_[(set_head_ + i) % SET_QUEUE_SIZE];
      if (strncmp(q.cmd, cmd, key_len) == 0) {
        snprintf(q.cmd, sizeof(q.cmd), "%s", cmd);
        return true;
      }
    }
    if (set_count_ >= SET_QUEUE_SIZE) {
      ESP_LOGW(TAG, "Settings queue full; dropping \"%s\"", cmd);
      return false;
    }
    QueuedSet &q = set_queue_[(set_head_ + set_count_) % SET_QUEUE_SIZE];
    snprintf(q.cmd, sizeof(q.cmd), "%s", cmd);
    q.group = group;
    set_count_++;
    return true;
  }

  void send_next_set_() {
    QueuedSet &q = set_queue_[set_head_];
    char cmd[48];
    snprintf(cmd, sizeof(cmd), "%s", q.cmd);
#ifdef USE_TIME
    if (strcmp(q.cmd, CLOCK_SYNC_CMD) == 0) {
      // Formatted now, so time spent waiting in the queue doesn't make the box clock late
      ESPTime now = time_->now();
      snprintf(cmd, sizeof(cmd), "RtcSetTime %u %u %u %u %u %u", now.day_of_month, now.month, now.year, now.hour,
               now.minute, now.second);
    }
#endif
    set_group_ = q.group;
    snprintf(set_cmd_, sizeof(set_cmd_), "%s", cmd);
    set_head_ = (set_head_ + 1) % SET_QUEUE_SIZE;
    set_count_--;

    ESP_LOGI(TAG, "Writing setting: %s", cmd);
    char line[52];
    snprintf(line, sizeof(line), "%s\r\n", cmd);
    send_command(line, PH_SET);
  }

#ifdef USE_TIME
  // Set the box clock once the time source is valid after boot, then daily from
  // 04:00 local time (so a DST change is followed within a day).
  void check_clock_sync_() {
    if (time_ == nullptr || (millis() - last_clock_check_ms_) < 1000) return;
    last_clock_check_ms_ = millis();
    ESPTime now = time_->now();
    if (!now.is_valid()) return;
    if (clock_synced_ && (now.day_of_year == clock_sync_yday_ || now.hour < 4)) return;
    clock_synced_ = true;
    clock_sync_yday_ = now.day_of_year;
    queue_set_(CLOCK_SYNC_CMD, GROUP_NONE);
  }
#endif
};

// ─────────────────────────────────────────────────────────────────────────────
// Number / select entities (platforms number.py / select.py). Changes are queued
// on the gateway; the state is only published from the value read back.
// ─────────────────────────────────────────────────────────────────────────────
class DucoNumber : public number::Number, public Parented<DucoGateway> {
 protected:
  void control(float value) override { this->parent_->write_setting_number(this, value); }
};

class DucoSelect : public select::Select, public Parented<DucoGateway> {
 public:
  void set_zone(uint8_t node) { zone_node_ = node; }

 protected:
  void control(size_t index) override {
    if (zone_node_ == 0) {
      this->parent_->write_bypass_mode(index);
    } else {
      this->parent_->write_zone_control(zone_node_, index);
    }
  }

  uint8_t zone_node_{0};  // 0 = bypass_mode, otherwise zone_ventilation for this zone valve node
};

// Switch entity (platform switch.py): zone_permanent for one zone valve. Its state is
// only published by the gateway, from what the box reports.
class DucoSwitch : public switch_::Switch, public Parented<DucoGateway> {
 public:
  void set_zone(uint8_t node) { zone_node_ = node; }

 protected:
  void write_state(bool state) override { this->parent_->write_zone_permanent(zone_node_, state); }

  uint8_t zone_node_{0};
};

}  // namespace duco_gateway
}  // namespace esphome