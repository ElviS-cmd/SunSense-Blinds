# SunSense Blinds

SunSense Blinds is an ESP32-S3 smart blinds controller with local light automation, button control, servo slat tilt, BLE onboarding, Wi-Fi, MQTT, and Home Assistant discovery.

## Current Runtime

The firmware currently runs these subsystems:

- Button input for local AUTO/MANUAL and motion control
- L298N-style DC motor direction control for blind travel
- Time-based blind position tracking with NVS recovery
- LDR ambient light sensing with hysteresis
- MG996R-style servo output for slat tilt
- Two-LED status patterns
- INMP441 microphone input over I2S
- Local utterance-count voice command prototype
- BLE provisioning for Wi-Fi and MQTT setup
- Wi-Fi station mode after provisioning
- MQTT command handling and state publishing
- Home Assistant MQTT discovery

The AS5600 encoder component and GPIO/I2C definitions are still present in the tree, but the main runtime does not currently initialize or use the encoder. Blind travel is estimated from motor run time and persisted in NVS.

## Hardware Pin Map

From `include/gpio_config.h`:

| Function | GPIO |
|---|---:|
| Button input | 3 |
| Green LED | 5 |
| Blue LED | 6 |
| INMP441 I2S clock | 2 |
| INMP441 I2S word select | 4 |
| INMP441 I2S data in | 7 |
| Optional AS5600 I2C SDA | 8 |
| Optional AS5600 I2C SCL | 9 |
| Servo PWM | 10 |
| LDR analog input | 1 |
| Motor IN1 | 12 |
| Motor IN2 | 13 |

## Hardware Stack

- ESP32-S3 board
- Pushbutton or button module
- Two LEDs with current-limiting resistors
- LDR analog light sensor
- DC motor with direction driver
- Servo motor for slat tilt
- INMP441 I2S microphone
- Optional AS5600 magnetic encoder hardware, currently disabled in runtime

## Behavior

### AUTO Mode

AUTO mode follows the LDR light level:

- Bright light opens the blinds and moves slats toward the configured open angle.
- Dark light closes the blinds and moves slats toward the configured closed angle.
- LDR readings use a moving average and hysteresis. Lower ADC values mean brighter light.
- After boot, automatic movement waits briefly so restored state and sensors can settle.

### MANUAL Mode

Manual actions come from the button, MQTT, or voice commands:

- A short button press in AUTO switches to MANUAL.
- Short button presses in MANUAL alternate open and close.
- A short button press while the motor is running stops the motor.
- A long button press returns to AUTO, stops motion, and reapplies light-driven slat behavior.
- MQTT cover and position commands enter MANUAL before moving.
- MQTT slat commands enter MANUAL and command the servo directly.

### Motor and Position Tracking

The motor controller drives two direction pins:

- Opening: `IN1=HIGH`, `IN2=LOW`
- Closing: `IN1=LOW`, `IN2=HIGH`
- Stop: `IN1=LOW`, `IN2=LOW`

Position is estimated from elapsed motor run time. The default full travel time is `120000 ms`. Runtime state is saved to NVS after meaningful position changes, at timed intervals while moving, and after stop/calibration events.

Saved runtime state includes:

- whether position is valid
- blind position percent
- AUTO/MANUAL mode
- last light level
- last slat angle

If no saved state exists, the firmware starts from a conservative closed-position assumption.

### Servo Slat Control

The slat servo uses ESP-IDF LEDC PWM:

- GPIO: `10`
- Frequency: `50 Hz`
- Resolution: `12 bit`
- Pulse range: `1000 us` to `2000 us`
- Startup angle: `90 degrees`
- Closed slat angle: `60 degrees`
- Open slat angle: `120 degrees`
- Ramp duration: `3000 ms`
- Ramp sample period: `40 ms`
- Settle time: `400 ms`

The servo controller tracks commanded position only; the MG996R does not provide feedback. Servo movement is blocked when the blinds are effectively fully rolled up, currently at `95%` or higher, so the slats are not driven when the linkage should not tilt.

### Servo-Only Test Mode

For isolated servo testing, set this in `include/gpio_config.h`:

```c
#define SUNSENSE_SERVO_TEST_ONLY 1
```

The default safe test sequence is:

```text
90 -> 60 -> 90 -> 120
```

Set `SUNSENSE_SERVO_TEST_FULL_RANGE` to `1` only when the linkage is disconnected or full travel is safe. That test uses:

```text
0 -> 90 -> 180 -> 90
```

After testing, restore:

```c
#define SUNSENSE_SERVO_TEST_ONLY 0
```

### LEDs

| Pattern | Meaning |
|---|---|
| Both solid on | Connected and ready |
| Both off | Offline |
| Green slow blink | Opening |
| Blue slow blink | Closing |
| Both slow blink together | BLE provisioning |
| Both fast blink together | Calibration pattern |
| Alternating slow blink | Wi-Fi or MQTT reconnecting |
| Alternating fast blink | Fault |

Network status updates do not overwrite an active opening or closing indication while the motor is moving.

### Voice Commands

Voice control is a simple local utterance detector, not speech recognition. The INMP441 audio level is converted to RMS, and loud bursts above the configured threshold are counted.

| Utterance count | Command |
|---:|---|
| 1 | Open |
| 2 | Close |
| 3 | Stop |
| 4 | Return to AUTO |

Voice commands use the same locked control paths as button and MQTT commands.

## Connectivity

Provisioning uses ESP-IDF BLE provisioning. Normal operation uses Wi-Fi and MQTT.

On first boot, or after `REPROVISION`, the device starts BLE provisioning and logs:

```text
Provision with BLE service name: SunSense-...
Use setup code: ...
Send MQTT config to mqtt-config using lines: uri=..., username=..., password=...
```

The provisioning client sends Wi-Fi credentials through the ESP-IDF provisioning flow and sends MQTT settings to the custom `mqtt-config` endpoint. MQTT URI, username, password, device ID, and setup code are stored in device NVS, not hardcoded in source.

## MQTT

Runtime topics use:

```text
sunsense/<device_id>/...
```

Command topics:

- `sunsense/<device_id>/cmd/cover`
- `sunsense/<device_id>/cmd/mode`
- `sunsense/<device_id>/cmd/position`
- `sunsense/<device_id>/cmd/slat`
- `sunsense/<device_id>/cmd/system`

State topics:

- `sunsense/<device_id>/state/cover`
- `sunsense/<device_id>/state/mode`
- `sunsense/<device_id>/state/position`
- `sunsense/<device_id>/state/light/raw`
- `sunsense/<device_id>/state/light/filtered`
- `sunsense/<device_id>/state/light/state`
- `sunsense/<device_id>/state/motor`
- `sunsense/<device_id>/state/slat`
- `sunsense/<device_id>/state/slat/position`
- `sunsense/<device_id>/state/health`
- `sunsense/<device_id>/state/network/rssi`
- `sunsense/<device_id>/state/network/online`

Supported command payloads:

| Topic suffix | Payloads |
|---|---|
| `cmd/cover` | `OPEN`, `CLOSE`, `STOP` |
| `cmd/mode` | `AUTO`, `MANUAL` |
| `cmd/position` | `0` through `100` |
| `cmd/slat` | `OPEN`, `CLOSE`, `STOP`, or `0` through `100` |
| `cmd/system` | `GO_HOME`, `REPROVISION`, `RESET_HA`, `SET_CLOSED`, `SET_OPEN` |

`SET_CLOSED` and `SET_OPEN` calibrate the current estimated travel position to `0%` or `100%`.

## Home Assistant

The firmware publishes retained MQTT discovery messages under:

```text
homeassistant/...
```

Home Assistant receives:

- one cover entity with position and slat tilt controls
- one AUTO/MANUAL mode selector
- sensors for position, raw light, filtered light, light state, motor state, slat state, slat position, health, and Wi-Fi RSSI

Availability is retained at:

```text
sunsense/<device_id>/state/network/online
```

Payloads are:

```text
online
offline
```

## Provisioning From This Repo

Example:

```bash
source env.sh
python tools/provision_sunsense.py \
  --service-name SunSense-DEVICE_ID_FROM_SERIAL \
  --pop SETUP_CODE_FROM_SERIAL \
  --ssid "YOUR_WIFI_SSID" \
  --passphrase "YOUR_WIFI_PASSWORD" \
  --mqtt-uri mqtt://HOME_ASSISTANT_IP:1883 \
  --mqtt-username sunsense \
  --mqtt-password "YOUR_MQTT_PASSWORD"
```

For hidden password prompts:

```bash
source env.sh
python tools/provision_interactive.py \
  --service-name SunSense-DEVICE_ID_FROM_SERIAL \
  --pop SETUP_CODE_FROM_SERIAL \
  --ssid "YOUR_WIFI_SSID"
```

## Build Requirements

- ESP-IDF 5.x
- Python environment required by ESP-IDF
- ESP32-S3 toolchain

## Build and Flash

From the project root:

```bash
export IDF_PATH=/path/to/esp-idf
source env.sh
idf.py build
idf.py flash
idf.py monitor
```

If your board uses a specific serial device:

```bash
idf.py -p /dev/ttyUSB0 flash monitor
```

## Project Structure

- `main/`: application orchestration, network setup, MQTT handlers
- `components/`: reusable controllers for hardware and runtime subsystems
- `include/`: shared GPIO configuration and system types
- `tools/`: BLE provisioning helper scripts
- `docs/`: project reference documents

## Notes

- The application entrypoint is `main/main.cpp`.
- GPIO assignments and timing constants live in `include/gpio_config.h`.
- Wi-Fi credentials are managed by ESP-IDF provisioning storage.
- SunSense MQTT settings and setup data are stored in NVS.
- The firmware derives a stable default device ID from the ESP32 MAC suffix.
- No Wi-Fi password, MQTT password, broker address, setup code, or device-specific ID should be committed to source.
