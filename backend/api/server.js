/**
 * Boiler Dosing Controller API Server
 * Receives data from ESP32 via HTTP and/or MQTT and stores in TimescaleDB
 * Phase E: MQTT gateway subscribes to device/+/metrics, alarm, health
 */

const express = require('express');
const { Pool } = require('pg');
const cors = require('cors');
const helmet = require('helmet');
const morgan = require('morgan');
const mqtt = require('mqtt');

const app = express();
const PORT = process.env.PORT || 3000;

// Database connection pool
const pool = new Pool({
  host: process.env.DB_HOST || 'localhost',
  port: process.env.DB_PORT || 5432,
  user: process.env.DB_USER || 'boiler',
  password: process.env.DB_PASSWORD || 'boiler_secret_123',
  database: process.env.DB_NAME || 'boiler_data',
  max: 10,
  idleTimeoutMillis: 30000,
  connectionTimeoutMillis: 2000,
});

// API key for ESP32 authentication
const API_KEY = process.env.API_KEY || 'esp32_api_key_change_me';

// MQTT gateway (Phase E)
const MQTT_URL = process.env.MQTT_URL || 'mqtt://localhost:1883';
const MQTT_ENABLED = process.env.MQTT_ENABLED !== 'false';

// Middleware
app.use(helmet());
app.use(cors());
app.use(express.json({ limit: '100kb' }));
app.use(morgan('combined'));

// API key authentication middleware
const authenticateApiKey = (req, res, next) => {
  const apiKey = req.headers['x-api-key'] || req.query.api_key;
  if (apiKey !== API_KEY) {
    return res.status(401).json({ error: 'Invalid API key' });
  }
  next();
};

// Validation helpers: ensure valid types and avoid Invalid Date / DB errors
function parseTimestamp(ts) {
  if (ts === undefined || ts === null) return new Date();
  const n = Number(ts);
  if (!Number.isFinite(n)) return null;
  return new Date(n * 1000);
}

function isValidNumber(v) {
  return v === undefined || v === null || (typeof v === 'number' && Number.isFinite(v));
}

function validateReading(body) {
  if (!body || typeof body !== 'object') return { ok: false, error: 'Request body must be a JSON object' };
  const time = parseTimestamp(body.timestamp);
  if (time === null) return { ok: false, error: 'timestamp must be a finite number (Unix seconds) or omitted' };
  if (!isValidNumber(body.conductivity)) return { ok: false, error: 'conductivity must be a number or omitted' };
  if (!isValidNumber(body.temperature)) return { ok: false, error: 'temperature must be a number or omitted' };
  if (!isValidNumber(body.water_meter1) && body.water_meter1 != null) return { ok: false, error: 'water_meter1 must be a number or omitted' };
  if (!isValidNumber(body.water_meter2) && body.water_meter2 != null) return { ok: false, error: 'water_meter2 must be a number or omitted' };
  if (!isValidNumber(body.flow_rate) && body.flow_rate != null) return { ok: false, error: 'flow_rate must be a number or omitted' };
  if (!isValidNumber(body.active_alarms) && body.active_alarms != null) return { ok: false, error: 'active_alarms must be a number or omitted' };
  return { ok: true, time };
}

function validatePumpEvent(body) {
  if (!body || typeof body !== 'object') return { ok: false, error: 'Request body must be a JSON object' };
  const time = parseTimestamp(body.timestamp);
  if (time === null) return { ok: false, error: 'timestamp must be a finite number (Unix seconds) or omitted' };
  if (typeof body.pump_id !== 'number' || !Number.isInteger(body.pump_id)) return { ok: false, error: 'pump_id must be an integer' };
  if (!body.event_type || typeof body.event_type !== 'string') return { ok: false, error: 'event_type is required and must be a string' };
  return { ok: true, time };
}

function validateBlowdownEvent(body) {
  if (!body || typeof body !== 'object') return { ok: false, error: 'Request body must be a JSON object' };
  const time = parseTimestamp(body.timestamp);
  if (time === null) return { ok: false, error: 'timestamp must be a finite number (Unix seconds) or omitted' };
  if (!body.event_type || typeof body.event_type !== 'string') return { ok: false, error: 'event_type is required and must be a string' };
  return { ok: true, time };
}

function validateAlarm(body) {
  if (!body || typeof body !== 'object') return { ok: false, error: 'Request body must be a JSON object' };
  const time = parseTimestamp(body.timestamp);
  if (time === null) return { ok: false, error: 'timestamp must be a finite number (Unix seconds) or omitted' };
  if (typeof body.alarm_code !== 'number' || !Number.isInteger(body.alarm_code)) return { ok: false, error: 'alarm_code must be an integer' };
  if (!body.state || typeof body.state !== 'string') return { ok: false, error: 'state is required and must be a string' };
  return { ok: true, time };
}

function validateCalibration(body) {
  if (!body || typeof body !== 'object') return { ok: false, error: 'Request body must be a JSON object' };
  const time = parseTimestamp(body.timestamp);
  if (time === null) return { ok: false, error: 'timestamp must be a finite number (Unix seconds) or omitted' };
  if (!body.sensor_type || typeof body.sensor_type !== 'string') return { ok: false, error: 'sensor_type is required and must be a string' };
  return { ok: true, time };
}

function validateConfigChange(body) {
  if (!body || typeof body !== 'object') return { ok: false, error: 'Request body must be a JSON object' };
  const time = parseTimestamp(body.timestamp);
  if (time === null) return { ok: false, error: 'timestamp must be a finite number (Unix seconds) or omitted' };
  if (!body.parameter || typeof body.parameter !== 'string') return { ok: false, error: 'parameter is required and must be a string' };
  return { ok: true, time };
}

function validateSystemStatus(body) {
  if (!body || typeof body !== 'object') return { ok: false, error: 'Request body must be a JSON object' };
  const time = parseTimestamp(body.timestamp);
  if (time === null) return { ok: false, error: 'timestamp must be a finite number (Unix seconds) or omitted' };
  if (!isValidNumber(body.uptime_sec) && body.uptime_sec != null) return { ok: false, error: 'uptime_sec must be a number or omitted' };
  if (!isValidNumber(body.free_heap) && body.free_heap != null) return { ok: false, error: 'free_heap must be a number or omitted' };
  if (!isValidNumber(body.error_count) && body.error_count != null) return { ok: false, error: 'error_count must be a number or omitted' };
  return { ok: true, time };
}

// Health check endpoint (no auth required)
app.get('/health', async (req, res) => {
  try {
    await pool.query('SELECT 1');
    res.json({ status: 'healthy', timestamp: new Date().toISOString() });
  } catch (err) {
    res.status(500).json({ status: 'unhealthy', error: err.message });
  }
});

// ============================================
// SENSOR READINGS
// ============================================
app.post('/api/readings', authenticateApiKey, async (req, res) => {
  try {
    const body = req.body;
    const validated = validateReading(body);
    if (!validated.ok) return res.status(400).json({ error: validated.error });

    const time = validated.time;
    const conductivity = body.conductivity != null && Number.isFinite(Number(body.conductivity)) ? Number(body.conductivity) : null;
    const temperature = body.temperature != null && Number.isFinite(Number(body.temperature)) ? Number(body.temperature) : null;
    const water_meter1 = body.water_meter1 != null && Number.isFinite(Number(body.water_meter1)) ? Math.round(Number(body.water_meter1)) : null;
    const water_meter2 = body.water_meter2 != null && Number.isFinite(Number(body.water_meter2)) ? Math.round(Number(body.water_meter2)) : null;
    const flow_rate = body.flow_rate != null && Number.isFinite(Number(body.flow_rate)) ? Number(body.flow_rate) : null;
    const blowdown_active = Boolean(body.blowdown_active);
    const pump1_active = Boolean(body.pump1_active);
    const pump2_active = Boolean(body.pump2_active);
    const pump3_active = Boolean(body.pump3_active);
    const active_alarms = body.active_alarms != null && Number.isFinite(Number(body.active_alarms)) ? Math.round(Number(body.active_alarms)) : 0;

    await pool.query(`
      INSERT INTO sensor_readings
      (time, conductivity, temperature, water_meter1, water_meter2,
       flow_rate, blowdown_active, pump1_active, pump2_active,
       pump3_active, active_alarms)
      VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11)
    `, [time, conductivity, temperature, water_meter1, water_meter2,
        flow_rate, blowdown_active, pump1_active, pump2_active,
        pump3_active, active_alarms]);

    res.status(201).json({ success: true });
  } catch (err) {
    console.error('Error inserting reading:', err);
    res.status(500).json({ error: err.message });
  }
});

// Batch insert readings (for offline buffer flush)
app.post('/api/readings/batch', authenticateApiKey, async (req, res) => {
  const client = await pool.connect();
  try {
    const { readings } = req.body;
    if (!Array.isArray(readings)) {
      return res.status(400).json({ error: 'readings must be an array' });
    }

    for (let i = 0; i < readings.length; i++) {
      const v = validateReading(readings[i]);
      if (!v.ok) return res.status(400).json({ error: `readings[${i}]: ${v.error}` });
    }

    await client.query('BEGIN');

    for (const r of readings) {
      const time = parseTimestamp(r.timestamp);
      const conductivity = r.conductivity != null && Number.isFinite(Number(r.conductivity)) ? Number(r.conductivity) : null;
      const temperature = r.temperature != null && Number.isFinite(Number(r.temperature)) ? Number(r.temperature) : null;
      const water_meter1 = r.water_meter1 != null && Number.isFinite(Number(r.water_meter1)) ? Math.round(Number(r.water_meter1)) : null;
      const water_meter2 = r.water_meter2 != null && Number.isFinite(Number(r.water_meter2)) ? Math.round(Number(r.water_meter2)) : null;
      const flow_rate = r.flow_rate != null && Number.isFinite(Number(r.flow_rate)) ? Number(r.flow_rate) : null;
      const blowdown_active = Boolean(r.blowdown_active);
      const pump1_active = Boolean(r.pump1_active);
      const pump2_active = Boolean(r.pump2_active);
      const pump3_active = Boolean(r.pump3_active);
      const active_alarms = r.active_alarms != null && Number.isFinite(Number(r.active_alarms)) ? Math.round(Number(r.active_alarms)) : 0;

      await client.query(`
        INSERT INTO sensor_readings
        (time, conductivity, temperature, water_meter1, water_meter2,
         flow_rate, blowdown_active, pump1_active, pump2_active,
         pump3_active, active_alarms)
        VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11)
      `, [time, conductivity, temperature, water_meter1, water_meter2,
          flow_rate, blowdown_active, pump1_active, pump2_active,
          pump3_active, active_alarms]);
    }

    await client.query('COMMIT');
    res.status(201).json({ success: true, count: readings.length });
  } catch (err) {
    await client.query('ROLLBACK');
    console.error('Error batch inserting readings:', err);
    res.status(500).json({ error: err.message });
  } finally {
    client.release();
  }
});

// Get latest reading
app.get('/api/readings/latest', async (req, res) => {
  try {
    const result = await pool.query(`
      SELECT * FROM sensor_readings ORDER BY time DESC LIMIT 1
    `);
    res.json(result.rows[0] || {});
  } catch (err) {
    res.status(500).json({ error: err.message });
  }
});

// Get readings for time range
app.get('/api/readings', async (req, res) => {
  try {
    // Clamp limit to 1-10000 to prevent database overload
    const limit = Math.min(Math.max(parseInt(req.query.limit, 10) || 1000, 1), 10000);
    const startTime = req.query.start ? new Date(req.query.start) : new Date(Date.now() - 24 * 60 * 60 * 1000);
    const endTime = req.query.end ? new Date(req.query.end) : new Date();

    // Reject invalid date strings to prevent DB query errors
    if (isNaN(startTime.getTime()) || isNaN(endTime.getTime())) {
      return res.status(400).json({ error: 'Invalid start or end date' });
    }

    const result = await pool.query(`
      SELECT * FROM sensor_readings
      WHERE time BETWEEN $1 AND $2
      ORDER BY time DESC
      LIMIT $3
    `, [startTime, endTime, limit]);

    res.json(result.rows);
  } catch (err) {
    res.status(500).json({ error: err.message });
  }
});

// ============================================
// PUMP EVENTS
// ============================================
app.post('/api/events/pump', authenticateApiKey, async (req, res) => {
  try {
    const validated = validatePumpEvent(req.body);
    if (!validated.ok) return res.status(400).json({ error: validated.error });

    const { pump_id, pump_name, event_type, duration_ms, volume_ml, trigger_source, feed_mode } = req.body;
    const time = validated.time;

    const pName = typeof pump_name === 'string' ? pump_name : null;
    const durMs = isValidNumber(duration_ms) ? Math.round(duration_ms) : null;
    const vMl = isValidNumber(volume_ml) ? volume_ml : null;
    const tSrc = typeof trigger_source === 'string' ? trigger_source : null;
    const fMode = typeof feed_mode === 'string' ? feed_mode : null;

    await pool.query(`
      INSERT INTO pump_events
      (time, pump_id, pump_name, event_type, duration_ms, volume_ml, trigger_source, feed_mode)
      VALUES ($1, $2, $3, $4, $5, $6, $7, $8)
    `, [time, pump_id, pName, event_type, durMs, vMl, tSrc, fMode]);

    res.status(201).json({ success: true });
  } catch (err) {
    console.error('Error inserting pump event:', err);
    res.status(500).json({ error: err.message });
  }
});

// ============================================
// BLOWDOWN EVENTS
// ============================================
app.post('/api/events/blowdown', authenticateApiKey, async (req, res) => {
  try {
    const validated = validateBlowdownEvent(req.body);
    if (!validated.ok) return res.status(400).json({ error: validated.error });

    const { event_type, duration_sec, start_conductivity, end_conductivity, water_volume_gal, trigger_source, sampling_mode } = req.body;
    const time = validated.time;

    await pool.query(`
      INSERT INTO blowdown_events
      (time, event_type, duration_sec, start_conductivity, end_conductivity,
       water_volume_gal, trigger_source, sampling_mode)
      VALUES ($1, $2, $3, $4, $5, $6, $7, $8)
    `, [time, event_type, duration_sec, start_conductivity, end_conductivity,
        water_volume_gal, trigger_source, sampling_mode]);

    res.status(201).json({ success: true });
  } catch (err) {
    console.error('Error inserting blowdown event:', err);
    res.status(500).json({ error: err.message });
  }
});

// ============================================
// ALARMS
// ============================================
app.post('/api/alarms', authenticateApiKey, async (req, res) => {
  try {
    const validated = validateAlarm(req.body);
    if (!validated.ok) return res.status(400).json({ error: validated.error });

    const { alarm_code, alarm_name, severity, state, value, setpoint, message } = req.body;
    const time = validated.time;

    await pool.query(`
      INSERT INTO alarms
      (time, alarm_code, alarm_name, severity, state, value, setpoint, message)
      VALUES ($1, $2, $3, $4, $5, $6, $7, $8)
    `, [time, alarm_code, alarm_name, severity, state, value, setpoint, message]);

    res.status(201).json({ success: true });
  } catch (err) {
    console.error('Error inserting alarm:', err);
    res.status(500).json({ error: err.message });
  }
});

// Get active alarms
app.get('/api/alarms/active', async (req, res) => {
  try {
    const result = await pool.query(`
      SELECT * FROM alarms WHERE state = 'active' ORDER BY time DESC
    `);
    res.json(result.rows);
  } catch (err) {
    res.status(500).json({ error: err.message });
  }
});

// ============================================
// CALIBRATION LOG
// ============================================
app.post('/api/calibration', authenticateApiKey, async (req, res) => {
  try {
    const validated = validateCalibration(req.body);
    if (!validated.ok) return res.status(400).json({ error: validated.error });

    const {
      sensor_type,
      cal_point,
      reference_value,
      measured_raw,
      previous_factor,
      new_factor,
      technician
    } = req.body;

    const time = validated.time;
    const cPoint = isValidNumber(cal_point) ? cal_point : null;
    const rValue = isValidNumber(reference_value) ? reference_value : null;
    const mRaw = isValidNumber(measured_raw) ? measured_raw : null;
    const pFactor = isValidNumber(previous_factor) ? previous_factor : null;
    const nFactor = isValidNumber(new_factor) ? new_factor : null;

    await pool.query(`
      INSERT INTO calibration_log
      (time, sensor_type, cal_point, reference_value, measured_raw,
       previous_factor, new_factor, technician)
      VALUES ($1, $2, $3, $4, $5, $6, $7, $8)
    `, [time, sensor_type, cPoint, rValue, mRaw,
        pFactor, nFactor, technician]);

    res.status(201).json({ success: true });
  } catch (err) {
    console.error('Error inserting calibration:', err);
    res.status(500).json({ error: err.message });
  }
});

// ============================================
// CONFIG CHANGES
// ============================================
app.post('/api/config', authenticateApiKey, async (req, res) => {
  try {
    const validated = validateConfigChange(req.body);
    if (!validated.ok) return res.status(400).json({ error: validated.error });

    const { parameter, old_value, new_value, source } = req.body;
    const time = validated.time;

    await pool.query(`
      INSERT INTO config_changes (time, parameter, old_value, new_value, source)
      VALUES ($1, $2, $3, $4, $5)
    `, [time, parameter, old_value, new_value, source]);

    res.status(201).json({ success: true });
  } catch (err) {
    console.error('Error inserting config change:', err);
    res.status(500).json({ error: err.message });
  }
});

// ============================================
// SYSTEM STATUS
// ============================================
app.post('/api/system/status', authenticateApiKey, async (req, res) => {
  try {
    const validated = validateSystemStatus(req.body);
    if (!validated.ok) return res.status(400).json({ error: validated.error });

    const {
      uptime_sec,
      free_heap,
      wifi_rssi,
      cpu_temp,
      nvs_free_entries,
      error_count
    } = req.body;

    const time = validated.time;
    const uptime = isValidNumber(uptime_sec) ? Math.round(uptime_sec) : null;
    const heap = isValidNumber(free_heap) ? Math.round(free_heap) : null;
    const rssi = isValidNumber(wifi_rssi) ? Math.round(wifi_rssi) : null;
    const temp = isValidNumber(cpu_temp) ? cpu_temp : null;
    const nvs = isValidNumber(nvs_free_entries) ? Math.round(nvs_free_entries) : null;
    const errCount = isValidNumber(error_count) ? Math.round(error_count) : null;

    await pool.query(`
      INSERT INTO system_status
      (time, uptime_sec, free_heap, wifi_rssi, cpu_temp, nvs_free_entries, error_count)
      VALUES ($1, $2, $3, $4, $5, $6, $7)
    `, [time, uptime, heap, rssi, temp, nvs, errCount]);

    res.status(201).json({ success: true });
  } catch (err) {
    console.error('Error inserting system status:', err);
    res.status(500).json({ error: err.message });
  }
});

// ============================================
// STATISTICS ENDPOINTS
// ============================================
app.get('/api/stats/today', async (req, res) => {
  try {
    const result = await pool.query(`SELECT * FROM today_stats`);
    res.json(result.rows[0] || {});
  } catch (err) {
    res.status(500).json({ error: err.message });
  }
});

app.get('/api/stats/hourly', async (req, res) => {
  try {
    // Parse and clamp interval to prevent SQL injection and invalid intervals
    const hours = Math.min(Math.max(parseInt(req.query.hours, 10) || 24, 1), 168);
    const result = await pool.query(`
      SELECT * FROM sensor_readings_hourly
      WHERE bucket > NOW() - INTERVAL '1 hour' * $1
      ORDER BY bucket DESC
    `, [hours]);
    res.json(result.rows);
  } catch (err) {
    res.status(500).json({ error: err.message });
  }
});

app.get('/api/stats/daily', async (req, res) => {
  try {
    // Parse and clamp interval to prevent SQL injection and invalid intervals
    const days = Math.min(Math.max(parseInt(req.query.days, 10) || 30, 1), 365);
    const result = await pool.query(`
      SELECT * FROM sensor_readings_daily
      WHERE bucket > NOW() - INTERVAL '1 day' * $1
      ORDER BY bucket DESC
    `, [days]);
    res.json(result.rows);
  } catch (err) {
    res.status(500).json({ error: err.message });
  }
});

// Error handling middleware
app.use((err, req, res, next) => {
  console.error('Unhandled error:', err);
  res.status(500).json({ error: 'Internal server error' });
});

// ============================================
// MQTT GATEWAY (Phase E) - device/+/metrics, alarm, health
// ============================================
function deviceIdFromTopic(topic) {
  const parts = topic.split('/');
  return parts.length >= 2 ? parts[1] : null;
}

function metricValue(metrics, id, defaultVal = null) {
  const m = (metrics || []).find((x) => x.id === id);
  return m != null && typeof m.value === 'number' && Number.isFinite(m.value) ? m.value : defaultVal;
}

function startMqttGateway() {
  if (!MQTT_ENABLED) return;
  const client = mqtt.connect(MQTT_URL, { reconnectPeriod: 10000 });
  client.on('connect', () => {
    client.subscribe('device/+/metrics', { qos: 0 });
    client.subscribe('device/+/alarm', { qos: 0 });
    client.subscribe('device/+/health', { qos: 0 });
    console.log('MQTT gateway connected, subscribed to device/+/metrics, alarm, health');
  });
  client.on('error', (err) => console.error('MQTT error:', err));

  client.on('message', async (topic, payload) => {
    const deviceId = deviceIdFromTopic(topic);
    if (!deviceId) return;
    let doc;
    try {
      doc = JSON.parse(payload.toString());
    } catch (e) {
      console.error('MQTT invalid JSON', topic, e.message);
      return;
    }
    const suffix = topic.split('/').pop();
    try {
      if (suffix === 'metrics') {
        const time = parseTimestamp(doc.timestamp) || new Date();
        const metrics = doc.metrics || [];
        await pool.query(
          `INSERT INTO sensor_readings
           (time, device_id, conductivity, temperature, water_meter1, water_meter2,
            flow_rate, blowdown_active, pump1_active, pump2_active, pump3_active, active_alarms)
           VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12)`,
          [
            time,
            deviceId,
            metricValue(metrics, 'conductivity', 0),
            metricValue(metrics, 'temperature', 0),
            Math.round(metricValue(metrics, 'water_meter1', 0)),
            Math.round(metricValue(metrics, 'water_meter2', 0)),
            metricValue(metrics, 'flow_rate', 0),
            Boolean(metricValue(metrics, 'blowdown_active', 0)),
            Boolean(metricValue(metrics, 'pump1_active', 0)),
            Boolean(metricValue(metrics, 'pump2_active', 0)),
            Boolean(metricValue(metrics, 'pump3_active', 0)),
            Math.round(metricValue(metrics, 'active_alarms', 0)),
          ]
        );
      } else if (suffix === 'alarm') {
        const time = parseTimestamp(doc.timestamp) || new Date();
        if (doc.type === 'alarm') {
          const state = doc.active ? 'active' : 'cleared';
          await pool.query(
            `INSERT INTO alarms (time, device_id, alarm_code, alarm_name, state, value)
             VALUES ($1, $2, $3, $4, $5, $6)`,
            [
              time,
              deviceId,
              isValidNumber(doc.alarm_code) ? Math.round(doc.alarm_code) : 0,
              typeof doc.alarm_name === 'string' ? doc.alarm_name.substring(0, 64) : '',
              state,
              isValidNumber(doc.trigger_value) ? doc.trigger_value : null,
            ]
          );
        } else if (doc.type === 'event') {
          await pool.query(
            `INSERT INTO pump_events (time, device_id, pump_id, event_type)
             VALUES ($1, $2, $3, $4)`,
            [time, deviceId, 0, typeof doc.event_type === 'string' ? doc.event_type.substring(0, 16) : 'event']
          );
        }
      } else if (suffix === 'health') {
        if (payload.toString() === 'offline') return; // LWT, no insert
        const time = parseTimestamp(doc.timestamp) || new Date();
        await pool.query(
          `INSERT INTO system_status (time, device_id, uptime_sec, free_heap, error_count)
           VALUES ($1, $2, $3, $4, $5)`,
          [
            time,
            deviceId,
            isValidNumber(doc.uptime_sec) ? Math.round(doc.uptime_sec) : null,
            isValidNumber(doc.free_heap) ? Math.round(doc.free_heap) : null,
            isValidNumber(doc.error_count) ? Math.round(doc.error_count) : null,
          ]
        );
      }
    } catch (err) {
      console.error('MQTT gateway DB insert failed', topic, err.message);
    }
  });
}

// Start server
app.listen(PORT, '0.0.0.0', () => {
  console.log(`Boiler API server running on port ${PORT}`);
  console.log(`Health check: http://localhost:${PORT}/health`);
  startMqttGateway();
});

// Graceful shutdown
process.on('SIGTERM', async () => {
  console.log('SIGTERM received, shutting down...');
  await pool.end();
  process.exit(0);
});
