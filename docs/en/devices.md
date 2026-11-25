# Devices: Test Scope and Sources

[Home](../../README.en.md) | [Polski](../pl/devices.md)

| Device | Evidence | Ready-made controls in this repository |
| --- | --- | --- |
| SHEHDS / SHE-FS403A, 54x3W RGBW LED PAR 7CH | Tests documented in the original firmware project | `she-fs403a-7ch`: dimmer, RGBW, program and strobe/speed |
| Generic laser projector 10CH, unidentified model | User observed patterns, two circles at CH2=150, size and drawing changes | `laser10`, raw channels, scenes and pattern descriptions |
| Robe Pointe Mode 1 | User confirmed response to `1 At 50` sent to grandMA3 over OSC; later color/gobo attempts are not comprehensive validation | No dedicated Pointe profile; a correct DMX definition is needed |
| Generic 6CH dimmer / Botex DPX-620 III | Example profile and tester section present; no physical test confirmation in this package | Generic RAW 6CH and separate Art-Net tester |

The control paths differed. Pointe through a grandMA3 console does not validate a direct Pointe profile on ESP32. The laser was controlled through the node's HTTP API and Art-Net. Packet delivery or HTTP 200 only acknowledges the node; fixture response must be observed separately.

## Images and Links

- SHEHDS 54x3W RGBW: [official product page](https://shehds.com/products/fast-shipping-led-54x3w-rgbw-led-flat-par-rgbw-color-mixing-dj-wash-light-stage-uplighting-ktv-disco-dj-dmx512), [manufacturer manuals](https://shehds.com/pages/user-manual-download), [listing supplying the catalog photograph](https://www.ebay.com/itm/286060053321). `SHE-FS403A` comes from local project notes; not every 54x3W fixture has this channel map.
- Robe Pointe: [official product page, photographs and documentation](https://www.robe.cz/pointe). The image is embedded directly from the Robe CDN.
- Laser: model unconfirmed; no photograph or product listing is represented as the matching device. A similar channel table from another manufacturer is insufficient identification.

Images identify product families and do not document our physical tests. They are externally hosted and not covered by this repository's MIT license. No manufacturer partnership or certification is implied.
