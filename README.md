# ESPHome-Meshtastic-MQTT

An ESP32 gateway that bridges Meshtastic devices over BLE to MQTT, enabling deep integration with Home Assistant via ESPHome.

---

## Overview

Meshtastic devices expose a full GATT service over BLE. This project turns an ESP32 into a dedicated BLE client that speaks that GATT service natively — parsing protobuf packets, forwarding them over MQTT, and maintaining a live session with the Meshtastic node.

The ESP32 runs ESPHome with a custom component that handles:

- BLE client connection to a Meshtastic node
- Full Meshtastic GATT service implementation
- Protobuf packet parsing and encoding
- MQTT publishing and subscribing
- Session state tracking and reconnect logic
- Optional packet-level encryption handling

Home Assistant receives structured MQTT messages and can display node telemetry, send messages back into the mesh, and trigger automations based on mesh events.

---

## Why an ESP32?

Most Meshtastic-to-MQTT bridges run on a Raspberry Pi or similar Linux host. This project takes a different approach:

- **Always-on, low-power** — the ESP32 draws milliamps at idle
- **No Linux dependency** — no OS, no Python, no pip
- **ESPHome ecosystem** — native Home Assistant discovery, OTA updates, web UI, and secrets management out of the box
- **Single-purpose appliance** — sits near your Meshtastic node and does one thing reliably

---

## What the ESP32 Must Do

Bridging the Meshtastic BLE GATT API correctly is non-trivial. The firmware must:

### 1. Implement the Meshtastic GATT Client

The ESP32 acts as a **BLE central** (client), not a peripheral. It scans for a Meshtastic node by name or MAC address, connects, and discovers the Meshtastic GATT service (`0x6ba4` service UUID family).

Key characteristics and remote characteristics (e.g. `fromRadio`, `toRadio`, `fromNum`) must be discovered and subscribed to via CCCD notifications.

### 2. Parse Protobuf Packets

All data exchanged with a Meshtastic node uses [Protocol Buffers](https://protobuf.dev/). The ESP32 must decode incoming `MeshPacket`, `FromRadio`, `MyNodeInfo`, `NodeInfo`, `Telemetry`, and other message types from raw BLE notify payloads.

Encoding `ToRadio` packets for downlink (sending messages into the mesh) requires the same protobuf schema in reverse.

A lightweight protobuf library suitable for embedded targets (e.g. [nanopb](https://github.com/nanopb/nanopb)) is used rather than the full C++ protobuf runtime.

### 3. Forward Over MQTT

Decoded packets are published to structured MQTT topics, for example:

```
meshtastic/<node_id>/text
meshtastic/<node_id>/telemetry/battery
meshtastic/<node_id>/telemetry/temperature
meshtastic/<node_id>/position/latitude
meshtastic/<node_id>/position/longitude
meshtastic/<node_id>/nodeinfo/longname
meshtastic/<node_id>/raw
```

The gateway also subscribes to a command topic so Home Assistant can send text messages or admin packets back into the mesh:

```
meshtastic/send/text
meshtastic/send/raw
```

ESPHome's native MQTT component handles broker connection, TLS, and Last Will & Testament automatically.

### 4. Maintain Session State

A Meshtastic session involves more than a single BLE connection. The firmware tracks:

- `MyNodeInfo` — the local node's own number and config
- `NodeDB` — the list of known mesh nodes and their last-heard state
- `WantConfig` sequence — the initial config sync handshake after connect
- Packet deduplication by packet ID to avoid re-publishing retransmissions

State is kept in RAM across BLE reconnects where possible.

### 5. Handle Reconnect Logic

BLE connections drop. The firmware implements:

- Configurable scan interval and reconnect backoff
- Re-subscription to GATT notifications after reconnect
- Re-running the `WantConfig` handshake to resync node state
- MQTT availability topic updated on connect/disconnect so Home Assistant shows the correct device state

### 6. Handle Encryption (Optional)

Meshtastic uses AES-256-CTR with a shared channel key. By default the BLE GATT interface delivers **decrypted** packets to a connected BLE client, so encryption is transparent for most use cases.

If the channel uses a non-default PSK, the firmware can optionally hold the channel key and decrypt packets locally — though this is only necessary when connecting to nodes with non-default encryption configs.

---

## Architecture

```
+------------------+        BLE (GATT)        +------------------+
| Meshtastic Node  | <-----------------------> |     ESP32        |
| (T-Beam, T3S3,   |                           | (ESPHome +       |
|  Heltec, etc.)   |                           |  custom component|
+------------------+                           +--------+---------+
                                                        |
                                                      MQTT
                                                        |
                                               +--------+---------+
                                               | Home Assistant   |
                                               | MQTT Broker      |
                                               +------------------+
```

---

## Requirements

### Hardware

- ESP32 with BLE support (ESP32, ESP32-S3, ESP32-C3, ESP32-C6)
  - ESP32-C3 or ESP32-C6 recommended for low power
  - ESP32-S2 is **not** supported (no BLE)
- Any Meshtastic-compatible node (Heltec, LilyGo T-Beam, T3S3, RAK WisBlock, etc.) running a recent Meshtastic firmware release

### Software

- [ESPHome](https://esphome.io) 2024.x or later
- Home Assistant with an MQTT broker (Mosquitto add-on or external)
- Python 3 + `esphome` CLI for local builds (optional if using HA add-on)

---

## Repository Layout

```
ESPHome-Meshtastic-MQTT/
│
├── meshtastic_gw.yaml              # Main ESPHome configuration — edit this
├── secrets.yaml.example            # Template for credentials (copy → secrets.yaml)
│
├── components/
│   └── meshtastic_ble/             # ESPHome custom component
│       ├── __init__.py             # Component schema & code-gen (Python)
│       ├── meshtastic_ble.h        # C++ class declaration
│       ├── meshtastic_ble.cpp      # C++ implementation
│       ├── gatt_defs.h             # GATT UUIDs, topic suffixes, constants
│       │
│       ├── proto/                  # nanopb-generated sources (run gen_proto.sh)
│       │   └── meshtastic/
│       │       ├── mesh.pb.h / .c
│       │       ├── telemetry.pb.h / .c
│       │       ├── config.pb.h / .c
│       │       └── ...
│       │
│       └── nanopb/                 # nanopb runtime (copied by gen_proto.sh)
│           ├── pb.h
│           ├── pb_decode.h / .c
│           └── pb_encode.h / .c
│
└── scripts/
    └── gen_proto.sh                # Fetch Meshtastic .proto files & run nanopb
```

---

## Getting Started

### 1. Prerequisites

| Tool | Version | Notes |
|------|---------|-------|
| ESPHome | ≥ 2024.6 | `pip install esphome` or HA add-on |
| Python | ≥ 3.10 | For ESPHome CLI and proto generation |
| protoc | any recent | `apt install protobuf-compiler` / `brew install protobuf` |
| nanopb | ≥ 0.4.8 | `pip install nanopb` |

### 2. Clone and configure

```bash
git clone https://github.com/<you>/ESPHome-Meshtastic-MQTT.git
cd ESPHome-Meshtastic-MQTT

cp secrets.yaml.example secrets.yaml
# Edit secrets.yaml — fill in WiFi, MQTT broker, and Meshtastic node name
```

### 3. Generate protobuf sources

This only needs to be run once (and again after a Meshtastic firmware update):

```bash
./scripts/gen_proto.sh
# Optionally pin a version:
# MESHTASTIC_PROTO_VERSION=v2.5 ./scripts/gen_proto.sh
```

The script fetches `.proto` files from the [Meshtastic protobufs repo](https://github.com/meshtastic/protobufs), runs the nanopb generator, and places the output under `components/meshtastic_ble/proto/` and `components/meshtastic_ble/nanopb/`.

### 4. Flash

```bash
esphome run meshtastic_gw.yaml
```

For subsequent updates over WiFi:

```bash
esphome upload meshtastic_gw.yaml
```

### 5. Watch the logs

```bash
esphome logs meshtastic_gw.yaml
```

The ESP32 will scan for your Meshtastic node, connect over BLE, run the `WantConfig` handshake to sync the node database, then enter the `READY` state. After that, packets appear on MQTT immediately.

### 6. Home Assistant

MQTT discovery payloads are published automatically. In Home Assistant go to **Settings → Devices & Services → MQTT** and the gateway device should appear. Individual sensors (battery, position, messages) are mapped to entities.

---

## Project Status

This project is in active early development. Current focus areas:

- [ ] BLE central scan and connect to Meshtastic GATT service
- [ ] `fromRadio` notify handler and protobuf decode (nanopb)
- [ ] `toRadio` write for sending packets
- [ ] `WantConfig` handshake and initial node sync
- [ ] MQTT topic schema and Home Assistant discovery payloads
- [ ] Reconnect and backoff logic
- [ ] ESPHome YAML configuration examples
- [ ] Position, telemetry, and text message entity mappings

---

## Contributing

Issues and pull requests are welcome. If you are working on BLE GATT, protobuf, or ESPHome custom components and want to collaborate, open an issue to discuss before submitting large changes.

---

## License

See [LICENSE](LICENSE).
