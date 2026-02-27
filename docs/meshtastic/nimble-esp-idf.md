# NimBLE / ESP-IDF BLE Client Reference

> Sources:
> [ESP-IDF NimBLE blecent example](https://github.com/espressif/esp-idf/blob/master/examples/bluetooth/nimble/blecent/main/main.c),
> [ESP-IDF GATT Client API](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/bluetooth/esp_gattc.html),
> [ESP-IDF BLE FAQ](https://docs.espressif.com/projects/esp-faq/en/latest/software-framework/bt/ble.html),
> [NimBLE mbuf fragmentation issue #8565](https://github.com/espressif/esp-idf/issues/8565)

This document covers the NimBLE (Apache Mynewt NimBLE, available via esp-idf) APIs needed to
implement a BLE GATT client that connects to a Meshtastic node.

---

## Framework Selection

Use **`esp-idf` framework** in ESPHome (not `arduino`). The Arduino BLE libraries do not expose
the low-level GATTC APIs needed for a reliable, event-driven central implementation.

```yaml
# meshtastic_gw.yaml
esp32:
  board: esp32-c3-devkitm-1
  framework:
    type: esp-idf
    version: recommended
```

---

## Required sdkconfig Options

These must be set for NimBLE to function as a central device with enough resources:

```ini
CONFIG_BT_ENABLED=y
CONFIG_BT_NIMBLE_ENABLED=y
CONFIG_BT_NIMBLE_ROLE_CENTRAL=y
CONFIG_BT_NIMBLE_ROLE_PERIPHERAL=n       # save RAM
CONFIG_BT_NIMBLE_ATT_PREFERRED_MTU=512
CONFIG_BT_NIMBLE_SVC_GAP_DEVICE_NAME="meshtastic-gw"
CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1
```

In ESPHome, place these in a `esp32_idf:` or `sdkconfig_options:` block (ESPHome ≥ 2024):

```yaml
esp32:
  framework:
    type: esp-idf
    sdkconfig_options:
      CONFIG_BT_NIMBLE_ATT_PREFERRED_MTU: "512"
      CONFIG_BT_NIMBLE_MAX_CONNECTIONS: "1"
```

---

## Initialization

```c
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/ble_gattc.h"
#include "services/gap/ble_svc_gap.h"

static void nimble_host_task(void *param) {
    nimble_port_run();  // blocks until nimble_port_stop()
    nimble_port_freertos_deinit();
}

static void on_sync(void) {
    // NimBLE host is ready — start scanning
    start_scan();
}

void setup_ble() {
    nimble_port_init();
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;   // optional — called on fatal error
    ble_svc_gap_init();
    nimble_port_freertos_init(nimble_host_task);
}
```

---

## Scanning

```c
static int gap_event_cb(struct ble_gap_event *event, void *arg);

static void start_scan(void) {
    uint8_t own_addr_type;
    ble_hs_id_infer_auto(0, &own_addr_type);  // public or random address

    struct ble_gap_disc_params dp = {
        .passive           = 1,
        .filter_duplicates = 1,
        .itvl              = BLE_GAP_SCAN_ITVL_MS(200),  // 200 ms interval
        .window            = BLE_GAP_SCAN_WIN_MS(150),   // 150 ms window
    };
    ble_gap_disc(own_addr_type, BLE_HS_FOREVER, &dp, gap_event_cb, NULL);
}
```

### Matching a device by name

```c
// In BLE_GAP_EVENT_DISC handler:
struct ble_hs_adv_fields fields;
ble_hs_adv_parse_fields(&fields, event->disc.data, event->disc.length_data);

if (fields.name != NULL && fields.name_len > 0) {
    if (strncmp((char *)fields.name, "Meshtastic_", 11) == 0) {
        ble_gap_disc_cancel();
        connect(&event->disc.addr);
    }
}
```

---

## Connecting

```c
static void connect(const ble_addr_t *addr) {
    uint8_t own_addr_type;
    ble_hs_id_infer_auto(0, &own_addr_type);

    struct ble_gap_conn_params cp = {
        .scan_itvl    = BLE_GAP_SCAN_ITVL_MS(60),
        .scan_window  = BLE_GAP_SCAN_WIN_MS(30),
        .itvl_min     = BLE_GAP_CONN_ITVL_MS(15),
        .itvl_max     = BLE_GAP_CONN_ITVL_MS(30),
        .latency      = 0,
        .supervision_timeout = BLE_GAP_SUPERVISION_TIMEOUT_MS(5000),
        .min_ce_len   = 0,
        .max_ce_len   = 0,
    };

    // 5 second connection timeout
    ble_gap_connect(own_addr_type, addr, 5000, &cp, gap_event_cb, NULL);
}
```

---

## GAP Event Handler Skeleton

```c
static int gap_event_cb(struct ble_gap_event *event, void *arg) {
    switch (event->type) {
    case BLE_GAP_EVENT_DISC:
        // scanning — check name/service UUID, call connect() if matched
        break;

    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            conn_handle = event->connect.conn_handle;
            // Start MTU exchange, then discover services
            ble_gattc_exchange_mtu(conn_handle, on_mtu, NULL);
        } else {
            // failed — go back to IDLE
        }
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        conn_handle = BLE_HS_CONN_HANDLE_NONE;
        // schedule reconnect
        break;

    case BLE_GAP_EVENT_MTU:
        // MTU negotiated — start service discovery
        discover_services();
        break;

    case BLE_GAP_EVENT_NOTIFY_RX:
        if (event->notify_rx.attr_handle == fromnum_handle) {
            read_from_radio();  // drain fromRadio queue
        }
        break;

    default:
        break;
    }
    return 0;
}
```

---

## Service & Characteristic Discovery

```c
// Meshtastic service UUID (128-bit, little-endian in NimBLE)
static const ble_uuid128_t MESH_SVC_UUID =
    BLE_UUID128_INIT(0xfd, 0xea, 0x73, 0xe2, 0xca, 0x5d, 0xa8, 0x9f,
                     0x61, 0x46, 0xa8, 0x15, 0x18, 0xb2, 0xa1, 0x6b);
// = 6ba1b218-15a8-461f-9fa8-5dcae273eafd, stored LSB first

static int on_svc_disc(uint16_t conn_handle,
                        const struct ble_gatt_error *error,
                        const struct ble_gatt_svc *svc, void *arg) {
    if (error->status != 0) return error->status;
    if (svc == NULL) {
        // done — discover characteristics within found service
        ble_gattc_disc_all_chrs(conn_handle, svc_start, svc_end,
                                 on_chr_disc, NULL);
        return 0;
    }
    svc_start = svc->start_handle;
    svc_end   = svc->end_handle;
    return 0;
}

static void discover_services(void) {
    ble_gattc_disc_svc_by_uuid(conn_handle, &MESH_SVC_UUID.u,
                                on_svc_disc, NULL);
}
```

### Characteristic discovery

```c
// UUIDs for matching characteristics
static const ble_uuid128_t TORADIO_UUID =
    BLE_UUID128_INIT(0xe7, 0x01, 0x44, 0x40, 0x12, 0x86, 0x1d, 0xa1,
                     0xad, 0x4d, 0x9e, 0x12, 0xd2, 0x76, 0xc5, 0xf7);
// = f75c76d2-129e-4dad-a1dd-7866124401e7

static const ble_uuid128_t FROMRADIO_UUID =
    BLE_UUID128_INIT(0x02, 0x00, 0x20, 0x12, 0x02, 0xac, 0x42, 0x11,
                     0xed, 0x11, 0x93, 0x49, 0x9e, 0xe6, 0x55, 0x2c);
// = 2c55e69e-4993-11ed-b878-0242ac120002

static const ble_uuid128_t FROMNUM_UUID =
    BLE_UUID128_INIT(0x53, 0xe3, 0x47, 0xa7, 0x70, 0xa6, 0x70, 0xa6,
                     0x66, 0x4f, 0x00, 0xa8, 0x8c, 0x9e, 0xda, 0xed);
// = ed9da18c-a800-4f66-a670-aa7547e34453

// NOTE: the 16-byte UUID above must be verified byte-by-byte against
// the firmware source. Generate with:
//   python3 -c "import uuid; u=uuid.UUID('ed9da18c-a800-4f66-a670-aa7547e34453');
//               print(', '.join(f'0x{b:02x}' for b in reversed(u.bytes)))"

static int on_chr_disc(uint16_t conn_handle,
                        const struct ble_gatt_error *error,
                        const struct ble_gatt_chr *chr, void *arg) {
    if (error->status != 0) return error->status;
    if (chr == NULL) {
        // all chars discovered — subscribe to fromNum CCCD
        subscribe_fromnum();
        return 0;
    }
    if (ble_uuid_cmp(&chr->uuid.u, &TORADIO_UUID.u)   == 0) toradio_handle   = chr->val_handle;
    if (ble_uuid_cmp(&chr->uuid.u, &FROMRADIO_UUID.u) == 0) fromradio_handle = chr->val_handle;
    if (ble_uuid_cmp(&chr->uuid.u, &FROMNUM_UUID.u)   == 0) fromnum_handle   = chr->val_handle;
    return 0;
}
```

---

## Subscribing to fromNum Notifications

```c
static void subscribe_fromnum(void) {
    // Write 0x0001 to CCCD (descriptor at fromnum_handle + 1 usually)
    uint8_t val[2] = {0x01, 0x00};  // notify enable
    // find the CCCD descriptor handle first, or use ble_gattc_disc_all_dscs
    // then:
    ble_gattc_write_flat(conn_handle, fromnum_cccd_handle,
                          val, sizeof(val), on_subscribe_complete, NULL);
}

static int on_subscribe_complete(uint16_t conn_handle,
                                  const struct ble_gatt_error *error,
                                  struct ble_gatt_attr *attr, void *arg) {
    if (error->status == 0) {
        send_want_config();
    }
    return 0;
}
```

---

## WantConfig Write

```c
static void send_want_config(void) {
    meshtastic_ToRadio msg = meshtastic_ToRadio_init_zero;
    msg.which_payload_variant = meshtastic_ToRadio_want_config_id_tag;
    msg.payload_variant.want_config_id = WANT_CONFIG_ID;  // any nonzero uint32

    uint8_t buf[64];
    pb_ostream_t out = pb_ostream_from_buffer(buf, sizeof(buf));
    pb_encode(&out, meshtastic_ToRadio_fields, &msg);

    ble_gattc_write_no_rsp_flat(conn_handle, toradio_handle,
                                  buf, out.bytes_written);
}
```

---

## Reading fromRadio (drain loop)

```c
static int on_fromradio_read(uint16_t conn_handle,
                              const struct ble_gatt_error *error,
                              struct ble_gatt_attr *attr, void *arg) {
    if (error->status != 0) return error->status;

    uint16_t len = OS_MBUF_PKTLEN(attr->om);
    if (len == 0) {
        // queue empty — done
        return 0;
    }

    uint8_t buf[512];
    ble_hs_mbuf_to_flat(attr->om, buf, sizeof(buf), &len);

    handle_from_radio(buf, len);

    // schedule next read (don't recurse directly in the callback)
    // e.g., post an event to a FreeRTOS queue
    return 0;
}

static void read_from_radio(void) {
    ble_gattc_read(conn_handle, fromradio_handle, on_fromradio_read, NULL);
}
```

> **Important:** Do not call `ble_gattc_read` recursively inside the read callback.
> Post a message to a FreeRTOS queue or use `ble_npl_event_run` to schedule the
> next read outside the callback context.

---

## Teardown / Disconnect

```c
static void disconnect(void) {
    if (conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }
}

// Full NimBLE shutdown (e.g., before deep sleep):
void ble_deinit(void) {
    nimble_port_stop();
    nimble_port_freertos_deinit();
    nimble_port_deinit();
}
```

---

## UUID Byte-Order Note

NimBLE `BLE_UUID128_INIT` macro takes bytes in **little-endian (reversed)** order compared to
the human-readable UUID string. To convert:

```python
import uuid
u = uuid.UUID('6ba1b218-15a8-461f-9fa8-5dcae273eafd')
print(', '.join(f'0x{b:02x}' for b in reversed(u.bytes)))
# 0xfd, 0xea, 0x73, 0xe2, 0xca, 0x5d, 0xa8, 0x9f,
# 0x61, 0x46, 0xa8, 0x15, 0x18, 0xb2, 0xa1, 0x6b
```

---

## References

- [ESP-IDF NimBLE blecent example (main.c)](https://github.com/espressif/esp-idf/blob/master/examples/bluetooth/nimble/blecent/main/main.c)
- [ESP-IDF GATT Client API reference](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/bluetooth/esp_gattc.html)
- [ESP-IDF Bluedroid GATT client walkthrough](https://github.com/espressif/esp-idf/blob/master/examples/bluetooth/bluedroid/ble/gatt_client/tutorial/Gatt_Client_Example_Walkthrough.md)
- [NimBLE mbuf fragmentation — ble_hs_mbuf_to_flat](https://github.com/espressif/esp-idf/issues/8565)
- [ESP32 BLE FAQ](https://docs.espressif.com/projects/esp-faq/en/latest/software-framework/bt/ble.html)
- [Apache MyNewt NimBLE docs](https://mynewt.apache.org/latest/network/docs/ble_hs/ble_gap.html)
