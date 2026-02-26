#pragma once

/**
 * Meshtastic BLE GATT service / characteristic UUIDs and helpers.
 *
 * Source of truth for these values:
 *   https://github.com/meshtastic/firmware/blob/master/src/mesh/api/MeshBLEService.cpp
 *
 * The Meshtastic GATT API overview:
 *   https://meshtastic.org/docs/development/device/ble-api
 *
 * fromNum  — notify-only: incremented each time a new fromRadio packet is
 *            ready.  The client must subscribe to this characteristic's CCCD
 *            and then read fromRadio after each notification.
 *
 * fromRadio — read-only: returns one ToRadio protobuf per read.  Keep reading
 *             until the response is empty (0 bytes).
 *
 * toRadio  — write-only (write-without-response): client writes a FromRadio
 *            protobuf here to send a packet into the mesh.
 *
 * All three characteristics live under a single service.
 */

// ── Service ───────────────────────────────────────────────────────────────────
// 128-bit UUIDs — verify against firmware source before flashing.
#define MESHTASTIC_SERVICE_UUID     "6ba4b910-bes2-b8aa-cf0e-2ede84ca374e"

// ── Characteristics ───────────────────────────────────────────────────────────
#define MESHTASTIC_TORADIO_UUID     "f75c76d2-129e-4dad-a1dd-7866124401e7"
#define MESHTASTIC_FROMRADIO_UUID   "8ba2bcc2-ee02-4a55-a531-c525c5e454d5"
#define MESHTASTIC_FROMNUM_UUID     "ed9da18c-a800-4f66-a670-aa7547255902"

// ── Packet constraints ────────────────────────────────────────────────────────
// BLE ATT MTU after negotiation is typically 512 bytes on esp-idf.
// Meshtastic limits radio packets to 237 bytes payload; the protobuf wrapper
// adds some overhead.  256 bytes is a safe read buffer for fromRadio.
#define MESHTASTIC_MAX_PACKET_LEN   512

// ── WantConfig handshake ──────────────────────────────────────────────────────
// After connecting the client writes a ToRadio{want_config_id: <nonzero>}.
// The node replies with a stream of FromRadio packets (MyNodeInfo, NodeInfo×N,
// Channel×N, Config, …) terminated by a FromRadio{config_complete_id: <same>}.
// Any nonzero 32-bit value works as the handshake ID.
#define MESHTASTIC_WANT_CONFIG_ID   0xDEADBEEF

// ── Topic suffixes (used by MeshtasticBLEComponent when publishing) ───────────
#define TOPIC_TEXT          "text"
#define TOPIC_POSITION_LAT  "position/latitude"
#define TOPIC_POSITION_LON  "position/longitude"
#define TOPIC_POSITION_ALT  "position/altitude"
#define TOPIC_TEL_BATTERY   "telemetry/battery_level"
#define TOPIC_TEL_VOLTAGE   "telemetry/voltage"
#define TOPIC_TEL_TEMP      "telemetry/temperature"
#define TOPIC_TEL_HUMIDITY  "telemetry/humidity"
#define TOPIC_NODEINFO_NAME "nodeinfo/long_name"
#define TOPIC_NODEINFO_HW   "nodeinfo/hw_model"
#define TOPIC_RAW           "raw"           // base64-encoded raw MeshPacket
#define TOPIC_AVAILABILITY  "status"        // "online" / "offline"
