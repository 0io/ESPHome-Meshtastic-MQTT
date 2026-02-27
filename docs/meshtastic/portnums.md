# Meshtastic Port Numbers (PortNum)

> Source: [meshtastic/protobufs — portnums.proto](https://github.com/meshtastic/protobufs/blob/master/meshtastic/portnums.proto),
> [Meshtastic Port Numbers docs](https://meshtastic.org/docs/development/firmware/portnum/)

The `portnum` field in `Data` tells the receiving application how to decode `Data.payload`.

---

## Enum Values

| Value | Name | Encoding | Notes |
|-------|------|----------|-------|
| 0 | `UNKNOWN_APP` | — | Reserved |
| 1 | `TEXT_MESSAGE_APP` | UTF-8 string | Plain text messages. Firmware may transparently compress to value 7. |
| 2 | `REMOTE_HARDWARE_APP` | Protobuf `HardwareMessage` | GPIO control |
| 3 | `POSITION_APP` | Protobuf `Position` | GPS position |
| 4 | `NODEINFO_APP` | Protobuf `User` | Node identity broadcast |
| 5 | `ROUTING_APP` | Protobuf `Routing` | ACKs and mesh routing control |
| 6 | `ADMIN_APP` | Protobuf `AdminMessage` | Admin commands (config changes) |
| 7 | `TEXT_MESSAGE_COMPRESSED_APP` | Compressed UTF-8 | Auto-decompressed to `TEXT_MESSAGE_APP` on receive |
| 8 | `WAYPOINT_APP` | Protobuf `Waypoint` | Named map waypoints |
| 9 | `AUDIO_APP` | raw audio | (Baofeng specific, experimental) |
| 10 | `DETECTION_SENSOR_APP` | UTF-8 string | Sensor alert text |
| 11 | `ALERT_APP` | Protobuf | Bell alert |
| 12 | `KEY_VERIFICATION_APP` | Protobuf | PKI key exchange |
| 32 | `REPLY_APP` | — | Echo/reply test |
| 33 | `IP_TUNNEL_APP` | raw IP | IP-over-LoRa tunnel |
| 34 | `PAXCOUNTER_APP` | Protobuf `Paxcount` | WiFi/BT device counter |
| 64 | `SERIAL_APP` | raw bytes | Serial bridge |
| 65 | `STORE_FORWARD_APP` | Protobuf | Store-and-forward messages |
| 66 | `RANGE_TEST_APP` | UTF-8 string | Range test |
| **67** | **`TELEMETRY_APP`** | **Protobuf `Telemetry`** | Device + environment metrics |
| 68 | `ZPS_APP` | — | Zero-Positioning System |
| 69 | `SIMULATOR_APP` | — | Simulator |
| 70 | `TRACEROUTE_APP` | Protobuf `RouteDiscovery` | Traceroute |
| 71 | `NEIGHBORINFO_APP` | Protobuf `NeighborInfo` | Neighbor info |
| 72 | `ATAK_PLUGIN` | Protobuf | ATAK/CoT integration |
| 73 | `MAP_REPORT_APP` | Protobuf `MapReport` | Map reporting |
| 74 | `POWERSTRESS_APP` | Protobuf | Power stress testing |
| 76 | `RETICULUM_TUNNEL_APP` | raw | Reticulum protocol tunnel |
| 256 | `PRIVATE_APP` | app-defined | Private/unregistered |
| 257 | `ATAK_FORWARDER` | Protobuf | ATAK forwarder |
| 511 | `MAX` | — | Upper bound sentinel |

---

## Port Number Ranges

| Range | Owner |
|-------|-------|
| 0–63 | Meshtastic core — do not use for third-party apps |
| 64–127 | Registered third-party apps — submit PR to `portnums.proto` to claim |
| 128–255 | Unallocated |
| 256–511 | Private / unregistered apps |

---

## Decoding by PortNum

### TEXT_MESSAGE_APP (1)

`Data.payload` is a raw UTF-8 string — **not** a protobuf. Read it directly:

```c
case meshtastic_PortNum_TEXT_MESSAGE_APP: {
    // payload.bytes is NOT null-terminated
    char text[pkt.decoded.payload.size + 1];
    memcpy(text, pkt.decoded.payload.bytes, pkt.decoded.payload.size);
    text[pkt.decoded.payload.size] = '\0';
    publish_text(pkt.from, text);
    break;
}
```

### POSITION_APP (3)

`Data.payload` is a `Position` protobuf:

```c
case meshtastic_PortNum_POSITION_APP: {
    meshtastic_Position pos = meshtastic_Position_init_zero;
    pb_istream_t in = pb_istream_from_buffer(
        pkt.decoded.payload.bytes, pkt.decoded.payload.size);
    if (pb_decode(&in, meshtastic_Position_fields, &pos)) {
        float lat = pos.latitude_i  * 1e-7f;
        float lon = pos.longitude_i * 1e-7f;
        int32_t alt = pos.altitude;          // meters above MSL
        // publish lat, lon, alt
    }
    break;
}
```

### NODEINFO_APP (4)

`Data.payload` is a `User` protobuf:

```c
case meshtastic_PortNum_NODEINFO_APP: {
    meshtastic_User user = meshtastic_User_init_zero;
    pb_istream_t in = pb_istream_from_buffer(
        pkt.decoded.payload.bytes, pkt.decoded.payload.size);
    if (pb_decode(&in, meshtastic_User_fields, &user)) {
        // user.long_name, user.short_name, user.hw_model
    }
    break;
}
```

### TELEMETRY_APP (67)

`Data.payload` is a `Telemetry` protobuf:

```c
case meshtastic_PortNum_TELEMETRY_APP: {
    meshtastic_Telemetry tel = meshtastic_Telemetry_init_zero;
    pb_istream_t in = pb_istream_from_buffer(
        pkt.decoded.payload.bytes, pkt.decoded.payload.size);
    if (pb_decode(&in, meshtastic_Telemetry_fields, &tel)) {
        if (tel.which_variant == meshtastic_Telemetry_device_metrics_tag) {
            // tel.variant.device_metrics.battery_level (0-100)
            // tel.variant.device_metrics.voltage (V)
            // tel.variant.device_metrics.channel_utilization (%)
            // tel.variant.device_metrics.air_util_tx (%)
        }
        if (tel.which_variant == meshtastic_Telemetry_environment_metrics_tag) {
            // tel.variant.environment_metrics.temperature (°C)
            // tel.variant.environment_metrics.relative_humidity (%)
            // tel.variant.environment_metrics.barometric_pressure (hPa)
        }
    }
    break;
}
```

---

## References

- [portnums.proto on GitHub](https://github.com/meshtastic/protobufs/blob/master/meshtastic/portnums.proto)
- [Meshtastic port numbers docs](https://meshtastic.org/docs/development/firmware/portnum/)
- [buf.build PortNum browser](https://buf.build/meshtastic/protobufs/docs/main:meshtastic#meshtastic.PortNum)
