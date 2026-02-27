#pragma once

#include <string>
#include <cstdint>
#include <functional>

#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/components/mqtt/mqtt_client.h"

// NimBLE (available via esp-idf)
#include "host/ble_hs.h"
#include "host/ble_gattc.h"
#include "nimble/nimble_port.h"

#include "gatt_defs.h"

// nanopb + generated Meshtastic proto headers (produced by scripts/gen_proto.sh)
#include "proto/meshtastic/mesh.pb.h"
#include "proto/meshtastic/telemetry.pb.h"
#include "pb_decode.h"
#include "pb_encode.h"

namespace esphome {
namespace meshtastic_ble {

static const char *const TAG = "meshtastic_ble";

// ── Connection state machine ──────────────────────────────────────────────────
enum class GatewayState : uint8_t {
    IDLE,           // Not scanning, waiting for next attempt
    SCANNING,       // BLE scan in progress
    CONNECTING,     // GAP connect issued, waiting for connection event
    DISCOVERING,    // GATT service / characteristic discovery in progress
    WANT_CONFIG,    // WantConfig ToRadio written, waiting for config stream
    SYNCING,        // Receiving NodeInfo / channel / config packets
    READY,          // Fully synced, forwarding live packets
    DISCONNECTING,  // Intentional disconnect in progress
};

// ── Per-node state ────────────────────────────────────────────────────────────
struct NodeEntry {
    uint32_t num;
    char long_name[40];
    char short_name[5];
    uint8_t hw_model;
    int32_t latitude_i;   // degrees × 1e7
    int32_t longitude_i;
    int32_t altitude;
    uint32_t last_heard;  // Unix timestamp from node
};

// ── Component ─────────────────────────────────────────────────────────────────
class MeshtasticBLEComponent : public Component {
   public:
    // ── ESPHome lifecycle ─────────────────────────────────────────────────────
    void setup() override;
    void loop() override;
    void dump_config() override;
    float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }

    // ── Config setters (called from __init__.py code-gen) ─────────────────────
    void set_node_name(const std::string &name) { node_name_ = name; }
    void set_node_mac(uint64_t mac) { node_mac_ = mac; use_mac_ = true; }
    void set_topic_prefix(const std::string &prefix) { topic_prefix_ = prefix; }
    void set_reconnect_interval(uint32_t seconds) { reconnect_interval_s_ = seconds; }

   private:
    // ── Config ────────────────────────────────────────────────────────────────
    std::string node_name_;
    uint64_t node_mac_{0};
    bool use_mac_{false};
    std::string topic_prefix_;
    uint32_t reconnect_interval_s_{30};

    // ── BLE state ─────────────────────────────────────────────────────────────
    GatewayState state_{GatewayState::IDLE};
    uint16_t conn_handle_{BLE_HS_CONN_HANDLE_NONE};
    ble_addr_t peer_addr_{};

    // Discovered GATT handles (populated during DISCOVERING state)
    uint16_t svc_start_handle_{0};
    uint16_t svc_end_handle_{0};
    uint16_t toradio_handle_{0};
    uint16_t fromradio_handle_{0};
    uint16_t fromnum_handle_{0};
    uint16_t fromnum_cccd_handle_{0};

    // ── Session state ─────────────────────────────────────────────────────────
    uint32_t my_node_num_{0};
    uint32_t want_config_id_{MESHTASTIC_WANT_CONFIG_ID};
    bool config_complete_{false};

    // Seen packet IDs for deduplication (ring buffer, last 64 IDs)
    static constexpr size_t DEDUP_SIZE = 64;
    uint32_t seen_ids_[DEDUP_SIZE]{};
    size_t seen_idx_{0};

    // ── Timing ────────────────────────────────────────────────────────────────
    uint32_t last_connect_attempt_ms_{0};

    // Set to true by handle_from_radio_() when a non-empty packet was decoded,
    // signalling that more fromRadio packets may be queued on the node.
    // Consumed by loop() which issues the next ble_gattc_read() from outside
    // the NimBLE callback context, avoiding nested GATTC calls.
    bool pending_fromradio_read_{false};

    // ── NimBLE host lifecycle (static — no instance pointer available yet) ───
    // Called by NimBLE when the host stack has finished initialising and is
    // ready to accept GAP/GATTC calls.  Triggers the first BLE scan.
    static void on_sync_();
    // Called when the NimBLE host resets (e.g. controller watchdog timeout).
    // Transitions state to IDLE so loop() will re-scan after the backoff period.
    static void on_reset_(int reason);
    // FreeRTOS task that runs the NimBLE event loop.  Blocks until
    // nimble_port_stop() is called (which we never do in normal operation).
    static void nimble_host_task_(void *param);

    // ── BLE callbacks (static trampolines required by NimBLE C API) ──────────
    static int on_gap_event_(struct ble_gap_event *event, void *arg);
    static int on_disc_complete_(uint16_t conn_handle, const struct ble_gatt_error *error,
                                  const struct ble_gatt_svc *service, void *arg);
    static int on_chr_discovered_(uint16_t conn_handle, const struct ble_gatt_error *error,
                                   const struct ble_gatt_chr *chr, void *arg);
    static int on_desc_discovered_(uint16_t conn_handle, const struct ble_gatt_error *error,
                                    uint16_t chr_val_handle,
                                    const struct ble_gatt_dsc *dsc, void *arg);
    static int on_notify_(uint16_t conn_handle, const struct ble_gatt_error *error,
                           struct ble_gatt_attr *attr, void *arg);
    static int on_fromradio_read_(uint16_t conn_handle, const struct ble_gatt_error *error,
                                   struct ble_gatt_attr *attr, void *arg);

    // ── Internal methods ──────────────────────────────────────────────────────
    void start_scan_();
    void connect_(const ble_addr_t &addr);
    void discover_services_();
    void subscribe_fromnum_();
    void send_want_config_();
    void read_fromradio_();

    void handle_from_radio_(const uint8_t *data, size_t len);
    void handle_mesh_packet_(const meshtastic_MeshPacket &pkt);
    void handle_my_node_info_(const meshtastic_MyNodeInfo &info);
    void handle_node_info_(const meshtastic_NodeInfo &info);
    void handle_config_complete_(uint32_t config_id);

    bool is_duplicate_(uint32_t packet_id);

    void publish_(const std::string &subtopic, const std::string &payload, bool retain = false);
    void publish_availability_(bool online);
    std::string node_topic_(uint32_t node_num, const char *suffix);
};

}  // namespace meshtastic_ble
}  // namespace esphome
