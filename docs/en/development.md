# Build, Flash, OTA and Development

[Home](../../README.md) | [Polski](../pl/development.md)

## Setup

```sh
git clone https://github.com/michalzubryk/esp32-dmx.git
cd esp32-dmx
python3 -m venv .venv
.venv/bin/pip install platformio
.venv/bin/pio run -e esp32-c3-devkitm-1
```

PlatformIO installs the Espressif32 platform, Arduino framework and toolchains. No private Wi-Fi settings or build caches are required. The project follows the original unpinned `espressif32` platform configuration; use a fixed platform version in a controlled deployment if reproducible dependency selection is required.

## Environments

| Environment | Hardware / behavior |
| --- | --- |
| `esp32-c3-devkitm-1` | C3, TX GPIO4, DE GPIO5 |
| `esp32s` | Classic ESP32 Dev Module, TX GPIO4, DE GPIO5 |
| `esp32-s3` | S3 DevKitC-1, TX GPIO4, DE GPIO5 |
| `esp32-eth01` | LAN8720 PHY address 1, TX GPIO4, DE GPIO33 |
| `esp32-eth01-ethonly` | ETH01; forces Ethernet-only and DHCP on each boot |
| `esp32-eth01-autonet` | ETH01 with automatic Wi-Fi suppression/fallback |
| `esp32-eth01-phy0` | ETH01 with PHY address 0 |

Environments indicate available build configurations, not physical validation of every board revision. Verify GPIO availability and PHY wiring on your hardware.

## USB Upload

```sh
.venv/bin/pio run -e esp32s -t upload --upload-port /dev/ttyUSB0
.venv/bin/pio device monitor --baud 115200
```

Choose the correct environment. ETH01 requires manual bootloader entry and an external adapter; see [wiring](hardware.md). Keep power stable. Uploading to hardware is intentionally not part of CI.

## OTA

Build the same environment as the installed hardware. Open Configuration, choose `.pio/build/<environment>/firmware.bin`, then upload through the OTA control. Do not upload an ELF, bootloader, partition table, merged image or binary for a different chip/board. The server reboots after success. Network settings and profile NVS normally survive. If the application no longer boots or the OTA partition cannot accept the image, recover over USB-UART.

## Source Layout

```text
platformio.ini             Hardware build environments
src/main.cpp              DMX UART task, Art-Net, HTTP API, embedded main web UI
src/profiles_ui.h         Dynamic profile/channel UI, laser10, pattern catalog
examples/                 JSON profiles and ArtDMX sender
tools/preview.py           Offline UI backend for documentation/screenshots
tools/screenshots.py       Chrome DevTools screenshot generation
artnet-tester/            Separate Python/Docker Art-Net tester
docs/en/ and docs/pl/      Bilingual documentation
.github/workflows/        Firmware build matrix
```

## Offline UI Preview and Screenshots

```sh
python3 tools/preview.py
# Open http://127.0.0.1:8765/
```

The preview extracts the HTML/JavaScript from the firmware and supplies synthetic status/configuration APIs. It binds only to loopback and never sends DMX. It is not the firmware or a replacement controller. To regenerate screenshots, install `websocket-client` in a virtual environment and run `tools/screenshots.py` with Google Chrome available. Screenshots should show mock data only.

## Verification

Build each environment with `pio run -e <environment>`, run `python3 -m py_compile examples/send_artnet.py tools/preview.py tools/screenshots.py`, validate example JSON, and inspect the preview on desktop and mobile. CI compiles each hardware environment and uploads its application image as a build artifact. A successful build does not validate electrical DMX timing or a connected fixture.

## Docker Art-Net Tester

```sh
cd artnet-tester
docker compose up --build
```

This is a separate host application, not code flashed to ESP32. It needs a working network route to the node and UDP 6454 access. Consult its README before using fixture-specific presets.
