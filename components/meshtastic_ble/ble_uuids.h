#pragma once

/**
 * NimBLE UUID structures for the Meshtastic GATT service.
 *
 * NimBLE requires UUIDs as ble_uuid128_t byte arrays in LITTLE-ENDIAN order
 * (i.e. the standard UUID string reversed, byte by byte).
 *
 * The human-readable string forms live in gatt_defs.h alongside the topic
 * and packet constants.  This header provides the NimBLE-ready structs used
 * by ble_gattc_disc_svc_by_uuid() and ble_uuid_cmp() in the implementation.
 *
 * Conversion: reverse the UUID byte-by-byte from the standard string form.
 *   python3 -c "import uuid; u=uuid.UUID('<uuid-string>');
 *               print(', '.join(f'0x{b:02x}' for b in reversed(u.bytes)))"
 */

#include "host/ble_uuid.h"   // ble_uuid128_t, BLE_UUID128_INIT

namespace esphome {
namespace meshtastic_ble {

// ── Service ───────────────────────────────────────────────────────────────────
// String: 6ba1b218-15a8-461f-9fa8-5dcae273eafd
static const ble_uuid128_t MESH_SVC_UUID =
    BLE_UUID128_INIT(0xfd, 0xea, 0x73, 0xe2, 0xca, 0x5d, 0xa8, 0x9f,
                     0x61, 0x46, 0xa8, 0x15, 0x18, 0xb2, 0xa1, 0x6b);

// ── toRadio ───────────────────────────────────────────────────────────────────
// String: f75c76d2-129e-4dad-a1dd-7866124401e7
// Properties: write (write-without-response preferred for throughput)
static const ble_uuid128_t TORADIO_CHR_UUID =
    BLE_UUID128_INIT(0xe7, 0x01, 0x44, 0x40, 0x12, 0x86, 0x1d, 0xa1,
                     0xad, 0x4d, 0x9e, 0x12, 0xd2, 0x76, 0x5c, 0xf7);

// ── fromRadio ─────────────────────────────────────────────────────────────────
// String: 2c55e69e-4993-11ed-b878-0242ac120002
// Properties: read  (one FromRadio protobuf per read; loop until 0 bytes)
// NOTE: firmware 1.x used 8ba2bcc2-ee02-4a55-a531-c525c5e454d5 — do not use.
static const ble_uuid128_t FROMRADIO_CHR_UUID =
    BLE_UUID128_INIT(0x02, 0x00, 0x12, 0xac, 0x42, 0x02, 0x78, 0xb8,
                     0xed, 0x11, 0x93, 0x49, 0x9e, 0xe6, 0x55, 0x2c);

// ── fromNum ───────────────────────────────────────────────────────────────────
// String: ed9da18c-a800-4f66-a670-aa7547e34453
// Properties: read, notify  (subscribe CCCD; each notify means ≥1 fromRadio ready)
static const ble_uuid128_t FROMNUM_CHR_UUID =
    BLE_UUID128_INIT(0x53, 0x44, 0xe3, 0x47, 0x75, 0xaa, 0x70, 0xa6,
                     0x66, 0x4f, 0x00, 0xa8, 0x8c, 0xa1, 0x9d, 0xed);

// ── logRecord (optional) ──────────────────────────────────────────────────────
// String: 5a3d6e49-06e6-4423-9944-e9de8cdf9547
// Properties: notify  (LogRecord protobufs; subscribe for debug logging)
static const ble_uuid128_t LOGRECORD_CHR_UUID =
    BLE_UUID128_INIT(0x47, 0x95, 0xdf, 0x8c, 0xde, 0xe9, 0x44, 0x99,
                     0x23, 0x44, 0xe6, 0x06, 0x49, 0x6e, 0x3d, 0x5a);

// ── CCCD descriptor ───────────────────────────────────────────────────────────
// Standard 16-bit UUID for the Client Characteristic Configuration Descriptor.
// Write 0x0001 to enable notifications; 0x0002 for indications; 0x0000 to disable.
static const ble_uuid16_t CCCD_UUID =
    BLE_UUID16_INIT(0x2902);

}  // namespace meshtastic_ble
}  // namespace esphome
