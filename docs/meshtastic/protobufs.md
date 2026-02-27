# Meshtastic Protobuf Message Reference

> Source: [meshtastic/protobufs](https://github.com/meshtastic/protobufs/tree/master/meshtastic)
> — specifically `mesh.proto`, `telemetry.proto`, `portnums.proto`, `config.proto`, `channel.proto`.
>
> nanopb-generated headers are placed under `components/meshtastic_ble/proto/` by `scripts/gen_proto.sh`.
> The nanopb struct names have the format `meshtastic_<MessageName>` and field names match the `.proto`
> snake_case names directly.

---

## ToRadio

Written to the `toRadio` GATT characteristic to send data **from the client to the radio**.

```protobuf
// mesh.proto
message ToRadio {
  oneof payload_variant {
    MeshPacket packet        = 1;  // send a packet into the mesh
    uint32 want_config_id    = 3;  // request full NodeDB download (see WantConfig handshake)
    bool disconnect          = 4;  // optional graceful disconnect signal
    XModem xmodem_packet     = 5;
    mqtt.MqttClientProxyMessage mqtt_client_proxy_message = 6;
    HeartBeat heartbeat      = 7;  // keepalive on serial; not needed for BLE
  }
}
```

**nanopb usage:**

```c
meshtastic_ToRadio msg = meshtastic_ToRadio_init_zero;
msg.which_payload_variant = meshtastic_ToRadio_want_config_id_tag;
msg.payload_variant.want_config_id = 0xDEADBEEF;

uint8_t buf[64];
pb_ostream_t out = pb_ostream_from_buffer(buf, sizeof(buf));
pb_encode(&out, meshtastic_ToRadio_fields, &msg);
// write buf[0..out.bytes_written] to toRadio characteristic
```

---

## FromRadio

Read from the `fromRadio` GATT characteristic. **Keep reading until you get 0 bytes back.**

```protobuf
// mesh.proto
message FromRadio {
  uint32 num = 1;              // incremented ID — matches fromNum characteristic value

  oneof payload_variant {
    MeshPacket packet            = 2;
    MyNodeInfo my_info           = 3;
    NodeInfo node_info           = 4;
    Config config                = 6;
    LogRecord log_record         = 7;
    uint32 config_complete_id    = 8;  // echoes want_config_id — handshake complete
    Channel channel              = 10;
    ModuleConfig module_config   = 11;
    DeviceMetadata device_metadata = 12;
    MqttClientProxyMessage mqtt_client_proxy_message = 13;
    FileInfo file_info           = 16;
    ClientNotification client_notification = 17;
    XModem xmodem_packet         = 18;
    Metadata metadata            = 19;
  }
}
```

**nanopb decode loop:**

```c
void drain_from_radio(uint16_t conn_handle, uint16_t fromradio_handle) {
    uint8_t buf[512];
    // read fromRadio (blocking or async via callback)
    // if len == 0 → done
    // else:
    meshtastic_FromRadio msg = meshtastic_FromRadio_init_zero;
    pb_istream_t in = pb_istream_from_buffer(buf, len);
    if (!pb_decode(&in, meshtastic_FromRadio_fields, &msg)) {
        ESP_LOGW(TAG, "decode error: %s", in.errmsg);
        return;
    }
    switch (msg.which_payload_variant) {
        case meshtastic_FromRadio_packet_tag:          handle_packet(msg.payload_variant.packet); break;
        case meshtastic_FromRadio_my_info_tag:         handle_my_info(msg.payload_variant.my_info); break;
        case meshtastic_FromRadio_node_info_tag:       handle_node_info(msg.payload_variant.node_info); break;
        case meshtastic_FromRadio_config_complete_id_tag: handle_complete(msg.payload_variant.config_complete_id); break;
        // …
    }
    drain_from_radio(conn_handle, fromradio_handle); // continue draining
}
```

---

## MeshPacket

The core envelope for all mesh traffic.

```protobuf
// mesh.proto
message MeshPacket {
  uint32 from            = 1;  // sender node number
  uint32 to              = 2;  // destination node number (0xFFFFFFFF = broadcast)
  uint32 channel         = 3;  // channel index hash

  oneof payload_variant {
    Data decoded         = 4;  // decrypted & decoded payload
    bytes encrypted      = 5;  // raw encrypted bytes (if we can't decrypt)
  }

  uint32 id              = 6;  // unique packet ID (use for deduplication)
  uint32 rx_time         = 7;  // timestamp (Unix epoch) when received by radio
  float  rx_snr          = 8;
  uint32 hop_limit       = 9;  // remaining hops allowed (decremented by each relay)
  bool   want_ack        = 10; // sender wants an ACK
  MeshPacket.Priority priority = 11;
  int32  rx_rssi         = 12;
  bool   delayed         = 13;
  uint32 via_mqtt        = 14;
  uint32 hop_start       = 15; // original hop_limit set by sender
  uint32 public_key      = 16;
  bool   pki_encrypted   = 17;
  uint32 next_hop        = 18;
  uint32 relay_node      = 19;
}
```

**Broadcast address:** `0xFFFFFFFF` (= `NODENUM_BROADCAST`)

**Deduplication:** Track seen `id` values in a ring buffer. `id == 0` is never valid.

---

## Data (decoded payload)

```protobuf
// mesh.proto
message Data {
  PortNum portnum        = 1;  // which app should receive this
  bytes payload          = 2;  // app-specific protobuf bytes (or raw text for TEXT_MESSAGE_APP)
  bool want_response     = 3;
  uint32 dest            = 4;
  uint32 source          = 5;
  uint32 request_id      = 6;
  uint32 reply_id        = 7;
  uint32 emoji           = 8;
  bool bitfield          = 9;
}
```

---

## MyNodeInfo

Sent once during `WantConfig` response. Contains local node's own number.

```protobuf
// mesh.proto
message MyNodeInfo {
  uint32 my_node_num     = 1;  // THIS node's node number — store this
  uint32 reboot_count    = 8;
  uint32 min_app_version = 11;
}
```

---

## NodeInfo

One entry per known node in the node database. Sent in bulk during `WantConfig`.

```protobuf
// mesh.proto
message NodeInfo {
  uint32   num       = 1;   // node number
  User     user      = 2;
  Position position  = 3;
  DeviceMetrics device_metrics = 4;
  uint32   last_heard = 5;  // Unix timestamp of last packet seen
  uint32   channel   = 6;
  bool     via_mqtt  = 7;
  uint32   snr       = 8;   // last SNR (scaled ×4 — divide by 4.0 for dB)
  bool     is_favorite = 9;
  bool     is_ignored  = 10;
  bool     is_key_manually_verified = 11;
}
```

---

## User

Node identity — name, hardware, and role.

```protobuf
// mesh.proto
message User {
  string id          = 1;   // "!<hex_nodeid>" e.g. "!aabbccdd"
  string long_name   = 2;   // human-readable name (up to ~39 chars)
  string short_name  = 3;   // 2-4 char abbreviation for OLED display
  bytes  macaddr     = 4;   // 6-byte MAC (deprecated in 2.1+, may be empty)
  HardwareModel hw_model = 5;
  bool is_licensed   = 6;   // Ham operator flag
  Config.DeviceConfig.Role role = 7;
  bytes public_key   = 8;
  bool is_unmessagable = 9;
}
```

---

## Position

```protobuf
// mesh.proto
message Position {
  sfixed32 latitude_i   = 1;   // degrees × 1e7  →  lat_degrees = latitude_i * 1e-7
  sfixed32 longitude_i  = 2;   // degrees × 1e7
  int32    altitude     = 3;   // meters above MSL
  uint32   time         = 4;   // POSIX timestamp of fix
  Position.LocSource location_source = 5;
  Position.AltSource altitude_source = 6;
  uint32   timestamp    = 7;
  int32    timestamp_millis_adjust = 8;
  sfixed32 altitude_hae = 9;   // height above WGS84 ellipsoid (mm)
  uint32   altitude_geoidal_separation = 10;
  uint32   pdop         = 11;
  uint32   hdop         = 12;
  uint32   vdop         = 13;
  uint32   gps_accuracy = 14;
  uint32   ground_speed = 15;  // m/s × 100
  uint32   ground_track = 16;  // degrees × 100 clockwise from north
  uint32   fix_quality  = 17;
  uint32   fix_type     = 18;
  uint32   sats_in_view = 19;
  uint32   sensor_id    = 20;
  uint32   next_update  = 21;
  uint32   seq_number   = 22;
  int32    precision_bits = 23;
}
```

**Decode to float:**
```c
float lat = (float)pos.latitude_i  * 1e-7f;
float lon = (float)pos.longitude_i * 1e-7f;
int   alt = pos.altitude;   // meters
```

---

## Telemetry

Sent over the mesh with `portnum = TELEMETRY_APP (67)`. Decode `Data.payload` as `Telemetry`.

```protobuf
// telemetry.proto
message Telemetry {
  uint32 time                           = 1;  // Unix timestamp
  oneof variant {
    DeviceMetrics     device_metrics     = 2;
    EnvironmentMetrics environment_metrics = 3;
    AirQualityMetrics air_quality_metrics = 4;
    PowerMetrics      power_metrics      = 5;
    LocalStats        local_stats        = 6;
    HealthMetrics     health_metrics     = 7;
    HostMetrics       host_metrics       = 8;
    TrafficManagementStats traffic_management_stats = 9;
  }
}

message DeviceMetrics {
  uint32 battery_level      = 1;  // 0–100; >100 means USB powered
  float  voltage            = 2;  // battery voltage in V
  float  channel_utilization = 3; // 0.0–100.0 %
  float  air_util_tx        = 4;  // TX airtime utilization last hour %
  uint32 uptime_seconds     = 5;
}

message EnvironmentMetrics {
  float temperature      = 1;   // °C
  float relative_humidity = 2;  // %
  float barometric_pressure = 3; // hPa
  float gas_resistance   = 4;   // MOhm
  float voltage          = 5;
  float current          = 6;
  uint32 iaq            = 7;    // indoor air quality (0-500)
  float distance        = 8;    // cm
  float lux             = 9;
  float white_lux       = 10;
  float ir_lux          = 11;
  float uv_lux          = 12;
  float wind_direction  = 13;   // degrees
  float wind_speed      = 14;   // m/s
  float weight          = 15;   // kg
  float wind_gust       = 16;
  float wind_lull       = 17;
  float radiation       = 18;   // µR/h
  float soil_moisture   = 19;
  float soil_temperature = 20;
}
```

---

## HardwareModel Enum (selected values)

From `mesh.proto` `HardwareModel` enum:

| Value | Name | Description |
|-------|------|-------------|
| 0 | `UNSET` | Unknown / not set |
| 4 | `TBEAM` | TTGO T-Beam |
| 5 | `HELTEC_V2_0` | Heltec WiFi_LoRa_32 V2 (GPIO13 bat sense) |
| 9 | `RAK4631` | RAK WisBlock 4631 |
| 10 | `HELTEC_V2_1` | Heltec WiFi_LoRa_32 V2 rev (GPIO37 bat sense) |
| 11 | `HELTEC_V1` | Heltec WiFi_LoRa_32 V1 |
| 13 | `RAK11200` | RAK WisBlock 11200 ESP32 |
| 38 | `ANDROID_SIM` | Android simulator |
| 255 | `PRIVATE_HW` | Private hardware |

Full list: [mesh.proto HardwareModel](https://github.com/meshtastic/protobufs/blob/master/meshtastic/mesh.proto)

---

## References

- [meshtastic/protobufs — mesh.proto](https://github.com/meshtastic/protobufs/blob/master/meshtastic/mesh.proto)
- [meshtastic/protobufs — telemetry.proto](https://github.com/meshtastic/protobufs/blob/master/meshtastic/telemetry.proto)
- [meshtastic/protobufs — portnums.proto](https://github.com/meshtastic/protobufs/blob/master/meshtastic/portnums.proto)
- [buf.build hosted proto browser](https://buf.build/meshtastic/protobufs/docs/main:meshtastic)
- [Meshtastic JS — ToRadio](https://js.meshtastic.org/classes/Protobuf.Mesh.ToRadio.html)
- [Rust meshtastic_protobufs crate](https://docs.rs/meshtastic_protobufs/latest/meshtastic_protobufs/meshtastic/)
- [nanopb documentation](https://jpa.kapsi.fi/nanopb/docs/)
