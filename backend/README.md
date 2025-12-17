# Boiler Dosing Controller - Raspberry Pi Backend

Complete backend setup for ESP32 boiler controller data logging with TimescaleDB and Grafana.

## Architecture

```
┌─────────────┐     WiFi      ┌─────────────────────────────────────┐
│    ESP32    │──────────────▶│         Raspberry Pi                │
│  Controller │   HTTP POST   │  ┌─────────┐  ┌────────────────┐   │
└─────────────┘               │  │   API   │──│  TimescaleDB   │   │
                              │  │  :3000  │  │     :5432      │   │
                              │  └─────────┘  └────────────────┘   │
                              │       │              │              │
                              │       └──────┬───────┘              │
                              │              ▼                      │
                              │        ┌──────────┐                 │
                              │        │ Grafana  │                 │
                              │        │  :3001   │                 │
                              └────────┴──────────┴─────────────────┘
```

## Requirements

- Raspberry Pi 4 (4GB+ RAM recommended) or Pi 5
- 32GB+ microSD card (64GB recommended for long-term storage)
- Raspberry Pi OS (64-bit) - Bookworm or newer
- Network connection (same network as ESP32)

## Quick Start

### 1. Install Docker on Raspberry Pi

```bash
# Update system
sudo apt update && sudo apt upgrade -y

# Install Docker using convenience script
curl -fsSL https://get.docker.com -o get-docker.sh
sudo sh get-docker.sh

# Add your user to docker group (logout/login required)
sudo usermod -aG docker $USER

# Install Docker Compose plugin
sudo apt install docker-compose-plugin -y

# Verify installation
docker --version
docker compose version
```

### 2. Clone and Configure

```bash
# Clone repository (or copy backend folder)
git clone <your-repo-url>
cd Boiler_Dosing_system/backend

# Create environment file
cp .env.example .env

# Edit with your secure passwords
nano .env
```

**Important:** Change these values in `.env`:
- `DB_PASSWORD` - Strong database password
- `API_KEY` - Match this in ESP32 firmware
- `GRAFANA_PASSWORD` - Grafana admin login

### 3. Start Services

```bash
# Start all services (first run downloads images ~1GB)
docker compose up -d

# Check status
docker compose ps

# View logs
docker compose logs -f
```

### 4. Verify Installation

```bash
# Check API health
curl http://localhost:3000/health

# Expected response:
# {"status":"healthy","timestamp":"2024-..."}
```

### 5. Access Grafana

1. Open browser: `http://<raspberry-pi-ip>:3001`
2. Login: `admin` / `<your GRAFANA_PASSWORD>`
3. Dashboard "Boiler Dosing Controller" is pre-configured

## ESP32 Configuration

Update these values in ESP32 firmware `config.h`:

```cpp
// WiFi credentials
#define WIFI_SSID     "your_network"
#define WIFI_PASSWORD "your_password"

// API server (Raspberry Pi IP)
#define API_HOST      "192.168.1.xxx"  // Your Pi's IP
#define API_PORT      3000
#define API_KEY       "your_api_key"   // Match .env file
```

## API Endpoints

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/health` | Health check (no auth) |
| POST | `/api/readings` | Submit sensor readings |
| POST | `/api/readings/batch` | Batch submit (offline buffer) |
| GET | `/api/readings/latest` | Get latest reading |
| GET | `/api/readings?start=&end=` | Query time range |
| POST | `/api/events/pump` | Log pump event |
| POST | `/api/events/blowdown` | Log blowdown event |
| POST | `/api/alarms` | Log alarm |
| GET | `/api/alarms/active` | Get active alarms |
| POST | `/api/calibration` | Log calibration |
| POST | `/api/config` | Log config change |
| POST | `/api/system/status` | System health data |
| GET | `/api/stats/today` | Today's statistics |
| GET | `/api/stats/hourly` | Hourly aggregates |
| GET | `/api/stats/daily` | Daily aggregates |

All POST endpoints require `X-API-Key` header.

## Data Retention

Automatic data retention policies:
- **Sensor readings:** 90 days (detailed)
- **Pump/Blowdown events:** 1 year
- **Alarms:** 2 years
- **Calibration/Config:** Forever

Hourly and daily aggregates are kept indefinitely.

## Maintenance

### Backup Database

```bash
# Create backup
docker exec boiler_timescaledb pg_dump -U boiler boiler_data > backup_$(date +%Y%m%d).sql

# Restore from backup
cat backup_20240115.sql | docker exec -i boiler_timescaledb psql -U boiler boiler_data
```

### Update Services

```bash
cd ~/Boiler_Dosing_system/backend

# Pull latest images
docker compose pull

# Restart with new images
docker compose up -d
```

### View Logs

```bash
# All services
docker compose logs -f

# Specific service
docker compose logs -f api
docker compose logs -f timescaledb
docker compose logs -f grafana
```

### Database Shell

```bash
docker exec -it boiler_timescaledb psql -U boiler boiler_data

# Example queries:
SELECT * FROM sensor_readings ORDER BY time DESC LIMIT 10;
SELECT * FROM active_alarms;
SELECT * FROM today_stats;
\q
```

## Troubleshooting

### ESP32 Can't Connect

1. Check Pi's IP address: `hostname -I`
2. Verify API is running: `curl http://localhost:3000/health`
3. Check firewall: `sudo ufw status`
4. Test from ESP32 network: `curl http://<pi-ip>:3000/health`

### Database Not Starting

```bash
# Check logs
docker compose logs timescaledb

# Common fix - permissions
sudo chown -R 999:999 /var/lib/docker/volumes/backend_timescale_data
```

### Out of Disk Space

```bash
# Check disk usage
df -h

# Clean old Docker data
docker system prune -a

# Force retention policy run
docker exec boiler_timescaledb psql -U boiler -c "CALL run_job((SELECT job_id FROM timescaledb_information.jobs WHERE proc_name = 'policy_retention'));"
```

### Reset Everything

```bash
# Stop and remove containers + volumes
docker compose down -v

# Restart fresh
docker compose up -d
```

## Performance Tuning (Optional)

For Raspberry Pi 4 with 4GB RAM, add to `docker-compose.yml`:

```yaml
services:
  timescaledb:
    # ... existing config ...
    command: >
      -c shared_buffers=512MB
      -c effective_cache_size=1GB
      -c work_mem=32MB
      -c maintenance_work_mem=128MB
      -c max_connections=50
```

## Static IP (Recommended)

Set a static IP so ESP32 always finds the server:

```bash
sudo nano /etc/dhcpcd.conf

# Add at bottom:
interface eth0
static ip_address=192.168.1.100/24
static routers=192.168.1.1
static domain_name_servers=192.168.1.1 8.8.8.8
```

## Auto-Start on Boot

Docker services auto-start by default with `restart: unless-stopped`.

To manually control:
```bash
# Disable auto-start
docker compose down

# Enable auto-start
docker compose up -d
```
