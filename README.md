# SunSense Blinds

SunSense Blinds is an ESP32-S3-based embedded project for automated blinds control using FreeRTOS tasks and modular hardware controllers.

## Overview

The firmware coordinates:

- Button input for user control
- Automatic and manual operating modes
- DC motor control for blinds movement
- LED status indication
- LDR-based ambient light sensing
- AS5600 magnetic encoder feedback
- Servo output control
- INMP441 microphone input over I2S

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

## Build Requirements

- ESP-IDF 5.x
- Python environment required by ESP-IDF
- ESP32-S3 toolchain

## Build and Flash

From the project root:

```bash
source ~/esp/esp-idf/export.sh
idf.py build
idf.py flash
idf.py monitor
```

If your board uses a specific serial device:

```bash
idf.py -p /dev/cu.usbmodem101 flash monitor
```

## Firmware Notes

- The application entrypoint is in `main/main.cpp`.
- Controllers are implemented as separate ESP-IDF components.
- GPIO assignments and task timing live in `include/gpio_config.h`.

## Current Status

The current firmware builds successfully with `idf.py build` and initializes the main controller set for integrated hardware testing on ESP32-S3.
