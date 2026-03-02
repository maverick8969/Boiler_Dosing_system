# MQTT Telemetry Schema (Modern IoT Stack)

This document describes the MQTT topic layout and JSON payload formats used by the ESP32 boiler controller when **MQTT telemetry** is enabled (`use_mqtt_telemetry` in config). A gateway (e.g. Node.js subscriber) can consume these messages and write them into TimescaleDB or another backend.

## Topic layout (Sparkplug-style)

All topics are under the namespace **`device/{device_id}/`** where `device_id` is a 12-character hex string derived from the device WiFi MAC (e.g. `A1B2C3D4E5F6`).

| Topic | Description | Publish rate / trigger |
|-------|-------------|------------------------|
| `device/{device_id}/metrics` | Time-series sensor and state metrics | Every `log_interval_ms` (e.g. 10 s) |
| `device/{device_id}/alarm`  | Alarm state changes and system events | On alarm/event change |
| `device/{device_id}/health` | Heartbeat and device health             | Every ~60 s; LWT payload `"offline"` on disconnect |
| `device/{device_id}/command_result` | Result of a command executed via API | After each command completion |

**Last Will and Testament (LWT):** On connect, the device sets a will on `device/{device_id}/health` with payload `"offline"`, QoS 1, retained. The broker publishes this if the device disconnects ungracefully.

---

## Payload: `device/{device_id}/metrics`

JSON object with timestamp, device_id, sequence, and an array of metrics (id, value, unit, quality).

```json
{
  "timestamp": 1709308800,
  "device_id": "A1B2C3D4E5F6",
  "sequence": 42,
  "metrics": [
    { "id": "conductivity", "value": 2500.0, "unit": "uS/cm", "quality": "good" },
    { "id": "temperature", "value": 25.5, "unit": "C", "quality": "good" },
    { "id": "flow_rate", "value": 12.0, "unit": "GPM", "quality": "good" },
    { "id": "water_meter1", "value": 1000.0, "unit": "gal", "quality": "good" },
    { "id": "water_meter2", "value": 0.0, "unit": "gal", "quality": "good" },
    { "id": "valve_position_mA", "value": 4.0, "unit": "mA", "quality": "good" },
    { "id": "blowdown_active", "value": 0.0, "unit": "", "quality": "good" },
    { "id": "pump1_active", "value": 0.0, "unit": "", "quality": "good" },
    { "id": "pump2_active", "value": 0.0, "unit": "", "quality": "good" },
    { "id": "pump3_active", "value": 0.0, "unit": "", "quality": "good" },
    { "id": "active_alarms", "value": 0.0, "unit": "", "quality": "good" },
    { "id": "safe_mode", "value": 0.0, "unit": "", "quality": "good" },
    { "id": "devices_operational", "value": 8.0, "unit": "", "quality": "good" },
    { "id": "devices_faulted", "value": 0.0, "unit": "", "quality": "good" }
  ]
}
```

- **timestamp**: Unix time (seconds).
- **device_id**: Same as in topic (12-char hex).
- **sequence**: Monotonic counter per device (wraps; used for ordering).
- **metrics[].id**: Metric name (string).
- **metrics[].value**: Numeric value (float).
- **metrics[].unit**: Unit string (e.g. `"uS/cm"`, `"C"`, `"GPM"`); may be empty.
- **metrics[].quality**: `"good"` or `"bad"` (sensor validity).

**Gateway mapping to `sensor_readings`:** Extract metrics by `id` (conductivity, temperature, flow_rate, water_meter1, water_meter2, blowdown_active, pump1_active, pump2_active, pump3_active, active_alarms), use `timestamp` for `time`, and store `device_id` in the table.

---

## Payload: `device/{device_id}/alarm`

Two sub-types: **alarm** (conductivity, sensor error, etc.) and **event** (e.g. feedwater pump on/off).

### Alarm message

```json
{
  "timestamp": 1709308800,
  "device_id": "A1B2C3D4E5F6",
  "sequence": 43,
  "type": "alarm",
  "alarm_code": 1,
  "alarm_name": "HIGH CONDUCTIVITY",
  "active": true,
  "trigger_value": 3200.5
}
```

- **type**: `"alarm"`.
- **alarm_code**: Unsigned integer (bitmask value, e.g. 1 = COND_HIGH).
- **alarm_name**: Human-readable name.
- **active**: `true` = alarm set, `false` = alarm cleared.
- **trigger_value**: Float (e.g. conductivity at trigger).

**Gateway mapping to `alarms`:** Insert row with `time` from timestamp, `device_id`, `alarm_code`, `alarm_name`, `state` = `"active"` or `"cleared"`, `value` = trigger_value.

### Event message

```json
{
  "timestamp": 1709308800,
  "device_id": "A1B2C3D4E5F6",
  "sequence": 44,
  "type": "event",
  "event_type": "FW_PUMP_ON",
  "description": "Feedwater pump started",
  "value": 123
}
```

- **type**: `"event"`.
- **event_type**: e.g. `"FW_PUMP_ON"`, `"FW_PUMP_OFF"`.
- **description**: Short description.
- **value**: Optional integer (e.g. cycle count).

**Gateway:** Can map to `pump_events` or a generic `system_events` table with `device_id`.

---

## Payload: `device/{device_id}/health`

```json
{
  "timestamp": 1709308800,
  "device_id": "A1B2C3D4E5F6",
  "sequence": 45,
  "uptime_sec": 3600,
  "free_heap": 120000,
  "wifi_connected": true,
  "mqtt_connected": true,
  "active_alarms": 0,
  "firmware": "1.0.0"
}
```

**Gateway mapping to `system_status`:** Insert with `time`, `device_id`, `uptime_sec`, `free_heap`, etc. Optionally use for device online/offline state (with LWT).

---

## Payload: `device/{device_id}/command_result`

```json
{
  "timestamp": 1709308800,
  "device_id": "A1B2C3D4E5F6",
  "sequence": 46,
  "request_id": "uuid-from-client",
  "result": "completed",
  "message": "Blowdown started"
}
```

- **request_id**: Echo of the client’s request ID from POST /api/command.
- **result**: `"completed"` or `"failed"`.
- **message**: Optional human-readable message.

**Gateway:** Optional; useful for auditing or UI feedback. Can be stored in a `command_results` table or logged.

---

## Subscription pattern for gateway

Subscribe to all devices with MQTT wildcards:

- `device/+/metrics`
- `device/+/alarm`
- `device/+/health`
- `device/+/command_result` (optional)

Extract `device_id` from the topic (e.g. split `device/A1B2C3D4E5F6/metrics` → `A1B2C3D4E5F6`).

---

## Database: `device_id` column

For multi-device support, add a `device_id` column (e.g. `VARCHAR(16)`) to:

- `sensor_readings`
- `alarms`
- `system_status`
- Optionally `pump_events` / event tables

Use the migration script **`backend/db/migrations/001_add_device_id.sql`** (adds `device_id` to `sensor_readings`, `alarms`, `system_status`, `pump_events`). New installs using the updated `backend/db/init.sql` already include `device_id` in these tables.
