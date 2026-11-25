# User Guide: Profiles, Scenes and Operation

[Home](../../README.en.md) | [Polski](../pl/guide.md)

## Start and Networking

A fresh node creates `DMX-Node-C3-XXXX` with password `dmx12345` and AP address `192.168.44.1/24`. The suffix comes from the chip MAC and can be disabled. The hostname defaults to `esp32-c3-dmx` plus the same suffix, including on other board variants. No station credentials are included in this public build.

Use Configuration to change AP address/password, enable station mode and enter your Wi-Fi credentials, set the Art-Net universe, or configure Ethernet. Saving network settings restarts the device. Station connection runs in the background; with AP enabled, a failed station attempt falls back to AP-only after roughly 20 seconds. Ethernet features require an ETH01 build. The `autonet` variant suppresses Wi-Fi when Ethernet links up and restores it when the link goes down. ETH-only variants may be unreachable without a configured wired network.

Stored NVS settings take precedence over compiled defaults. An OTA update is not a factory reset.

## Web Views

| View | Purpose |
| --- | --- |
| Start | Network addresses, services, firmware information |
| Live | Fixture/profile selection, address, channels, color wheel or raw controls, presets, blackout |
| Programs | Record and edit steps, choose durations and repeats, play/stop, save/load programs |
| Monitor | Frame and packet counters, memory and diagnostic logs |
| Configuration | Network settings, profile JSON import/export, OTA upload |

Use Ustaw (Apply) to transmit raw profile values after changing the form. Named scene buttons apply their channel values immediately. The illustrated preview is approximate; it is not optical feedback, a laser geometry simulator or confirmation from a fixture.

## Fixture Profiles and Named Scenes

Built-in profiles include SHE-FS403A 7CH, generic RGBW 4CH, RAW 8CH and a generic laser 10CH. A profile defines channel count/order, labels, optional ranges, defaults, named scene arrays and fixture-specific blackout values. Generic direct profiles support up to 512 channels, subject to the start address and the 16,000-byte stored JSON limit.

Configuration → Import profiles JSON merges validated profile objects with the UI profiles and stores the merged data in device NVS. Export downloads profile JSON. Updates survive page reload, reboot and browser changes. An import with the same key replaces that profile in the merged set. There is no dedicated profile-delete button; deleting stored entries through the API does not remove built-in firmware profiles.

Examples: [`laser10-profile.json`](../../examples/laser10-profile.json) and [`generic-profile-example.json`](../../examples/generic-profile-example.json). The actual importer expects an object keyed by profile ID, not the older standalone `schema/model/channelCount` proposal.

```json
{
  "my-dimmer": {
    "type": "raw",
    "label": "Single dimmer",
    "channels": 1,
    "labels": ["Dimmer"],
    "defaults": [0],
    "ranges": [[{"label": "Full range", "min": 0, "max": 255}]],
    "scenes": {"Half": [128], "Full": [255]},
    "blackout": {"1": 0}
  }
}
```

`defaults` and every scene must contain exactly `channels` integers in 0–255. Profile IDs use letters, digits, `_` or `-`. `blackout` keys are one-based relative channel numbers. A direct profile (`type: raw`, or another non-legacy type) transmits channel arrays in order.

## Pattern Catalog

For laser10, CH2 selects a pattern in manual mode. Pick a raw value in the catalog, enter a description and save it. The `patterns` object maps values to names, for example `{"150":"Two circles"}`. `patternChannel: 2` enables the catalog for another direct profile.

The list exposes all 256 raw values. The manual says 51 patterns, but exact slot boundaries have not been mapped. Two circles at CH2=150 was observed on the tested projector; this is not a universal manufacturer specification.

## Programs and Scene Management

1. Select the profile and address, then set channel values.
2. In Programs choose a step duration and record the current values.
3. Click an existing step to load it, edit channels/time/address, then replace the selected step.
4. Delete unwanted steps or clear the program. Save/load under a program name.
5. Set repeats: 0 means indefinitely, 1 means once, higher values specify loops.
6. Play sends each step over HTTP; Stop stops the browser scheduler.

Program steps contain the profile ID, start address, duration (`ms`) and full mapped channel array (`values`), including channels above CH8. Programs run in the browser, not autonomously in the ESP32; closing or suspending the page interrupts playback. Named programs use browser localStorage and do not appear automatically on another phone. Profile scenes, by contrast, can be exported and are stored on the ESP32 when imported.

Stop does not necessarily black out the output. Use fixture blackout for the selected direct profile, or the global Blackout control/API to clear all 512 channels. Another Art-Net controller can subsequently write new values.

## Generic Laser 10CH

| Channel | Function | Values |
| --- | --- | --- |
| 1 | Mode | 0–63 off; 64–127 manual; 128–191 auto; 192–255 sound |
| 2 | Pattern/program | Manual: 51 patterns across 0–255; auto/sound: four 64-value bands |
| 3 | Rotation | 0–127 angle; 128–191 forward speed; 192–255 reverse speed |
| 4–5 | Horizontal/vertical flip | 0–127 position; 128–255 automatic speed |
| 6–7 | Horizontal/vertical position | 0–127 position; 128–255 automatic speed |
| 8 | Size | 0–63 fixed, large to small; remaining bands animate scaling |
| 9 | Color | 0–63 single; 64–127 mixed; 128–191 single auto; 192–255 mixed auto |
| 10 | Drawing | Raw 0–255; complete outlines, dots/dashes and progressive drawing observed; boundaries unknown |

Example: `100,150,64,64,64,64,64,0,0,0`. Fixture blackout sets only its mode channel to 0. Match this profile to your fixture's manual; there is no separate verified dimmer channel in this profile.

## Troubleshooting

- Panel inaccessible: check the joined SSID, current gateway, persisted AP IP, DHCP and Ethernet-only mode. Old versions used `192.168.4.1`.
- HTTP works but fixture does not: verify fixture DMX mode/address, matching channel mode, cable pin numbers, transceiver supply and DE pin. ETH01 needs GPIO33, not GPIO5.
- Random flashes or distorted patterns: inspect RS485 polarity, common ground, cable/termination and the electrical signal. Packet/frame counters do not verify the physical waveform or optical output.
- Art-Net ignored: check UDP 6454, routing/firewall and the zero-based universe; input is not OSC.
- Values unexpectedly change: stop other browser programs, demos or external controllers. There is no HTP/LTP merge or priority arbitration.
- Imported profiles fail: use the keyed-object format and valid channel arrays; keep merged JSON at or below 16,000 bytes.
- OTA fails: build for the exact hardware and verify available OTA partition space; recover via USB-UART when necessary.
