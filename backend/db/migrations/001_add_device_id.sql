-- Phase E: Add device_id for MQTT telemetry (multi-device support)
-- Run this migration on existing databases before using the MQTT gateway.
-- New installs: run after init.sql or add these columns to init.sql.

-- sensor_readings: add device_id (nullable for backward compatibility with HTTP-only devices)
ALTER TABLE sensor_readings
  ADD COLUMN IF NOT EXISTS device_id VARCHAR(16);
CREATE INDEX IF NOT EXISTS idx_sensor_readings_device_time ON sensor_readings (device_id, time DESC);

-- alarms: add device_id
ALTER TABLE alarms
  ADD COLUMN IF NOT EXISTS device_id VARCHAR(16);
CREATE INDEX IF NOT EXISTS idx_alarms_device_time ON alarms (device_id, time DESC);

-- system_status: add device_id
ALTER TABLE system_status
  ADD COLUMN IF NOT EXISTS device_id VARCHAR(16);
CREATE INDEX IF NOT EXISTS idx_system_status_device_time ON system_status (device_id, time DESC);

-- pump_events: add device_id (for MQTT-sourced events e.g. FW_PUMP_ON/OFF)
ALTER TABLE pump_events
  ADD COLUMN IF NOT EXISTS device_id VARCHAR(16);
CREATE INDEX IF NOT EXISTS idx_pump_events_device_time ON pump_events (device_id, time DESC);

COMMENT ON COLUMN sensor_readings.device_id IS 'Device identifier (12-char hex from MAC) when telemetry is via MQTT';
