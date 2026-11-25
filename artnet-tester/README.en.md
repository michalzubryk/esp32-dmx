# Art-Net DMX Tester

[Polski](README.md) | English

Optional Python web application sending ArtDMX UDP packets directly to a node. It does not use the ESP32 HTTP API and is not flashed onto the ESP32.

```sh
docker compose up --build
# Open http://127.0.0.1:8080/
```

Linux host networking is used. The host needs a route to the node and UDP 6454 access. Defaults: target `192.168.44.1`, zero-based universe 0, HTTP port 8080.

```sh
ARTNET_TARGET=192.168.1.50 HTTP_PORT=18080 docker compose up --build
```

Controls include raw channel ranges, SHE-FS403A RGBW presets, a Botex DPX-620 III dimmer section (default start 450), and multi-fixture channel tests. Match presets to the fixture manual before use. Four 7CH devices default to addresses `100,107,114,121`; configure them to match the actual patch.

Environment: `ARTNET_TARGET`, `ARTNET_UNIVERSE`, `HTTP_PORT`, `DPX_ADDRESS`, `PIXEZOOM_ADDRESSES`, `PIXEZOOM_CHANNELS`.

The tester has no authentication; run it on a trusted network. It sends a full 512-channel universe and can replace another controller's values. Its presets are separate from the node's imported profiles.
