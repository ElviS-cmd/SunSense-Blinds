# SunSense Blinds

SunSense Blinds is an ESP32-S3-based smart blinds controller with local automation, BLE onboarding, Wi-Fi connectivity, MQTT integration, and a Home Assistant-oriented control model.

## Overview

The firmware coordinates:

- Button input for user control
- Automatic and manual operating modes
- DC motor control for blind travel
- LED status indication
- LDR-based ambient light sensing
- AS5600 magnetic encoder feedback
- Servo output for slat tilt
- INMP441 microphone input over I2S
- BLE provisioning for first-time setup
- Wi-Fi station mode after provisioning
- MQTT state publishing and command handling
- NVS-backed persistence for network and device setup data

## Connectivity Model

The current firmware is structured around the production connectivity path:

- BLE is used for provisioning
- Wi-Fi is used for normal operation
- MQTT is the runtime control and telemetry transport
- Home Assistant is the intended primary user interface

On first boot or after reprovisioning, the device starts BLE provisioning. After Wi-Fi and MQTT settings are supplied, it reboots into normal runtime and connects to the configured MQTT broker.

## Project Structure

- `main/` application entrypoint and task orchestration
- `components/` modular controllers for each hardware subsystem
- `include/` shared project headers and GPIO configuration

Notable controller components:

- `led_controller`: two-LED product status patterns
- `servo_controller`: deterministic MG996R positional servo PWM control
- `voice_command_controller`: local utterance-count voice command prototype

## Hardware Pin Map

From `include/gpio_config.h`:

- Button signal: `GPIO 3`
- Green LED: `GPIO 5`
- Blue LED: `GPIO 6`
- I2S clock: `GPIO 2`
- I2S word select: `GPIO 4`
- I2S data in: `GPIO 7`
- I2C SDA: `GPIO 8`
- I2C SCL: `GPIO 9`
- Servo PWM: `GPIO 10`
- LDR analog output: `GPIO 1`
- Motor IN1: `GPIO 12`
- Motor IN2: `GPIO 13`

## Components

- ESP32-S3 board
- LDR sensor module with analog output
- Pushbutton or button module
- 2 LEDs with current-limiting resistors
- AS5600 encoder module
- Servo motor
- DC motor with driver module
- INMP441 microphone module

## LED Status Patterns

The two LEDs communicate product state:

| Pattern | Meaning |
|---|---|
| Both solid on | Normal, connected, and ready |
| Both off | Not connected or offline |
| Green slow blink | Opening blinds |
| Blue slow blink | Closing blinds |
| Both slow blink together | Pairing or setup mode |
| Both fast blink together | Calibration pattern available |
| Alternating slow blink | Reconnecting or searching for controller |
| Alternating fast blink | Fault, jam, obstruction, or motor/system error |

LED patterns are event-driven from network, MQTT, motor, and fault transitions. Network status updates do not overwrite an active opening or closing indication while the motor is moving.

## Servo Control

The slat servo is treated as a standard MG996R positional servo.

Servo PWM settings are defined in `include/gpio_config.h`:

- GPIO: `GPIO 10`
- Frequency: `50 Hz`
- Resolution: `LEDC_TIMER_12_BIT`
- Pulse range: `1000 us` to `2000 us`
- Closed slat angle: `0 degrees`
- Open slat angle: `90 degrees`

The servo controller applies PWM deterministically and tracks commanded state only. It does not estimate physical intermediate position because the MG996R provides no position feedback. After each command, the controller waits for a conservative software settle time before allowing dependent sequencing, such as starting the roll motor.

Repeated servo commands are intentional:

- `servo_move_to()` reapplies PWM even if the target is unchanged.
- `servo_move_to_ex(..., force_reapply=false)` can skip unchanged targets explicitly.

Useful servo debug logs include commanded angle, computed duty, command count, skipped-command reason, and settled state.

## Servo-Only Test Mode

A compile-time servo-only test path is available for validating ESP-IDF LEDC output before running the full system.

In `include/gpio_config.h`, set:

```c
#define SUNSENSE_SERVO_TEST_ONLY 1
```

Then build and flash normally:

```bash
idf.py build
idf.py flash monitor
```

This bypasses LDR, motor, encoder, MQTT, Wi-Fi, mode switching, button handling, LEDs, and microphone processing. It repeatedly commands the servo through:

```text
0 -> 90 -> 180 -> 90
```

After testing, restore:

```c
#define SUNSENSE_SERVO_TEST_ONLY 0
```

## Voice Commands

The current local voice command implementation is a first-pass utterance detector, not semantic speech recognition. The INMP441 microphone captures audio level data, and `voice_command_controller` converts loud utterance bursts into commands.

Current mapping:

| Utterances above threshold | Command |
|---|---|
| 1 | Open |
| 2 | Close |
| 3 | Stop |
| 4 | Return to AUTO |

Voice commands use the same locked control path as button and MQTT commands:

- Open enters manual mode and starts the open sequence.
- Close enters manual mode and starts the close sequence.
- Stop clears pending motion and stops the motor.
- Return to AUTO restores automatic mode and applies light-driven servo behavior.

The default RMS threshold comes from `system_config.audio_threshold`. Timing constants for utterance duration, quiet gap, command window, and cooldown are defined in `include/gpio_config.h`.

For true spoken phrases like "open blinds" or "close blinds", a real keyword or speech recognition engine should replace the current utterance-count recognizer.

## Provisioning

Provisioning is implemented with ESP-IDF BLE provisioning:

- The device advertises a BLE provisioning service named `SunSense-<device_id>`
- A unique setup code is generated on first boot and stored in NVS
- Wi-Fi credentials are provisioned through the ESP-IDF provisioning flow
- The MQTT broker URI is provisioned through a custom endpoint named `mqtt-config`

The device logs the following over serial during provisioning:

- BLE service name
- setup code
- custom endpoint name for MQTT configuration

The current firmware expects the provisioning client to send the MQTT broker URI to `mqtt-config` before provisioning completes.

## MQTT Integration

After provisioning, the firmware connects to the configured MQTT broker and uses a topic layout based on:

```text
sunsense/<device_id>/...
```

Current command topics:

- `sunsense/<device_id>/cmd/cover`
- `sunsense/<device_id>/cmd/mode`
- `sunsense/<device_id>/cmd/position`
- `sunsense/<device_id>/cmd/slat`
- `sunsense/<device_id>/cmd/system`

Current state topics:

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

Supported command payloads currently include:

- cover: `OPEN`, `CLOSE`, `STOP`
- mode: `AUTO`, `MANUAL`
- position: `0` through `100`
- slat: `OPEN`, `CLOSE`, `STOP`, or `0` through `100`
- system: `GO_HOME`, `REPROVISION`

## Home Assistant Integration

The firmware publishes Home Assistant MQTT discovery messages after it connects to
the configured broker. With Home Assistant MQTT discovery enabled, the device is
created automatically with:

- one main cover entity for the blinds, including slat tilt controls
- one mode selector for `AUTO` and `MANUAL`
- supporting light, motor, slat, health, and network sensors

Slat tilt is exposed on the main Home Assistant cover entity. Home Assistant
sends slat commands to `sunsense/<device_id>/cmd/slat`; `0` means closed and
`100` means open. The servo maps that range onto the configured slat angles
from `SERVO_SLAT_CLOSED_ANGLE` to `SERVO_SLAT_OPEN_ANGLE`.

The discovery prefix is the Home Assistant default:

```text
homeassistant/...
```

The runtime device topics still use:

```text
sunsense/<device_id>/...
```

The online availability topic is retained at:

```text
sunsense/<device_id>/state/network/online
```

Availability payloads are retained as:

```text
online
offline
```

### Home Assistant setup

1. Install and start the Mosquitto broker add-on in Home Assistant.
2. Add a Home Assistant MQTT user, for example `sunsense`.
3. In Home Assistant, add the MQTT integration and enable discovery.
4. Flash the firmware to the ESP32-S3.
5. Watch the serial monitor for:

```text
Provision with BLE service name: SunSense-...
Use setup code: ...
```

6. Provision the device over BLE from this repository:

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

After provisioning completes, the ESP32 restarts, connects to Wi-Fi and MQTT,
publishes discovery, and appears in Home Assistant under the MQTT integration.

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

## Firmware Notes

- The application entrypoint is in `main/main.cpp`.
- Controllers are implemented as separate ESP-IDF components.
- GPIO assignments and task timing live in `include/gpio_config.h`.
- Wi-Fi credentials are managed by ESP-IDF provisioning storage.
- SunSense-specific network metadata such as the MQTT broker URI and setup code are stored in NVS.
- The firmware derives a stable device ID from the device MAC if one is not already stored.

## Current Status

The current firmware builds successfully with `idf.py build` and currently includes:

- controller initialization for the main SunSense hardware stack
- AUTO and MANUAL mode handling
- encoder-based position reporting
- deterministic MG996R servo control with isolated servo-only test mode
- event-driven two-LED status patterns
- first-pass local utterance-count voice commands
- BLE provisioning bootstrap
- MQTT runtime connectivity
- Home Assistant-oriented MQTT topic structure

The next integration step is layered hardware validation:

1. Run `SUNSENSE_SERVO_TEST_ONLY=1` and confirm ESP-IDF servo motion.
2. Restore normal firmware and validate button/MQTT open and close sequences.
3. Tune microphone threshold using serial audio-level logs.
4. Validate utterance-count voice commands in a quiet room.
5. Run end-to-end BLE provisioning and MQTT onboarding.
