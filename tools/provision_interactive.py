#!/usr/bin/env python3
import argparse
import asyncio
import getpass
from types import SimpleNamespace

from provision_sunsense import provision


def required_prompt(label):
    value = input(f"{label}: ").strip()
    if not value:
        raise SystemExit(f"{label} is required")
    return value


def required_secret(label):
    value = getpass.getpass(f"{label}: ")
    if not value:
        raise SystemExit(f"{label} is required")
    return value


def main():
    parser = argparse.ArgumentParser(description="Provision SunSense with hidden credential prompts.")
    parser.add_argument("--service-name", required=True)
    parser.add_argument("--pop", required=True)
    parser.add_argument("--ssid", required=True)
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args()

    ha_host = required_prompt("Home Assistant IP or hostname")
    mqtt_username = required_prompt("MQTT username")
    mqtt_password = required_secret("MQTT password")
    wifi_password = required_secret(f"Wi-Fi password for {args.ssid}")

    provision_args = SimpleNamespace(
        service_name=args.service_name,
        pop=args.pop,
        ssid=args.ssid,
        passphrase=wifi_password,
        mqtt_uri=f"mqtt://{ha_host}:1883",
        mqtt_username=mqtt_username,
        mqtt_password=mqtt_password,
        verbose=args.verbose,
    )
    asyncio.run(provision(provision_args))


if __name__ == "__main__":
    main()
