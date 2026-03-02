# MQTT Telemetry Schema (Modern IoT Stack)

Firmware publishes Sparkplug-style JSON to MQTT. The gateway subscribes to `device/+/metrics`, `device/+/alarm`, `device/+/health`, and optionally `device/+/command_result`, and writes to TimescaleDB.

## Topic layout

| Topic pattern | Description |
|---------------|-------------|
| `device/{device_id}/metrics` | Time-series sensor readings (conductivity, temperature, flow, etc.) |
| `device/{device_id}/alarm` | Alarm state changes (active/cleared) |
| `device/{device_id}/health` | Health/heartbeat (uptime, heap, connectivity) |
| `device/{device_id}/command_result` | Command execution result (optional; for audit) |
| `device/{device_id}/state` | Event snapshots (e.g. feedwater pump on/off) |

`device_id` is a stable 12-character hex string (MAC-derived) per device.

## Payload requirements (Sparkplug-compatible)

All payloads include:

- `timestamp` — Unix seconds (uint32)
- `device_id` — Same as topic (string)
- `sequence` — Monotonic counter (uint32)

### metrics

```json
{
  "timestamp": 1709308800,
  "device_id": "A1B2C3D4E5F6",
  "sequence": 42,
  "metrics": [
    { "id": "conductivity", "value": 2500.0, "unit": "uS_cm", "quality": "good" },
    { "id": "temperature", "value": 25.5, "unit": "C", "quality": "good" },
    { "id": "water_meter1", "value": 1000, "unit": "gal", "quality": "good" },
    { "id": "water_meter2", "value": 200, "unit": "gal", "quality": "good" },
    { "id": "flow_rate", "value": 2.5, "unit": "GPM", "quality": "good" },
    { "id": "blowdown_active", "value": false, "unit": "", "quality": "good" },
    { "id": "active_alarms", "value": 0, "unit": "", "quality": "good" }
  ]
}
```

Gateway maps metrics into `sensor_readings` (time, device_id, conductivity, temperature, water_meter1, water_meter2, flow_rate, blowdown_active, pump1_active, pump2_active, pump3_active, active_alarms). Metric `id` is used to pick the value; booleans can be 0/1 or true/false.

### alarm

```json
{
  "type": "alarm",
  "timestamp": 1709308800,
  "device_id": "A1B2C3D4E5F6",
  "sequence": 43,
  "alarm_code": 1,
  "alarm_name": "HIGH CONDUCTIVITY",
  "active": true,
  "trigger_value": 3200.0
}
```

Gateway inserts into `alarms` (time, device_id, alarm_code, alarm_name, state = 'active'|'cleared', value = trigger_value).

### health

```json
{
  "timestamp": 1709308800,
  "device_id": "A1B2C3D4E5F6",
  "sequence": 44,
  "uptime_sec": 3600,
  "free_heap": 45000,
  "wifi_connected": true,
  "mqtt_connected": true,
  "active_alarms": 0
}
```

Gateway inserts into `system_status` (time, device_id, uptime_sec, free_heap, error_count). `error_count` can be set from `active_alarms` or left 0.

### command_result (optional)

```json
{
  "request_id": "uuid-from-tablet",
  "result": "completed",
  "message": "Blowdown started",
  "timestamp": 1709308800,
  "device_id": "A1B2C3D4E5F6",
  "sequence": 45
}
```

Can be logged to a dedicated table or ignored by the gateway.

## Database columns (device_id)

- `sensor_readings.device_id` — VARCHAR(16), NULL for HTTP-only legacy
- `alarms.device_id` — VARCHAR(16)
- `pump_events.device_id` — VARCHAR(16)
- `system_status.device_id` — VARCHAR(16)

Indexes and retention policies are unchanged; filter by `device_id` for multi-device dashboards.

## Gateway configuration

- `MQTT_URL` — e.g. `mqtt://localhost:1883` or `mqtt://broker:1883`
- `MQTT_ENABLED` — set to `false` to disable MQTT gateway (HTTP-only)

The Node API server starts the MQTT client on listen and subscribes to `device/+/metrics`, `device/+/alarm`, `device/+/health`.
