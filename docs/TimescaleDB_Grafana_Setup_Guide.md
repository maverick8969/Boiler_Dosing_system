# TimescaleDB & Grafana Setup Guide
## Raspberry Pi 400 Backend for Boiler Dosing Controller

This guide walks through setting up a Raspberry Pi 400 as a data logging and visualization backend for the ESP32 Boiler Dosing Controller.

---

## Table of Contents

1. [System Overview](#system-overview)
2. [Prerequisites](#prerequisites)
3. [Initial Pi Setup](#initial-pi-setup)
4. [Install Docker](#install-docker)
5. [Install TimescaleDB](#install-timescaledb)
6. [Create Database Schema](#create-database-schema)
7. [Install API Server](#install-api-server)
8. [Install Grafana](#install-grafana)
9. [Configure Grafana Dashboards](#configure-grafana-dashboards)
10. [ESP32 Controller Configuration](#esp32-controller-configuration)
11. [Maintenance & Backup](#maintenance--backup)
12. [Troubleshooting](#troubleshooting)

---

## System Overview

```
┌─────────────────────┐         ┌─────────────────────────────────────────┐
│  ESP32 Controller   │         │        Raspberry Pi 400                 │
│                     │         │                                         │
│  ┌───────────────┐  │  WiFi   │  ┌─────────────┐    ┌───────────────┐  │
│  │ Sensors       │  │ ──────► │  │ API Server  │───►│ TimescaleDB   │  │
│  │ Pumps         │  │  HTTP   │  │ (Port 3000) │    │ (Port 5432)   │  │
│  │ Fuzzy Logic   │  │         │  └─────────────┘    └───────────────┘  │
│  └───────────────┘  │         │                            │           │
│                     │         │  ┌─────────────┐           │           │
│  POST /api/log      │         │  │  Grafana    │◄──────────┘           │
│                     │         │  │ (Port 3001) │                       │
└─────────────────────┘         │  └─────────────┘                       │
                                │        ▲                               │
                                └────────┼───────────────────────────────┘
                                         │
                                    Web Browser
                                  (View Dashboards)
```

### Components

| Component | Purpose | Port |
|-----------|---------|------|
| **TimescaleDB** | Time-series database for sensor data | 5432 |
| **API Server** | REST API to receive ESP32 data | 3000 |
| **Grafana** | Visualization dashboards | 3001 |

---

## Prerequisites

### Hardware
- Raspberry Pi 400 (or Pi 4 with 4GB+ RAM)
- 32GB+ microSD card (64GB recommended)
- Ethernet connection (recommended) or WiFi
- Power supply (official Pi 400 PSU)

### Software
- Raspberry Pi OS (64-bit) - Bookworm or later
- Internet connection for package downloads

### Network
- Static IP address for the Pi (recommended)
- ESP32 and Pi on same network
- Ports 3000, 3001, 5432 accessible on local network

---

## Initial Pi Setup

### 1. Install Raspberry Pi OS

1. Download [Raspberry Pi Imager](https://www.raspberrypi.com/software/)
2. Select **Raspberry Pi OS (64-bit)**
3. Click gear icon for advanced options:
   - Set hostname: `boiler-backend`
   - Enable SSH
   - Set username/password
   - Configure WiFi (if needed)
4. Write to SD card and boot

### 2. Initial Configuration

```bash
# Update system
sudo apt update && sudo apt upgrade -y

# Install essential tools
sudo apt install -y git curl wget htop

# Set static IP (recommended)
sudo nmcli con mod "Wired connection 1" ipv4.addresses 192.168.1.100/24
sudo nmcli con mod "Wired connection 1" ipv4.gateway 192.168.1.1
sudo nmcli con mod "Wired connection 1" ipv4.dns "8.8.8.8 8.8.4.4"
sudo nmcli con mod "Wired connection 1" ipv4.method manual
sudo nmcli con up "Wired connection 1"
```

### 3. Configure Timezone

```bash
sudo timedatectl set-timezone America/New_York  # Adjust to your timezone
timedatectl
```

---

## Install Docker

Docker simplifies installation and management of TimescaleDB and Grafana.

### 1. Install Docker

```bash
# Install Docker
curl -fsSL https://get.docker.com -o get-docker.sh
sudo sh get-docker.sh

# Add user to docker group
sudo usermod -aG docker $USER

# Log out and back in, then verify
docker --version
```

### 2. Install Docker Compose

```bash
sudo apt install -y docker-compose-plugin

# Verify
docker compose version
```

### 3. Create Project Directory

```bash
mkdir -p ~/boiler-backend
cd ~/boiler-backend
```

---

## Install TimescaleDB

### 1. Create Docker Compose File

```bash
cat > ~/boiler-backend/docker-compose.yml << 'EOF'
version: '3.8'

services:
  timescaledb:
    image: timescale/timescaledb:latest-pg15
    container_name: timescaledb
    restart: unless-stopped
    ports:
      - "5432:5432"
    environment:
      POSTGRES_USER: boiler
      POSTGRES_PASSWORD: boiler_water_2024
      POSTGRES_DB: boiler_data
    volumes:
      - timescale_data:/var/lib/postgresql/data
      - ./init-db:/docker-entrypoint-initdb.d
    healthcheck:
      test: ["CMD-SHELL", "pg_isready -U boiler -d boiler_data"]
      interval: 10s
      timeout: 5s
      retries: 5

  api-server:
    build: ./api
    container_name: boiler-api
    restart: unless-stopped
    ports:
      - "3000:3000"
    environment:
      DATABASE_URL: postgres://boiler:boiler_water_2024@timescaledb:5432/boiler_data
      API_KEY: your-secret-api-key-here
      PORT: 3000
    depends_on:
      timescaledb:
        condition: service_healthy

  grafana:
    image: grafana/grafana:latest
    container_name: grafana
    restart: unless-stopped
    ports:
      - "3001:3000"
    environment:
      GF_SECURITY_ADMIN_USER: admin
      GF_SECURITY_ADMIN_PASSWORD: boiler_grafana_2024
      GF_USERS_ALLOW_SIGN_UP: "false"
    volumes:
      - grafana_data:/var/lib/grafana
      - ./grafana/provisioning:/etc/grafana/provisioning
    depends_on:
      - timescaledb

volumes:
  timescale_data:
  grafana_data:
EOF
```

### 2. Start TimescaleDB

```bash
cd ~/boiler-backend
docker compose up -d timescaledb

# Wait for it to be ready
docker compose logs -f timescaledb
# Press Ctrl+C when you see "database system is ready to accept connections"
```

### 3. Verify Connection

```bash
# Connect to database
docker exec -it timescaledb psql -U boiler -d boiler_data

# In psql, verify TimescaleDB extension
\dx

# You should see timescaledb in the list
# Type \q to exit
```

---

## Create Database Schema

### 1. Create Initialization Script

```bash
mkdir -p ~/boiler-backend/init-db

cat > ~/boiler-backend/init-db/01-schema.sql << 'EOF'
-- Enable TimescaleDB extension
CREATE EXTENSION IF NOT EXISTS timescaledb;

-- ============================================================================
-- SENSOR READINGS TABLE (Main time-series data)
-- ============================================================================

CREATE TABLE IF NOT EXISTS sensor_readings (
    time                TIMESTAMPTZ NOT NULL,
    conductivity        DOUBLE PRECISION,
    temperature         DOUBLE PRECISION,
    tds_manual          DOUBLE PRECISION,
    alkalinity_manual   DOUBLE PRECISION,
    sulfite_manual      DOUBLE PRECISION,
    ph_manual           DOUBLE PRECISION,
    water_meter1        BIGINT,
    water_meter2        BIGINT,
    flow_rate           DOUBLE PRECISION,
    blowdown_active     BOOLEAN DEFAULT FALSE,
    pump1_active        BOOLEAN DEFAULT FALSE,
    pump2_active        BOOLEAN DEFAULT FALSE,
    pump3_active        BOOLEAN DEFAULT FALSE,
    active_alarms       INTEGER DEFAULT 0
);

-- Convert to hypertable (TimescaleDB magic)
SELECT create_hypertable('sensor_readings', 'time', if_not_exists => TRUE);

-- Create index for faster queries
CREATE INDEX IF NOT EXISTS idx_readings_time ON sensor_readings (time DESC);

-- ============================================================================
-- FUZZY LOGIC OUTPUT TABLE
-- ============================================================================

CREATE TABLE IF NOT EXISTS fuzzy_outputs (
    time                TIMESTAMPTZ NOT NULL,
    blowdown_rate       DOUBLE PRECISION,
    caustic_rate        DOUBLE PRECISION,
    sulfite_rate        DOUBLE PRECISION,
    acid_rate           DOUBLE PRECISION,
    confidence          VARCHAR(10),
    active_rules        INTEGER,
    max_firing          DOUBLE PRECISION
);

SELECT create_hypertable('fuzzy_outputs', 'time', if_not_exists => TRUE);
CREATE INDEX IF NOT EXISTS idx_fuzzy_time ON fuzzy_outputs (time DESC);

-- ============================================================================
-- ALARM HISTORY TABLE
-- ============================================================================

CREATE TABLE IF NOT EXISTS alarm_history (
    time                TIMESTAMPTZ NOT NULL,
    alarm_code          INTEGER NOT NULL,
    alarm_name          VARCHAR(50),
    active              BOOLEAN,
    value               DOUBLE PRECISION
);

SELECT create_hypertable('alarm_history', 'time', if_not_exists => TRUE);
CREATE INDEX IF NOT EXISTS idx_alarm_time ON alarm_history (time DESC);

-- ============================================================================
-- PUMP EVENTS TABLE
-- ============================================================================

CREATE TABLE IF NOT EXISTS pump_events (
    time                TIMESTAMPTZ NOT NULL,
    pump_id             INTEGER NOT NULL,
    pump_name           VARCHAR(20),
    event_type          VARCHAR(20),  -- 'start', 'stop', 'error'
    duration_ms         BIGINT,
    volume_ml           DOUBLE PRECISION,
    feed_mode           VARCHAR(10)
);

SELECT create_hypertable('pump_events', 'time', if_not_exists => TRUE);
CREATE INDEX IF NOT EXISTS idx_pump_time ON pump_events (time DESC);

-- ============================================================================
-- BLOWDOWN EVENTS TABLE
-- ============================================================================

CREATE TABLE IF NOT EXISTS blowdown_events (
    time                TIMESTAMPTZ NOT NULL,
    event_type          VARCHAR(20),  -- 'recommended', 'started', 'stopped'
    recommendation_pct  DOUBLE PRECISION,
    duration_sec        INTEGER,
    conductivity_before DOUBLE PRECISION,
    conductivity_after  DOUBLE PRECISION
);

SELECT create_hypertable('blowdown_events', 'time', if_not_exists => TRUE);

-- ============================================================================
-- MANUAL TEST ENTRIES TABLE
-- ============================================================================

CREATE TABLE IF NOT EXISTS manual_tests (
    time                TIMESTAMPTZ NOT NULL,
    test_type           VARCHAR(20) NOT NULL,  -- 'tds', 'alkalinity', 'sulfite', 'ph'
    value               DOUBLE PRECISION NOT NULL,
    operator            VARCHAR(50),
    notes               TEXT
);

SELECT create_hypertable('manual_tests', 'time', if_not_exists => TRUE);

-- ============================================================================
-- CONTINUOUS AGGREGATES (Pre-computed rollups for faster dashboards)
-- ============================================================================

-- Hourly averages
CREATE MATERIALIZED VIEW IF NOT EXISTS readings_hourly
WITH (timescaledb.continuous) AS
SELECT
    time_bucket('1 hour', time) AS bucket,
    AVG(conductivity) AS avg_conductivity,
    AVG(temperature) AS avg_temperature,
    MAX(conductivity) AS max_conductivity,
    MIN(conductivity) AS min_conductivity,
    AVG(flow_rate) AS avg_flow_rate,
    SUM(CASE WHEN blowdown_active THEN 1 ELSE 0 END) AS blowdown_minutes
FROM sensor_readings
GROUP BY bucket
WITH NO DATA;

-- Daily summaries
CREATE MATERIALIZED VIEW IF NOT EXISTS readings_daily
WITH (timescaledb.continuous) AS
SELECT
    time_bucket('1 day', time) AS bucket,
    AVG(conductivity) AS avg_conductivity,
    AVG(temperature) AS avg_temperature,
    MAX(conductivity) AS max_conductivity,
    MIN(conductivity) AS min_conductivity,
    MAX(water_meter1) - MIN(water_meter1) AS water_usage_gal,
    COUNT(*) AS sample_count
FROM sensor_readings
GROUP BY bucket
WITH NO DATA;

-- Set up refresh policies
SELECT add_continuous_aggregate_policy('readings_hourly',
    start_offset => INTERVAL '3 hours',
    end_offset => INTERVAL '1 hour',
    schedule_interval => INTERVAL '1 hour',
    if_not_exists => TRUE);

SELECT add_continuous_aggregate_policy('readings_daily',
    start_offset => INTERVAL '3 days',
    end_offset => INTERVAL '1 day',
    schedule_interval => INTERVAL '1 day',
    if_not_exists => TRUE);

-- ============================================================================
-- DATA RETENTION POLICY
-- ============================================================================

-- Keep raw data for 90 days
SELECT add_retention_policy('sensor_readings', INTERVAL '90 days', if_not_exists => TRUE);
SELECT add_retention_policy('fuzzy_outputs', INTERVAL '90 days', if_not_exists => TRUE);
SELECT add_retention_policy('pump_events', INTERVAL '365 days', if_not_exists => TRUE);
SELECT add_retention_policy('alarm_history', INTERVAL '365 days', if_not_exists => TRUE);

-- ============================================================================
-- HELPER FUNCTIONS
-- ============================================================================

-- Function to get latest readings
CREATE OR REPLACE FUNCTION get_latest_readings()
RETURNS TABLE (
    conductivity DOUBLE PRECISION,
    temperature DOUBLE PRECISION,
    flow_rate DOUBLE PRECISION,
    water_total BIGINT,
    blowdown_active BOOLEAN,
    last_update TIMESTAMPTZ
) AS $$
BEGIN
    RETURN QUERY
    SELECT
        sr.conductivity,
        sr.temperature,
        sr.flow_rate,
        sr.water_meter1,
        sr.blowdown_active,
        sr.time
    FROM sensor_readings sr
    ORDER BY sr.time DESC
    LIMIT 1;
END;
$$ LANGUAGE plpgsql;

-- Function to calculate daily water usage
CREATE OR REPLACE FUNCTION daily_water_usage(target_date DATE DEFAULT CURRENT_DATE)
RETURNS DOUBLE PRECISION AS $$
DECLARE
    usage DOUBLE PRECISION;
BEGIN
    SELECT MAX(water_meter1) - MIN(water_meter1)
    INTO usage
    FROM sensor_readings
    WHERE time >= target_date
      AND time < target_date + INTERVAL '1 day';

    RETURN COALESCE(usage, 0);
END;
$$ LANGUAGE plpgsql;

COMMIT;
EOF
```

### 2. Apply Schema

```bash
# Restart TimescaleDB to apply init script
docker compose down
docker compose up -d timescaledb

# Or manually apply if already running:
docker exec -i timescaledb psql -U boiler -d boiler_data < ~/boiler-backend/init-db/01-schema.sql
```

### 3. Verify Tables

```bash
docker exec -it timescaledb psql -U boiler -d boiler_data -c "\dt"
```

Expected output:
```
              List of relations
 Schema |       Name        | Type  | Owner
--------+-------------------+-------+-------
 public | alarm_history     | table | boiler
 public | blowdown_events   | table | boiler
 public | fuzzy_outputs     | table | boiler
 public | manual_tests      | table | boiler
 public | pump_events       | table | boiler
 public | sensor_readings   | table | boiler
```

---

## Install API Server

The API server receives HTTP POST requests from the ESP32 and inserts data into TimescaleDB.

### 1. Create API Directory

```bash
mkdir -p ~/boiler-backend/api
```

### 2. Create package.json

```bash
cat > ~/boiler-backend/api/package.json << 'EOF'
{
  "name": "boiler-api",
  "version": "1.0.0",
  "description": "API server for Boiler Dosing Controller",
  "main": "server.js",
  "scripts": {
    "start": "node server.js"
  },
  "dependencies": {
    "express": "^4.18.2",
    "pg": "^8.11.3",
    "cors": "^2.8.5"
  }
}
EOF
```

### 3. Create API Server

```bash
cat > ~/boiler-backend/api/server.js << 'EOF'
const express = require('express');
const { Pool } = require('pg');
const cors = require('cors');

const app = express();
app.use(cors());
app.use(express.json());

// Database connection
const pool = new Pool({
  connectionString: process.env.DATABASE_URL
});

const API_KEY = process.env.API_KEY || 'default-key';

// Middleware to check API key
const checkApiKey = (req, res, next) => {
  const key = req.headers['x-api-key'] || req.query.api_key;
  if (key !== API_KEY) {
    return res.status(401).json({ error: 'Invalid API key' });
  }
  next();
};

// Health check
app.get('/health', (req, res) => {
  res.json({ status: 'ok', timestamp: new Date().toISOString() });
});

// ============================================================================
// SENSOR READINGS ENDPOINT
// ============================================================================

app.post('/api/readings', checkApiKey, async (req, res) => {
  try {
    const {
      conductivity,
      temperature,
      tds_manual,
      alkalinity_manual,
      sulfite_manual,
      ph_manual,
      water_meter1,
      water_meter2,
      flow_rate,
      blowdown_active,
      pump1_active,
      pump2_active,
      pump3_active,
      active_alarms
    } = req.body;

    await pool.query(
      `INSERT INTO sensor_readings
       (time, conductivity, temperature, tds_manual, alkalinity_manual,
        sulfite_manual, ph_manual, water_meter1, water_meter2, flow_rate,
        blowdown_active, pump1_active, pump2_active, pump3_active, active_alarms)
       VALUES (NOW(), $1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14)`,
      [conductivity, temperature, tds_manual, alkalinity_manual,
       sulfite_manual, ph_manual, water_meter1, water_meter2, flow_rate,
       blowdown_active, pump1_active, pump2_active, pump3_active, active_alarms]
    );

    res.json({ success: true });
  } catch (err) {
    console.error('Error inserting reading:', err);
    res.status(500).json({ error: err.message });
  }
});

// ============================================================================
// FUZZY OUTPUT ENDPOINT
// ============================================================================

app.post('/api/fuzzy', checkApiKey, async (req, res) => {
  try {
    const {
      blowdown_rate,
      caustic_rate,
      sulfite_rate,
      acid_rate,
      confidence,
      active_rules,
      max_firing
    } = req.body;

    await pool.query(
      `INSERT INTO fuzzy_outputs
       (time, blowdown_rate, caustic_rate, sulfite_rate, acid_rate,
        confidence, active_rules, max_firing)
       VALUES (NOW(), $1, $2, $3, $4, $5, $6, $7)`,
      [blowdown_rate, caustic_rate, sulfite_rate, acid_rate,
       confidence, active_rules, max_firing]
    );

    res.json({ success: true });
  } catch (err) {
    console.error('Error inserting fuzzy output:', err);
    res.status(500).json({ error: err.message });
  }
});

// ============================================================================
// ALARM ENDPOINT
// ============================================================================

app.post('/api/alarms', checkApiKey, async (req, res) => {
  try {
    const { alarm_code, alarm_name, active, value } = req.body;

    await pool.query(
      `INSERT INTO alarm_history (time, alarm_code, alarm_name, active, value)
       VALUES (NOW(), $1, $2, $3, $4)`,
      [alarm_code, alarm_name, active, value]
    );

    res.json({ success: true });
  } catch (err) {
    console.error('Error inserting alarm:', err);
    res.status(500).json({ error: err.message });
  }
});

// ============================================================================
// PUMP EVENT ENDPOINT
// ============================================================================

app.post('/api/pump-events', checkApiKey, async (req, res) => {
  try {
    const { pump_id, pump_name, event_type, duration_ms, volume_ml, feed_mode } = req.body;

    await pool.query(
      `INSERT INTO pump_events
       (time, pump_id, pump_name, event_type, duration_ms, volume_ml, feed_mode)
       VALUES (NOW(), $1, $2, $3, $4, $5, $6)`,
      [pump_id, pump_name, event_type, duration_ms, volume_ml, feed_mode]
    );

    res.json({ success: true });
  } catch (err) {
    console.error('Error inserting pump event:', err);
    res.status(500).json({ error: err.message });
  }
});

// ============================================================================
// MANUAL TEST ENTRY ENDPOINT
// ============================================================================

app.post('/api/manual-tests', checkApiKey, async (req, res) => {
  try {
    const { test_type, value, operator, notes } = req.body;

    await pool.query(
      `INSERT INTO manual_tests (time, test_type, value, operator, notes)
       VALUES (NOW(), $1, $2, $3, $4)`,
      [test_type, value, operator, notes]
    );

    res.json({ success: true });
  } catch (err) {
    console.error('Error inserting manual test:', err);
    res.status(500).json({ error: err.message });
  }
});

// ============================================================================
// QUERY ENDPOINTS (for ESP32 or other clients)
// ============================================================================

app.get('/api/latest', async (req, res) => {
  try {
    const result = await pool.query(
      `SELECT * FROM sensor_readings ORDER BY time DESC LIMIT 1`
    );
    res.json(result.rows[0] || {});
  } catch (err) {
    res.status(500).json({ error: err.message });
  }
});

app.get('/api/stats/daily', async (req, res) => {
  try {
    const result = await pool.query(`
      SELECT
        AVG(conductivity) as avg_conductivity,
        MAX(conductivity) as max_conductivity,
        MIN(conductivity) as min_conductivity,
        AVG(temperature) as avg_temperature,
        MAX(water_meter1) - MIN(water_meter1) as water_usage,
        COUNT(*) as samples
      FROM sensor_readings
      WHERE time > NOW() - INTERVAL '24 hours'
    `);
    res.json(result.rows[0] || {});
  } catch (err) {
    res.status(500).json({ error: err.message });
  }
});

// Start server
const PORT = process.env.PORT || 3000;
app.listen(PORT, '0.0.0.0', () => {
  console.log(`Boiler API server running on port ${PORT}`);
});
EOF
```

### 4. Create Dockerfile

```bash
cat > ~/boiler-backend/api/Dockerfile << 'EOF'
FROM node:18-alpine

WORKDIR /app

COPY package*.json ./
RUN npm install --production

COPY . .

EXPOSE 3000

CMD ["npm", "start"]
EOF
```

### 5. Start All Services

```bash
cd ~/boiler-backend
docker compose up -d --build

# Check status
docker compose ps

# View logs
docker compose logs -f api-server
```

### 6. Test API

```bash
# Health check
curl http://localhost:3000/health

# Test posting data (replace API key)
curl -X POST http://localhost:3000/api/readings \
  -H "Content-Type: application/json" \
  -H "X-API-Key: your-secret-api-key-here" \
  -d '{
    "conductivity": 2500,
    "temperature": 42.5,
    "water_meter1": 1000,
    "flow_rate": 1.2,
    "blowdown_active": false,
    "pump1_active": false,
    "pump2_active": true,
    "pump3_active": false,
    "active_alarms": 0
  }'
```

---

## Install Grafana

### 1. Create Grafana Provisioning

```bash
mkdir -p ~/boiler-backend/grafana/provisioning/datasources
mkdir -p ~/boiler-backend/grafana/provisioning/dashboards

# Create datasource config
cat > ~/boiler-backend/grafana/provisioning/datasources/timescaledb.yml << 'EOF'
apiVersion: 1

datasources:
  - name: TimescaleDB
    type: postgres
    url: timescaledb:5432
    database: boiler_data
    user: boiler
    secureJsonData:
      password: boiler_water_2024
    jsonData:
      sslmode: disable
      maxOpenConns: 10
      maxIdleConns: 10
      connMaxLifetime: 14400
      postgresVersion: 1500
      timescaledb: true
    isDefault: true
EOF

# Create dashboard provider config
cat > ~/boiler-backend/grafana/provisioning/dashboards/default.yml << 'EOF'
apiVersion: 1

providers:
  - name: 'Boiler Dashboards'
    orgId: 1
    folder: ''
    type: file
    disableDeletion: false
    updateIntervalSeconds: 30
    options:
      path: /etc/grafana/provisioning/dashboards
EOF
```

### 2. Start Grafana

```bash
cd ~/boiler-backend
docker compose up -d grafana

# Wait for startup
sleep 10

# Check logs
docker compose logs grafana
```

### 3. Access Grafana

1. Open browser to `http://<pi-ip>:3001`
2. Login with:
   - Username: `admin`
   - Password: `boiler_grafana_2024`

---

## Configure Grafana Dashboards

### 1. Create Main Dashboard JSON

```bash
cat > ~/boiler-backend/grafana/provisioning/dashboards/boiler-main.json << 'EOF'
{
  "annotations": {
    "list": []
  },
  "editable": true,
  "fiscalYearStartMonth": 0,
  "graphTooltip": 0,
  "id": null,
  "links": [],
  "liveNow": false,
  "panels": [
    {
      "datasource": {
        "type": "postgres",
        "uid": "${DS_TIMESCALEDB}"
      },
      "fieldConfig": {
        "defaults": {
          "color": {
            "mode": "thresholds"
          },
          "mappings": [],
          "thresholds": {
            "mode": "absolute",
            "steps": [
              { "color": "green", "value": null },
              { "color": "yellow", "value": 2800 },
              { "color": "red", "value": 3200 }
            ]
          },
          "unit": "μS/cm"
        }
      },
      "gridPos": { "h": 6, "w": 6, "x": 0, "y": 0 },
      "id": 1,
      "options": {
        "colorMode": "value",
        "graphMode": "area",
        "justifyMode": "auto",
        "orientation": "auto",
        "reduceOptions": {
          "calcs": ["lastNotNull"],
          "fields": "",
          "values": false
        },
        "textMode": "auto"
      },
      "pluginVersion": "10.0.0",
      "targets": [
        {
          "datasource": { "type": "postgres" },
          "format": "table",
          "rawSql": "SELECT conductivity FROM sensor_readings ORDER BY time DESC LIMIT 1",
          "refId": "A"
        }
      ],
      "title": "Conductivity",
      "type": "stat"
    },
    {
      "datasource": {
        "type": "postgres",
        "uid": "${DS_TIMESCALEDB}"
      },
      "fieldConfig": {
        "defaults": {
          "color": { "mode": "thresholds" },
          "mappings": [],
          "thresholds": {
            "mode": "absolute",
            "steps": [
              { "color": "blue", "value": null },
              { "color": "green", "value": 35 },
              { "color": "yellow", "value": 50 },
              { "color": "red", "value": 60 }
            ]
          },
          "unit": "celsius"
        }
      },
      "gridPos": { "h": 6, "w": 6, "x": 6, "y": 0 },
      "id": 2,
      "options": {
        "colorMode": "value",
        "graphMode": "area",
        "justifyMode": "auto",
        "orientation": "auto",
        "reduceOptions": {
          "calcs": ["lastNotNull"],
          "fields": "",
          "values": false
        }
      },
      "targets": [
        {
          "rawSql": "SELECT temperature FROM sensor_readings ORDER BY time DESC LIMIT 1",
          "refId": "A",
          "format": "table"
        }
      ],
      "title": "Temperature",
      "type": "stat"
    },
    {
      "datasource": { "type": "postgres" },
      "fieldConfig": {
        "defaults": {
          "color": { "mode": "thresholds" },
          "mappings": [],
          "thresholds": {
            "mode": "absolute",
            "steps": [
              { "color": "green", "value": null }
            ]
          },
          "unit": "gallons"
        }
      },
      "gridPos": { "h": 6, "w": 6, "x": 12, "y": 0 },
      "id": 3,
      "options": {
        "colorMode": "value",
        "graphMode": "none",
        "reduceOptions": { "calcs": ["lastNotNull"] }
      },
      "targets": [
        {
          "rawSql": "SELECT water_meter1 FROM sensor_readings ORDER BY time DESC LIMIT 1",
          "refId": "A",
          "format": "table"
        }
      ],
      "title": "Water Total",
      "type": "stat"
    },
    {
      "datasource": { "type": "postgres" },
      "fieldConfig": {
        "defaults": {
          "color": { "mode": "palette-classic" },
          "custom": {
            "axisCenteredZero": false,
            "axisColorMode": "text",
            "axisLabel": "",
            "axisPlacement": "auto",
            "barAlignment": 0,
            "drawStyle": "line",
            "fillOpacity": 10,
            "gradientMode": "none",
            "lineInterpolation": "smooth",
            "lineWidth": 2,
            "pointSize": 5,
            "showPoints": "never",
            "spanNulls": false,
            "stacking": { "group": "A", "mode": "none" }
          },
          "unit": "μS/cm"
        }
      },
      "gridPos": { "h": 8, "w": 12, "x": 0, "y": 6 },
      "id": 4,
      "options": {
        "legend": { "calcs": ["mean", "max", "min"], "displayMode": "table", "placement": "bottom" },
        "tooltip": { "mode": "single" }
      },
      "targets": [
        {
          "rawSql": "SELECT time, conductivity FROM sensor_readings WHERE $__timeFilter(time) ORDER BY time",
          "refId": "A",
          "format": "time_series"
        }
      ],
      "title": "Conductivity History",
      "type": "timeseries"
    },
    {
      "datasource": { "type": "postgres" },
      "fieldConfig": {
        "defaults": {
          "color": { "mode": "palette-classic" },
          "custom": {
            "drawStyle": "line",
            "fillOpacity": 10,
            "lineInterpolation": "smooth",
            "lineWidth": 2
          },
          "unit": "celsius"
        }
      },
      "gridPos": { "h": 8, "w": 12, "x": 12, "y": 6 },
      "id": 5,
      "targets": [
        {
          "rawSql": "SELECT time, temperature FROM sensor_readings WHERE $__timeFilter(time) ORDER BY time",
          "refId": "A",
          "format": "time_series"
        }
      ],
      "title": "Temperature History",
      "type": "timeseries"
    },
    {
      "datasource": { "type": "postgres" },
      "fieldConfig": {
        "defaults": {
          "color": { "mode": "palette-classic" },
          "custom": {
            "drawStyle": "bars",
            "fillOpacity": 80,
            "lineWidth": 1
          },
          "unit": "percent"
        }
      },
      "gridPos": { "h": 8, "w": 24, "x": 0, "y": 14 },
      "id": 6,
      "targets": [
        {
          "rawSql": "SELECT time, blowdown_rate as \"Blowdown\", caustic_rate as \"Caustic\", sulfite_rate as \"Sulfite\", acid_rate as \"Acid\" FROM fuzzy_outputs WHERE $__timeFilter(time) ORDER BY time",
          "refId": "A",
          "format": "time_series"
        }
      ],
      "title": "Fuzzy Logic Outputs",
      "type": "timeseries"
    }
  ],
  "refresh": "10s",
  "schemaVersion": 38,
  "style": "dark",
  "tags": ["boiler"],
  "templating": { "list": [] },
  "time": { "from": "now-6h", "to": "now" },
  "timepicker": {},
  "timezone": "",
  "title": "Boiler Controller - Main",
  "uid": "boiler-main",
  "version": 1
}
EOF
```

### 2. Restart Grafana to Load Dashboard

```bash
docker compose restart grafana
```

### 3. Additional Dashboard Panels

You can add these queries in Grafana's query editor:

**Daily Water Usage:**
```sql
SELECT
  time_bucket('1 day', time) as time,
  MAX(water_meter1) - MIN(water_meter1) as "Daily Usage"
FROM sensor_readings
WHERE $__timeFilter(time)
GROUP BY 1
ORDER BY 1
```

**Pump Runtime Today:**
```sql
SELECT
  pump_name,
  SUM(duration_ms)/1000/60 as "Minutes"
FROM pump_events
WHERE time > CURRENT_DATE
GROUP BY pump_name
```

**Alarm Count by Type:**
```sql
SELECT
  alarm_name,
  COUNT(*) as count
FROM alarm_history
WHERE time > NOW() - INTERVAL '7 days'
  AND active = true
GROUP BY alarm_name
ORDER BY count DESC
```

**Manual Test History:**
```sql
SELECT
  time,
  test_type,
  value
FROM manual_tests
WHERE $__timeFilter(time)
ORDER BY time DESC
```

---

## ESP32 Controller Configuration

### 1. Configure WiFi and Backend

On the ESP32 controller, set these parameters via LCD menu or web interface:

| Setting | Value |
|---------|-------|
| WiFi SSID | Your network name |
| WiFi Password | Your WiFi password |
| TimescaleDB Host | `192.168.1.100` (Pi IP) |
| TimescaleDB Port | `3000` (API server port) |
| API Key | `your-secret-api-key-here` |
| Log Interval | `10000` (10 seconds) |

### 2. Verify Connection

1. Check ESP32 LCD shows "WiFi Connected"
2. Check API server logs:
   ```bash
   docker compose logs -f api-server
   ```
3. Verify data in database:
   ```bash
   docker exec -it timescaledb psql -U boiler -d boiler_data \
     -c "SELECT * FROM sensor_readings ORDER BY time DESC LIMIT 5"
   ```

### 3. Data Logger Code Reference

The ESP32 sends data using this format (from `data_logger.cpp`):

```cpp
// POST to http://<pi-ip>:3000/api/readings
{
  "conductivity": 2500.0,
  "temperature": 42.5,
  "water_meter1": 1247,
  "water_meter2": 0,
  "flow_rate": 1.2,
  "blowdown_active": false,
  "pump1_active": false,
  "pump2_active": true,
  "pump3_active": false,
  "active_alarms": 0
}
```

---

## Maintenance & Backup

### Daily Checks

```bash
# Check all services running
docker compose ps

# Check disk usage
df -h

# Check database size
docker exec -it timescaledb psql -U boiler -d boiler_data \
  -c "SELECT pg_size_pretty(pg_database_size('boiler_data'));"
```

### Backup Database

```bash
# Create backup script
cat > ~/boiler-backend/backup.sh << 'EOF'
#!/bin/bash
BACKUP_DIR=~/boiler-backups
DATE=$(date +%Y%m%d_%H%M%S)
mkdir -p $BACKUP_DIR

# Backup database
docker exec timescaledb pg_dump -U boiler boiler_data | gzip > $BACKUP_DIR/boiler_data_$DATE.sql.gz

# Keep only last 7 days
find $BACKUP_DIR -name "*.sql.gz" -mtime +7 -delete

echo "Backup complete: $BACKUP_DIR/boiler_data_$DATE.sql.gz"
EOF

chmod +x ~/boiler-backend/backup.sh

# Add to crontab (daily at 2 AM)
(crontab -l 2>/dev/null; echo "0 2 * * * ~/boiler-backend/backup.sh") | crontab -
```

### Restore Database

```bash
# Stop API server during restore
docker compose stop api-server

# Restore from backup
gunzip -c ~/boiler-backups/boiler_data_YYYYMMDD_HHMMSS.sql.gz | \
  docker exec -i timescaledb psql -U boiler -d boiler_data

# Restart
docker compose start api-server
```

### Update Services

```bash
cd ~/boiler-backend

# Pull latest images
docker compose pull

# Recreate containers
docker compose up -d
```

### View Logs

```bash
# All services
docker compose logs -f

# Specific service
docker compose logs -f timescaledb
docker compose logs -f api-server
docker compose logs -f grafana
```

---

## Troubleshooting

### Common Issues

| Issue | Cause | Solution |
|-------|-------|----------|
| Can't connect to Grafana | Port blocked | Check `sudo ufw status`, allow port 3001 |
| API returns 401 | Wrong API key | Verify key in ESP32 config |
| No data in Grafana | Time range wrong | Adjust time picker to "Last 1 hour" |
| Database connection failed | Service not ready | Wait 30s after startup, check logs |
| Disk full | Too much data | Check retention policy, clean old data |

### Check Service Health

```bash
# TimescaleDB
docker exec -it timescaledb pg_isready -U boiler

# API Server
curl http://localhost:3000/health

# Grafana
curl http://localhost:3001/api/health
```

### Reset Everything

```bash
cd ~/boiler-backend

# Stop and remove all containers and volumes
docker compose down -v

# Remove data directories
sudo rm -rf ~/boiler-backend/grafana/data

# Start fresh
docker compose up -d
```

### Check ESP32 Connection

On the ESP32 web interface (`http://<esp32-ip>/`), verify:
- WiFi shows "Connected"
- Check if data is being sent (Serial monitor)

From the Pi, check incoming requests:
```bash
docker compose logs -f api-server | grep "POST"
```

---

## Quick Reference

### URLs

| Service | URL |
|---------|-----|
| Grafana | `http://<pi-ip>:3001` |
| API Health | `http://<pi-ip>:3000/health` |
| API Latest | `http://<pi-ip>:3000/api/latest` |

### Default Credentials

| Service | Username | Password |
|---------|----------|----------|
| Grafana | admin | boiler_grafana_2024 |
| TimescaleDB | boiler | boiler_water_2024 |

### Docker Commands

```bash
# Start all
docker compose up -d

# Stop all
docker compose down

# Restart service
docker compose restart <service>

# View logs
docker compose logs -f

# Shell into container
docker exec -it timescaledb bash
docker exec -it timescaledb psql -U boiler -d boiler_data
```

---

*Document Revision: 1.0*
*Last Updated: December 2024*
