#!/usr/bin/env python3
import argparse
import asyncio
import os
import sys


def load_esp_prov():
    idf_path = os.environ.get("IDF_PATH")
    if not idf_path:
        raise RuntimeError("IDF_PATH is not set. Run: source ~/esp/esp-idf/export.sh")

    sys.path.insert(0, os.path.join(idf_path, "components", "protocomm", "python"))
    sys.path.insert(1, os.path.join(idf_path, "tools", "esp_prov"))

    import esp_prov  # type: ignore

    return esp_prov


async def provision(args):
    esp_prov = load_esp_prov()

    transport = await esp_prov.get_transport("ble", args.service_name)
    if transport is None:
        raise RuntimeError("Failed to connect to BLE provisioning service")

    try:
        sec = esp_prov.get_security(1, 0, None, None, args.pop, args.verbose)
        if sec is None:
            raise RuntimeError("Failed to initialize provisioning security")

        # ESP-IDF 5.4 devices currently report protocomm provisioning version v1.1.
        if not await esp_prov.version_match(transport, "v1.1", args.verbose):
            raise RuntimeError("Provisioning protocol version mismatch")

        if not await esp_prov.establish_session(transport, sec):
            raise RuntimeError("Failed to establish secure provisioning session")

        mqtt_payload = f"uri={args.mqtt_uri}\nusername={args.mqtt_username}\npassword={args.mqtt_password}"
        mqtt_message = esp_prov.prov.custom_data_request(sec, mqtt_payload)
        mqtt_response = await transport.send_data("mqtt-config", mqtt_message)
        esp_prov.prov.custom_data_response(sec, mqtt_response)

        if not await esp_prov.send_wifi_config(transport, sec, args.ssid, args.passphrase):
            raise RuntimeError("Failed to send Wi-Fi credentials")

        if not await esp_prov.apply_wifi_config(transport, sec):
            raise RuntimeError("Failed to apply Wi-Fi configuration")

        if not await esp_prov.wait_wifi_connected(transport, sec):
            raise RuntimeError("Device failed to connect to Wi-Fi")

        print("\nProvisioning completed successfully.")
        print(f"Device: {args.service_name}")
        print(f"Wi-Fi SSID: {args.ssid}")
        print(f"MQTT URI: {args.mqtt_uri}")
    finally:
        await transport.disconnect()


def main():
    parser = argparse.ArgumentParser(description="Provision SunSense over BLE with Wi-Fi and MQTT settings.")
    parser.add_argument("--service-name", required=True, help="BLE provisioning service name, e.g. SunSense-70F8")
    parser.add_argument("--pop", required=True, help="Proof of possession string")
    parser.add_argument("--ssid", required=True, help="Wi-Fi SSID")
    parser.add_argument("--passphrase", required=True, help="Wi-Fi password")
    parser.add_argument("--mqtt-uri", required=True, help="MQTT broker URI, e.g. mqtt://broker.local")
    parser.add_argument("--mqtt-username", default="", help="MQTT username")
    parser.add_argument("--mqtt-password", default="", help="MQTT password")
    parser.add_argument("--verbose", action="store_true", help="Enable verbose provisioning logs")
    args = parser.parse_args()

    asyncio.run(provision(args))


if __name__ == "__main__":
    main()
