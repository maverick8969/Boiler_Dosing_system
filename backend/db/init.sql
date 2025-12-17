-- TimescaleDB Schema for Boiler Dosing Controller
-- Columbia CT-6 Boiler System

-- Enable TimescaleDB extension
CREATE EXTENSION IF NOT EXISTS timescaledb;

-- ============================================
-- SENSOR READINGS - Primary time-series table
-- ============================================
CREATE TABLE sensor_readings (
    time                TIMESTAMPTZ     NOT NULL,
    conductivity        REAL,           -- µS/cm
    temperature         REAL,           -- °C
    water_meter1        BIGINT,         -- Total gallons (makeup)
    water_meter2        BIGINT,         -- Total gallons (blowdown)
    flow_rate           REAL,           -- GPM
    blowdown_active     BOOLEAN         DEFAULT FALSE,
    pump1_active        BOOLEAN         DEFAULT FALSE,  -- H2SO3
    pump2_active        BOOLEAN         DEFAULT FALSE,  -- NaOH
    pump3_active        BOOLEAN         DEFAULT FALSE,  -- Amine
    active_alarms       INTEGER         DEFAULT 0
);

-- Convert to hypertable (TimescaleDB time-series optimization)
SELECT create_hypertable('sensor_readings', 'time');

-- Create index for faster queries
CREATE INDEX idx_readings_time ON sensor_readings (time DESC);

-- ============================================
-- PUMP EVENTS - Chemical dosing log
-- ============================================
CREATE TABLE pump_events (
    time                TIMESTAMPTZ     NOT NULL,
    pump_id             INTEGER         NOT NULL,   -- 1=H2SO3, 2=NaOH, 3=Amine
    pump_name           VARCHAR(32),
    event_type          VARCHAR(16)     NOT NULL,   -- 'start', 'stop', 'prime', 'calibrate'
    duration_ms         INTEGER,
    volume_ml           REAL,
    trigger_source      VARCHAR(32),    -- 'conductivity', 'timer', 'manual', 'water_meter'
    feed_mode           VARCHAR(16)     -- 'A', 'B', 'C', 'D', 'E', 'S'
);

SELECT create_hypertable('pump_events', 'time');
CREATE INDEX idx_pump_events_time ON pump_events (time DESC);
CREATE INDEX idx_pump_events_pump ON pump_events (pump_id, time DESC);

-- ============================================
-- BLOWDOWN EVENTS - Blowdown cycle log
-- ============================================
CREATE TABLE blowdown_events (
    time                TIMESTAMPTZ     NOT NULL,
    event_type          VARCHAR(16)     NOT NULL,   -- 'start', 'stop', 'timeout', 'lockout'
    duration_sec        INTEGER,
    start_conductivity  REAL,
    end_conductivity    REAL,
    water_volume_gal    REAL,
    trigger_source      VARCHAR(32),    -- 'high_cond', 'timer', 'manual', 'sample_mode'
    sampling_mode       VARCHAR(16)     -- 'continuous', 'intermittent', 'timed', 'time_prop'
);

SELECT create_hypertable('blowdown_events', 'time');
CREATE INDEX idx_blowdown_time ON blowdown_events (time DESC);

-- ============================================
-- ALARMS - Alarm history
-- ============================================
CREATE TABLE alarms (
    time                TIMESTAMPTZ     NOT NULL,
    alarm_code          INTEGER         NOT NULL,
    alarm_name          VARCHAR(64),
    severity            VARCHAR(16),    -- 'warning', 'alarm', 'critical'
    state               VARCHAR(16)     NOT NULL,   -- 'active', 'acknowledged', 'cleared'
    value               REAL,           -- Sensor value at alarm time
    setpoint            REAL,           -- Threshold that was exceeded
    message             TEXT
);

SELECT create_hypertable('alarms', 'time');
CREATE INDEX idx_alarms_time ON alarms (time DESC);
CREATE INDEX idx_alarms_active ON alarms (state, time DESC) WHERE state = 'active';

-- ============================================
-- CALIBRATION LOG - Sensor calibration history
-- ============================================
CREATE TABLE calibration_log (
    time                TIMESTAMPTZ     NOT NULL,
    sensor_type         VARCHAR(32)     NOT NULL,   -- 'conductivity', 'temperature', 'flow'
    cal_point           INTEGER,        -- 1 or 2 for two-point cal
    reference_value     REAL,           -- Known standard value
    measured_raw        REAL,           -- Raw ADC/sensor reading
    previous_factor     REAL,
    new_factor          REAL,
    technician          VARCHAR(64)
);

SELECT create_hypertable('calibration_log', 'time');

-- ============================================
-- CONFIGURATION CHANGES - Audit trail
-- ============================================
CREATE TABLE config_changes (
    time                TIMESTAMPTZ     NOT NULL,
    parameter           VARCHAR(64)     NOT NULL,
    old_value           TEXT,
    new_value           TEXT,
    source              VARCHAR(32)     -- 'local', 'remote', 'factory_reset'
);

SELECT create_hypertable('config_changes', 'time');
CREATE INDEX idx_config_time ON config_changes (time DESC);

-- ============================================
-- SYSTEM STATUS - Periodic system health
-- ============================================
CREATE TABLE system_status (
    time                TIMESTAMPTZ     NOT NULL,
    uptime_sec          BIGINT,
    free_heap           INTEGER,
    wifi_rssi           INTEGER,
    cpu_temp            REAL,
    nvs_free_entries    INTEGER,
    error_count         INTEGER
);

SELECT create_hypertable('system_status', 'time');

-- ============================================
-- DATA RETENTION POLICIES
-- ============================================
-- Keep detailed readings for 90 days
SELECT add_retention_policy('sensor_readings', INTERVAL '90 days');

-- Keep pump/blowdown events for 1 year
SELECT add_retention_policy('pump_events', INTERVAL '1 year');
SELECT add_retention_policy('blowdown_events', INTERVAL '1 year');

-- Keep alarms for 2 years
SELECT add_retention_policy('alarms', INTERVAL '2 years');

-- Keep calibration/config indefinitely (no retention policy)

-- ============================================
-- CONTINUOUS AGGREGATES (Pre-computed rollups)
-- ============================================

-- Hourly averages for dashboard
CREATE MATERIALIZED VIEW sensor_readings_hourly
WITH (timescaledb.continuous) AS
SELECT
    time_bucket('1 hour', time) AS bucket,
    AVG(conductivity) AS avg_conductivity,
    MIN(conductivity) AS min_conductivity,
    MAX(conductivity) AS max_conductivity,
    AVG(temperature) AS avg_temperature,
    AVG(flow_rate) AS avg_flow_rate,
    MAX(water_meter1) - MIN(water_meter1) AS water_usage_gal,
    COUNT(*) FILTER (WHERE blowdown_active) AS blowdown_minutes,
    COUNT(*) FILTER (WHERE pump1_active) AS pump1_minutes,
    COUNT(*) FILTER (WHERE pump2_active) AS pump2_minutes,
    COUNT(*) FILTER (WHERE pump3_active) AS pump3_minutes
FROM sensor_readings
GROUP BY bucket;

-- Refresh hourly aggregates
SELECT add_continuous_aggregate_policy('sensor_readings_hourly',
    start_offset => INTERVAL '3 hours',
    end_offset => INTERVAL '1 hour',
    schedule_interval => INTERVAL '1 hour');

-- Daily summary
CREATE MATERIALIZED VIEW sensor_readings_daily
WITH (timescaledb.continuous) AS
SELECT
    time_bucket('1 day', time) AS bucket,
    AVG(conductivity) AS avg_conductivity,
    MIN(conductivity) AS min_conductivity,
    MAX(conductivity) AS max_conductivity,
    AVG(temperature) AS avg_temperature,
    MAX(water_meter1) - MIN(water_meter1) AS water_usage_gal,
    COUNT(*) FILTER (WHERE blowdown_active) AS blowdown_count
FROM sensor_readings
GROUP BY bucket;

SELECT add_continuous_aggregate_policy('sensor_readings_daily',
    start_offset => INTERVAL '3 days',
    end_offset => INTERVAL '1 day',
    schedule_interval => INTERVAL '1 day');

-- ============================================
-- USEFUL VIEWS
-- ============================================

-- Current system state (latest readings)
CREATE VIEW current_state AS
SELECT * FROM sensor_readings
ORDER BY time DESC
LIMIT 1;

-- Active alarms
CREATE VIEW active_alarms AS
SELECT * FROM alarms
WHERE state = 'active'
ORDER BY time DESC;

-- Today's statistics
CREATE VIEW today_stats AS
SELECT
    COUNT(*) as reading_count,
    AVG(conductivity) as avg_conductivity,
    MIN(conductivity) as min_conductivity,
    MAX(conductivity) as max_conductivity,
    AVG(temperature) as avg_temperature,
    MAX(water_meter1) - MIN(water_meter1) as water_used_gal
FROM sensor_readings
WHERE time > CURRENT_DATE;

-- ============================================
-- API USER (limited permissions)
-- ============================================
CREATE USER api_user WITH PASSWORD 'api_password_change_me';
GRANT CONNECT ON DATABASE boiler_data TO api_user;
GRANT USAGE ON SCHEMA public TO api_user;
GRANT INSERT ON sensor_readings, pump_events, blowdown_events, alarms,
               calibration_log, config_changes, system_status TO api_user;
GRANT SELECT ON ALL TABLES IN SCHEMA public TO api_user;

COMMENT ON DATABASE boiler_data IS 'Columbia CT-6 Boiler Dosing Controller Data';
