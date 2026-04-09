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
- `sunsense/<device_id>/state/health`
- `sunsense/<device_id>/state/network/rssi`
- `sunsense/<device_id>/state/network/online`

Supported command payloads currently include:

- cover: `OPEN`, `CLOSE`, `STOP`
- mode: `AUTO`, `MANUAL`
- system: `GO_HOME`, `REPROVISION`

## Home Assistant Integration

The firmware is designed to map cleanly into a Home Assistant MQTT setup with:

- one main cover entity for the blinds
- one mode selector for `AUTO` and `MANUAL`
- supporting light, motor, slat, health, and network sensors

The Linux-side Home Assistant and Mosquitto setup used during development lives outside this repository. This repository contains the device-side firmware integration only.

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
- BLE provisioning bootstrap
- MQTT runtime connectivity
- Home Assistant-oriented MQTT topic structure

The next integration step is end-to-end hardware validation of the BLE provisioning flow and the client-side path that writes the `mqtt-config` endpoint during onboarding.
