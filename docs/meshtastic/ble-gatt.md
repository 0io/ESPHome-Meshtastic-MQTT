# Meshtastic BLE GATT Reference

> Sources: [Meshtastic Client API docs](https://meshtastic.org/docs/development/device/client-api/),
> [meshtastic/firmware NimbleBluetooth.cpp](https://github.com/meshtastic/firmware/blob/master/src/nimble/NimbleBluetooth.cpp),
> [meshtastic-python BLEInterface](https://python.meshtastic.org/ble_interface.html),
> [HackMD protocol breakdown](https://hackmd.io/@minexo79/meshtastic-protocol)

---

## Service UUID

| Service | UUID |
|---------|------|
| **Meshtastic Mesh Service** | `6ba1b218-15a8-461f-9fa8-5dcae273eafd` |
| Software Update Service | `cb0b9a0b-a84c-4c0d-bdbb-442e3144ee30` |

---

## Characteristics (Current API — firmware ≥ 2.x)

The current API exposes **three** characteristics. Everything flows through them as length-prefixed protobuf streams.

| Name | UUID | Properties | Description |
|------|------|-----------|-------------|
| **`toRadio`** | `f75c76d2-129e-4dad-a1dd-7866124401e7` | write | Client → Radio. Write a `ToRadio` protobuf here. Max `MAXPACKET` bytes per write. |
| **`fromRadio`** | `2c55e69e-4993-11ed-b878-0242ac120002` | read | Radio → Client. Read one `FromRadio` protobuf per call. Returns 0 bytes when the queue is empty. |
| **`fromNum`** | `ed9da18c-a800-4f66-a670-aa7547e34453` | read, notify | A counter incremented each time a new `FromRadio` packet is available. Subscribe to notifications here; each notify means there is at least one `FromRadio` to read. |
| `logRecord` | `5a3d6e49-06e6-4423-9944-e9de8cdf9547` | notify | Debug log records as `LogRecord` protobufs. Optional — subscribe and log if desired. |

> **Note on old API (firmware 1.x legacy):** Earlier firmware also exposed separate characteristics for
> `mynode` (`ea9f3f82-...`), `nodeinfo` (`d31e02e0-...`), `radio` (`b56786c8-...`), and `owner` (`6ff1d8b6-...`).
> These are gone in current firmware. All node state is now streamed through `fromRadio` during the
> `WantConfig` handshake. Do not target these old UUIDs.

---

## Packet Size (MTU)

- Negotiate MTU to **512 bytes** immediately after connecting.
- Meshtastic radio packets cap at **237 bytes** payload; the protobuf envelope adds overhead.
- A 512-byte buffer is safe for all `fromRadio` reads.
- In NimBLE (ESP-IDF), flatten the mbuf with `ble_hs_mbuf_to_flat()` before decoding:

```c
uint16_t len = OS_MBUF_PKTLEN(event->notify_rx.om);
uint8_t buf[512];
ble_hs_mbuf_to_flat(event->notify_rx.om, buf, sizeof(buf), &len);
```

---

## BLE Advertising / Discovery

Meshtastic nodes advertise:
- The **Mesh Service UUID** in their advertisement service list.
- A local name typically of the form `Meshtastic_XXXX` where `XXXX` is the last four hex digits of the
  device MAC.

To find a node, scan for peripherals whose advertisement includes service UUID `6ba1b218-15a8-461f-9fa8-5dcae273eafd`,
or filter by the known local name prefix `Meshtastic_`.

In NimBLE (passive scan example):

```c
struct ble_gap_disc_params dp = {
    .passive        = 1,
    .filter_duplicates = 1,
    .itvl           = BLE_GAP_SCAN_ITVL_MS(200),
    .window         = BLE_GAP_SCAN_WIN_MS(150),
};
ble_gap_disc(own_addr_type, BLE_HS_FOREVER, &dp, gap_event_handler, NULL);
```

Inside `BLE_GAP_EVENT_DISC`, check `event->disc.event_type == BLE_HCI_ADV_RPT_EVTYPE_ADV_IND`
and compare the device name or service UUIDs from the AD structures.

---

## Connection Sequence

```
Client                                        Meshtastic Node
  |                                                  |
  |──── ble_gap_connect() ───────────────────────>   |
  |<─── BLE_GAP_EVENT_CONNECT (status=0) ─────────   |
  |                                                  |
  |──── ble_gattc_disc_svc_by_uuid(MESH_SVC) ──>    |
  |<─── service + characteristic handles ──────────  |
  |                                                  |
  |──── exchange MTU → 512 ─────────────────────>    |
  |                                                  |
  |──── write CCCD 0x0001 on fromNum ──────────>     |  ← subscribe to notifications
  |                                                  |
  |──── write ToRadio{wantConfigId: N} ─────────>    |  ← start config handshake
  |<─── FromRadio{myInfo} ─────────────────────────  |
  |<─── FromRadio{nodeInfo} × M ───────────────────  |
  |<─── FromRadio{channel} × C ────────────────────  |
  |<─── FromRadio{config} ─────────────────────────  |
  |<─── FromRadio{moduleConfig} ───────────────────  |
  |<─── FromRadio{configCompleteId: N} ────────────  |  ← handshake complete
  |                                                  |
  |   ── READY — live packet forwarding ──           |
  |                                                  |
  |<─── fromNum NOTIFY ────────────────────────────  |  ← new packet available
  |──── read fromRadio ─────────────────────────>    |
  |<─── FromRadio{packet} ─────────────────────────  |
  |──── read fromRadio (loop until empty) ──────>    |
  |<─── [0 bytes] ─────────────────────────────────  |
```

---

## Sending Packets

To send a mesh packet (e.g., a text message), write a `ToRadio` protobuf to the `toRadio` characteristic:

```c
// Encode
meshtastic_ToRadio to_radio = meshtastic_ToRadio_init_zero;
to_radio.which_payload_variant = meshtastic_ToRadio_packet_tag;
to_radio.payload_variant.packet.to = NODENUM_BROADCAST;  // 0xFFFFFFFF
to_radio.payload_variant.packet.decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
// ... fill decoded.payload ...

uint8_t buf[MESHTASTIC_MAX_PACKET_LEN];
pb_ostream_t stream = pb_ostream_from_buffer(buf, sizeof(buf));
pb_encode(&stream, meshtastic_ToRadio_fields, &to_radio);

// Write (no response needed, use write-without-response for speed)
ble_gattc_write_no_rsp_flat(conn_handle, toradio_handle, buf, stream.bytes_written);
```

---

## References

- [Meshtastic Client API](https://meshtastic.org/docs/development/device/client-api/)
- [meshtastic/firmware — NimbleBluetooth.cpp](https://github.com/meshtastic/firmware/blob/master/src/nimble/NimbleBluetooth.cpp)
- [meshtastic-python — BLEInterface source](https://github.com/meshtastic/python/blob/master/meshtastic/ble_interface.py)
- [ESP-IDF NimBLE blecent example](https://github.com/espressif/esp-idf/blob/master/examples/bluetooth/nimble/blecent/main/main.c)
- [meshtastic/protobufs — mesh.proto](https://github.com/meshtastic/protobufs/blob/master/meshtastic/mesh.proto)
