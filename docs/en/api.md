# HTTP API and Art-Net

[Home](../../README.en.md) | [Polski](../pl/api.md)

Base URL on a fresh AP: `http://192.168.44.1`. API commands change real outputs. Examples assume a test fixture with a known profile. HTTP 200 acknowledges the node, not the fixture.

## Endpoints

| Method | Path | Request / result |
| --- | --- | --- |
| GET | `/api/status` | JSON: firmware, hardware, addresses, links, universe, frames, packet counters, heap, demo |
| GET | `/api/logs` | Plain-text ring buffer, up to 60 lines |
| GET | `/api/config` | Current configuration JSON, including network credentials; trusted clients only |
| POST | `/api/config` | URL-encoded complete configuration; saves NVS and reboots |
| GET | `/api/profiles` | Stored profile object, excluding built-in definitions unless saved by an import |
| POST | `/api/profiles` | JSON object, replaces stored object; 2–16,000 bytes; 200, 400 or 507 |
| POST | `/api/range` | Form: `addr`, comma-separated `values`; consecutive DMX channels |
| POST | `/api/dmx` | Form: optional `c1` through `c8`; legacy first-eight-channel control |
| POST | `/api/fixture` | Form: `dim,r,g,b,w,mode,strobe`; SHE 7CH at address 1 |
| POST | `/api/demo` | Form: `name`; built-in firmware demo |
| POST | `/api/blackout` | No body required; stops firmware demo and clears all channels |
| GET | `/api/wifi/scan` | Plain-text Wi-Fi scan |
| POST | `/api/update` | Multipart firmware file; OTA flash and restart |

Non-profile POST controls use `application/x-www-form-urlencoded`. Unknown paths return the web UI, so a successful HTTP status alone does not identify an API route.

## Channel Range

```sh
curl --fail http://192.168.44.1/api/status
curl --fail http://192.168.44.1/api/range \
  --data 'addr=1&values=255,255,0,0,0,0,0'
curl --fail -X POST http://192.168.44.1/api/blackout
```

Range values are clamped to 0–255. Address is clamped to 1–512; data extending past channel 512 is truncated. Empty lists return 400. The parser is permissive rather than a strict numeric validator; validate input on the client. Range/fixture controls disable the firmware demo. Art-Net remains another potential writer.

## Profiles

```sh
curl --fail http://192.168.44.1/api/profiles
curl --fail http://192.168.44.1/api/profiles \
  -H 'Content-Type: application/json' \
  --data-binary @examples/laser10-profile.json
```

The raw API replaces the whole stored object. The web importer merges first. Server checks are limited to size and an opening `{`; full channel/profile validation is performed in the web UI, so API clients must validate their own JSON. See [profile format](guide.md#fixture-profiles-and-named-scenes).

## Network Configuration

POST all configuration fields together: `hostname,uniqueNames,apEnabled,apSsid,apPass,apIp,staEnabled,staSsid,staPass,ethEnabled,ethDhcp,ethIp,ethGateway,ethSubnet,ethDns,universe`. Booleans are `1` or `0`. Missing fields are not treated as a partial update. Prefer the configuration UI. Never commit downloaded configuration or credentials.

## Firmware Demos

Names: `red`, `green`, `blue`, `white`, `all-on`, `shehds-color`, `fast-chase`, `fade-rgbw`, `dimmer-ramp`, `shehds-strobe`, `channel-scan`, `mode-scan`. Other names select `off`. These use firmware-specific RGBW/7CH assumptions, not arbitrary imported profile semantics. Profile scenes and browser programs use `/api/range` instead.

## OTA

```sh
curl --fail http://192.168.44.1/api/update \
  -F 'update=@.pio/build/esp32-c3-devkitm-1/firmware.bin'
```

Use the exact target's application image; see [build guide](development.md). The endpoint is unauthenticated.

## Art-Net

- UDP 6454, identifier `Art-Net\0`.
- ArtDMX opcode `0x5000`, little-endian; protocol version 14, big-endian.
- Port-Address/universe little-endian; defaults to 0. Some controllers display this as universe 1.
- Payload length big-endian; use an even number of slots, up to 512.
- ArtPoll discovery is supported. This is output-only DMX, not RDM or patch discovery.
- No Art-Net merging or controller priority arbitration; output holds the buffered values.

[`examples/send_artnet.py`](../../examples/send_artnet.py) transmits only zeros by default. Explicitly provide channels and duration for a test.

```sh
python3 examples/send_artnet.py --host 192.168.44.1 --seconds 2
python3 examples/send_artnet.py --host 192.168.44.1 --address 1 \
  --values 128 --seconds 3 --blackout
```
