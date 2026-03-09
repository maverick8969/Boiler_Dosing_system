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
    const {
      timestamp,
      conductivity,
      temperature,
      water_meter1,
      water_meter2,
      flow_rate,
      blowdown_active,
      pump1_active,
      pump2_active,
      pump3_active,
      active_alarms
    } = req.body;

    const time = timestamp ? new Date(timestamp * 1000) : new Date();

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

    await client.query('BEGIN');

    for (const r of readings) {
      const time = r.timestamp ? new Date(r.timestamp * 1000) : new Date();
      await client.query(`
        INSERT INTO sensor_readings
        (time, conductivity, temperature, water_meter1, water_meter2,
         flow_rate, blowdown_active, pump1_active, pump2_active,
         pump3_active, active_alarms)
        VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11)
      `, [time, r.conductivity, r.temperature, r.water_meter1, r.water_meter2,
          r.flow_rate, r.blowdown_active, r.pump1_active, r.pump2_active,
          r.pump3_active, r.active_alarms]);
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
    const { start, end, limit = 1000 } = req.query;
    const startTime = start ? new Date(start) : new Date(Date.now() - 24 * 60 * 60 * 1000);
    const endTime = end ? new Date(end) : new Date();

    const result = await pool.query(`
      SELECT * FROM sensor_readings
      WHERE time BETWEEN $1 AND $2
      ORDER BY time DESC
      LIMIT $3
    `, [startTime, endTime, Math.min(limit, 10000)]);

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
    const {
      timestamp,
      pump_id,
      pump_name,
      event_type,
      duration_ms,
      volume_ml,
      trigger_source,
      feed_mode
    } = req.body;

    const time = timestamp ? new Date(timestamp * 1000) : new Date();

    await pool.query(`
      INSERT INTO pump_events
      (time, pump_id, pump_name, event_type, duration_ms, volume_ml, trigger_source, feed_mode)
      VALUES ($1, $2, $3, $4, $5, $6, $7, $8)
    `, [time, pump_id, pump_name, event_type, duration_ms, volume_ml, trigger_source, feed_mode]);

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
    const {
      timestamp,
      event_type,
      duration_sec,
      start_conductivity,
      end_conductivity,
      water_volume_gal,
      trigger_source,
      sampling_mode
    } = req.body;

    const time = timestamp ? new Date(timestamp * 1000) : new Date();

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
    const {
      timestamp,
      alarm_code,
      alarm_name,
      severity,
      state,
      value,
      setpoint,
      message
    } = req.body;

    const time = timestamp ? new Date(timestamp * 1000) : new Date();

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
    const {
      timestamp,
      sensor_type,
      cal_point,
      reference_value,
      measured_raw,
      previous_factor,
      new_factor,
      technician
    } = req.body;

    const time = timestamp ? new Date(timestamp * 1000) : new Date();

    await pool.query(`
      INSERT INTO calibration_log
      (time, sensor_type, cal_point, reference_value, measured_raw,
       previous_factor, new_factor, technician)
      VALUES ($1, $2, $3, $4, $5, $6, $7, $8)
    `, [time, sensor_type, cal_point, reference_value, measured_raw,
        previous_factor, new_factor, technician]);

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
    const { timestamp, parameter, old_value, new_value, source } = req.body;
    const time = timestamp ? new Date(timestamp * 1000) : new Date();

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
    const {
      timestamp,
      uptime_sec,
      free_heap,
      wifi_rssi,
      cpu_temp,
      nvs_free_entries,
      error_count
    } = req.body;

    const time = timestamp ? new Date(timestamp * 1000) : new Date();

    await pool.query(`
      INSERT INTO system_status
      (time, uptime_sec, free_heap, wifi_rssi, cpu_temp, nvs_free_entries, error_count)
      VALUES ($1, $2, $3, $4, $5, $6, $7)
    `, [time, uptime_sec, free_heap, wifi_rssi, cpu_temp, nvs_free_entries, error_count]);

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
    const { hours = 24 } = req.query;
    const result = await pool.query(`
      SELECT * FROM sensor_readings_hourly
      WHERE bucket > NOW() - INTERVAL '${Math.min(hours, 168)} hours'
      ORDER BY bucket DESC
    `);
    res.json(result.rows);
  } catch (err) {
    res.status(500).json({ error: err.message });
  }
});

app.get('/api/stats/daily', async (req, res) => {
  try {
    const { days = 30 } = req.query;
    const result = await pool.query(`
      SELECT * FROM sensor_readings_daily
      WHERE bucket > NOW() - INTERVAL '${Math.min(days, 365)} days'
      ORDER BY bucket DESC
    `);
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
  return m != null && typeof m.value === 'number' ? m.value : defaultVal;
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
        const time = doc.timestamp ? new Date(doc.timestamp * 1000) : new Date();
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
        const time = doc.timestamp ? new Date(doc.timestamp * 1000) : new Date();
        if (doc.type === 'alarm') {
          const state = doc.active ? 'active' : 'cleared';
          await pool.query(
            `INSERT INTO alarms (time, device_id, alarm_code, alarm_name, state, value)
             VALUES ($1, $2, $3, $4, $5, $6)`,
            [
              time,
              deviceId,
              doc.alarm_code ?? 0,
              doc.alarm_name || '',
              state,
              doc.trigger_value ?? null,
            ]
          );
        } else if (doc.type === 'event') {
          await pool.query(
            `INSERT INTO pump_events (time, device_id, pump_id, event_type)
             VALUES ($1, $2, $3, $4)`,
            [time, deviceId, 0, doc.event_type || 'event']
          );
        }
      } else if (suffix === 'health') {
        if (payload.toString() === 'offline') return; // LWT, no insert
        const time = doc.timestamp ? new Date(doc.timestamp * 1000) : new Date();
        await pool.query(
          `INSERT INTO system_status (time, device_id, uptime_sec, free_heap, error_count)
           VALUES ($1, $2, $3, $4, $5)`,
          [
            time,
            deviceId,
            doc.uptime_sec ?? null,
            doc.free_heap ?? null,
            doc.active_alarms ?? 0,
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
