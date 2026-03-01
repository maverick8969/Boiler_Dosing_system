# Coprocessor Communication Logic

This document defines **what data is transmitted and when** on the RS-485 link between the main ESP32 (control box) and the boiler-panel coprocessor (ESP32 DevKit), including sequences for sensor data, confirmations, blowdown requests, and final readings.

---

## 1. Link and topology

- **Physical**: Half-duplex RS-485; one twisted pair (A/B) + GND over ~10 ft.
- **Framing**: Binary frame = SYNC(0xAA55) + TYPE(1) + LEN(1) + PAYLOAD(LEN) + CRC16(2). See `coprocessor_protocol.h`.
- **Turn-around**: Only one node drives the bus at a time. After sending, the transmitter releases DE; wait 1–2 character times before listening for a reply.

---

## 2. Normal flow: telemetry and commands

### 2.1 C3 → Main: Telemetry (periodic)

- **When**: C3 sends **telemetry** at a fixed rate (e.g. **2–10 Hz**, configurable).
- **Content**: Conductivity (uS/cm), temperature (°C), blowdown state, valve open/closed, valve feedback (mA), solenoid on/off, sensor/valve health flags, sequence number.
- **Purpose**: Main uses this as the only source of conductivity and temperature when the coprocessor is present; it runs fuzzy logic, blowdown decisions, and alarms from this data. The **Atlas EZO-EC** is on the boiler panel; the coprocessor reads it over Serial1 (GPIO9 RX, GPIO10 TX, 9600 baud) and includes the reading in telemetry.
- **No explicit “confirmation”** from main for telemetry: main does not ACK each telemetry frame. Sequence numbers allow main to detect gaps or duplicates.

### 2.2 Main → C3: Commands (on event)

- **When**: On user action or control logic (e.g. open/close blowdown, solenoid on/off, sample request, time sync).
- **Types**: `CMD_BLOWDOWN_OPEN`, `CMD_BLOWDOWN_CLOSE`, `CMD_SOLENOID`, `CMD_SAMPLE_REQUEST`, `CMD_CONFIG` (optional), `TIME_SYNC`.
- **Payload**: Each command includes a **sequence number** so C3 can match the corresponding ACK/NAK.

### 2.3 C3 → Main: Confirmation (ACK/NAK)

- **When**: After C3 receives a **command** frame, it responds with **ACK** (accepted) or **NAK** (rejected).
- **Content**: `ack_sequence` = command’s sequence, `result` = 0 for ACK or NAK error code (busy, invalid state, valve fault, etc.).
- **Timing**: C3 sends ACK/NAK as soon as the command is processed (within one turn-around). Main waits for this reply before considering the command complete; if no reply within timeout, main may retry (e.g. up to 3x with backoff).

---

## 3. Blowdown sequence (request → confirmation → final reading)

Typical sequence when main requests a blowdown cycle:

1. **Main** decides to open blowdown (e.g. from fuzzy/setpoint or manual).
2. **Main → C3**: Sends **CMD_BLOWDOWN_OPEN** with sequence N.
3. **C3**: Receives command, starts opening valve (relay + state machine), sends **ACK** with sequence N.
4. **Main**: Receives ACK; can show “Blowdown opening” and wait for telemetry indicating valve open.
5. **C3**: Continues sending **telemetry** at 2–10 Hz; `blowdown_state` and `valve_open` / `valve_feedback_mA` reflect progress. When valve is fully open, telemetry shows valve open and blowdown state “blowing down”.
6. **Main**: Runs control loop using telemetry (conductivity, temp, blowdown state). When logic decides to close:
7. **Main → C3**: Sends **CMD_BLOWDOWN_CLOSE** with sequence N+1.
8. **C3**: Closes valve, sends **ACK** with sequence N+1.
9. **C3**: Telemetry continues; when valve is fully closed, **final sensor reading** is just the next telemetry frame(s) with updated conductivity/temp and blowdown state = idle. No separate “final reading” message type unless we add one later; the **next telemetry after close** is the “final” reading for logging.
10. **Main**: Logs or uses that telemetry as the post-blowdown reading.

**Summary**: Sensor data to main = **periodic telemetry**. Blowdown request = **command (open/close)**. Confirmation = **ACK/NAK**. Final sensor reading = **next telemetry** after close (or any telemetry during/after blowdown).

---

## 4. Event and error reporting (C3 → Main)

- **Event** (on change): C3 sends **EVENT** when something notable happens (e.g. alarm, valve timeout, limit/feedback fault). Payload: event_code (e.g. alarm bitmask), severity, timestamp.
- **Error** (on detection): C3 sends **ERROR** on protocol or internal fault (CRC/sequence error, C3 internal error). Payload: error_code, timestamp.
- Main may log events/errors and set local alarms; no ACK required for EVENT/ERROR.

---

## 5. Time sync (optional)

- **When**: Main sends **TIME_SYNC** periodically (e.g. 1/min) or on request. Payload: Unix time (sec + subsec ms).
- **Purpose**: C3 can timestamp events and telemetry for alignment with main/logging.

---

## 6. Update rates and timeouts (summary)

| Item                 | Rate / trigger        | Direction   |
|----------------------|----------------------|-------------|
| Telemetry            | 2–10 Hz              | C3 → Main   |
| Command (blowdown, etc.) | On event         | Main → C3   |
| ACK/NAK              | Per command          | C3 → Main   |
| Event                 | On change            | C3 → Main   |
| Error                 | On detection         | C3 → Main   |
| Time sync             | 1/min or on request  | Main → C3   |

- **Main**: If no telemetry received for N seconds (e.g. 5 s), treat as comms lost → safe mode.
- **C3**: If no valid frame from main for M seconds (e.g. 3 s), set `comms_lost` in telemetry and enter local fail-safe (e.g. close blowdown, solenoid off).
- **Command reply**: Main waits for ACK/NAK with timeout (e.g. 100–500 ms); retry up to 3x with backoff if no reply.

---

## 7. Half-duplex timing

- **Main sends command**: Assert DE, send frame, flush UART, release DE, wait **1–2 character times** (e.g. ~2 ms at 115200), then read for ACK/NAK until timeout.
- **C3 receives command**: In RX mode (DE low). On valid frame, process; then assert DE, send ACK/NAK, release DE.
- **C3 sends telemetry**: Between commands, C3 is the only one sending periodically; main listens. So main must **not** hold DE except when sending a command. C3 asserts DE only for the duration of each telemetry (or ACK/NAK) transmission.

This document should be read together with `include/coprocessor_protocol.h` for payload layouts and message type values.
