# Meshtastic Reference Documentation

Reference material for building the ESPHome BLE → MQTT gateway component.

| Document | Contents |
|----------|----------|
| [ble-gatt.md](ble-gatt.md) | Service/characteristic UUIDs, MTU, scanning, connection sequence diagram |
| [protobufs.md](protobufs.md) | `ToRadio`, `FromRadio`, `MeshPacket`, `Position`, `Telemetry`, `User`, `HardwareModel` message definitions |
| [portnums.md](portnums.md) | Full `PortNum` enum table; decode examples for TEXT, POSITION, NODEINFO, TELEMETRY |
| [nimble-esp-idf.md](nimble-esp-idf.md) | NimBLE C API snippets: scan, connect, discover, subscribe, MTU, read/write, UUID byte-order |

---

## Key Facts at a Glance

### GATT UUIDs (firmware ≥ 2.x)

```
Service  : 6ba1b218-15a8-461f-9fa8-5dcae273eafd
toRadio  : f75c76d2-129e-4dad-a1dd-7866124401e7  (write)
fromRadio: 2c55e69e-4993-11ed-b878-0242ac120002  (read)
fromNum  : ed9da18c-a800-4f66-a670-aa7547e34453  (read, notify)
logRecord: 5a3d6e49-06e6-4423-9944-e9de8cdf9547  (notify, optional)
```

> **Note:** The old v1 API had additional characteristics (`mynode`, `nodeinfo`, `radio`, `owner`).
> Current firmware has removed them — all state flows through the three characteristics above.

### Connection Handshake

1. Connect, negotiate MTU → 512
2. Discover `toRadio` / `fromRadio` / `fromNum` handles
3. Subscribe to `fromNum` CCCD notifications
4. Write `ToRadio{want_config_id: N}` — triggers NodeDB download
5. Read `fromRadio` in a loop until empty (receives `MyNodeInfo`, `NodeInfo`×N, `Channel`×C, `Config`, `configCompleteId == N`)
6. From now on: each `fromNum` notify → drain `fromRadio` loop

### Port Numbers to Implement

| Priority | PortNum | Value | Payload |
|----------|---------|-------|---------|
| High | `TEXT_MESSAGE_APP` | 1 | UTF-8 string |
| High | `POSITION_APP` | 3 | `Position` proto |
| High | `NODEINFO_APP` | 4 | `User` proto |
| High | `TELEMETRY_APP` | 67 | `Telemetry` proto |
| Medium | `ROUTING_APP` | 5 | ACKs |
| Low | `TRACEROUTE_APP` | 70 | `RouteDiscovery` proto |

### Proto Generation

```bash
./scripts/gen_proto.sh   # fetches protobufs + generates nanopb sources
```

Output goes to:
- `components/meshtastic_ble/proto/meshtastic/*.pb.{h,c}`
- `components/meshtastic_ble/nanopb/*.{h,c}`

---

## Official Sources

- [Meshtastic Client API docs](https://meshtastic.org/docs/development/device/client-api/)
- [meshtastic/protobufs on GitHub](https://github.com/meshtastic/protobufs)
- [meshtastic/firmware on GitHub](https://github.com/meshtastic/firmware)
- [meshtastic-python BLEInterface](https://github.com/meshtastic/python/blob/master/meshtastic/ble_interface.py)
- [buf.build proto browser](https://buf.build/meshtastic/protobufs/docs/main:meshtastic)
- [Meshtastic Port Numbers](https://meshtastic.org/docs/development/firmware/portnum/)
