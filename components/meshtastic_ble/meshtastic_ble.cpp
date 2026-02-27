#include "meshtastic_ble.h"

#include "esphome/core/log.h"
#include "esphome/core/application.h"

// NimBLE host
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/ble_gattc.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"

namespace esphome {
namespace meshtastic_ble {

// ── ESPHome lifecycle ─────────────────────────────────────────────────────────

void MeshtasticBLEComponent::setup() {
    ESP_LOGI(TAG, "Setting up Meshtastic BLE gateway");
    ESP_LOGI(TAG, "  Node name   : %s", node_name_.c_str());
    ESP_LOGI(TAG, "  Topic prefix: %s", topic_prefix_.c_str());

    // TODO: initialise NimBLE host and GAP callbacks
    //   nimble_port_init();
    //   ble_hs_cfg.sync_cb = on_sync_;
    //   nimble_port_freertos_init(nimble_host_task_);

    publish_availability_(false);
}

void MeshtasticBLEComponent::loop() {
    const uint32_t now = millis();

    switch (state_) {
        case GatewayState::IDLE:
            if (now - last_connect_attempt_ms_ >= reconnect_interval_s_ * 1000U) {
                last_connect_attempt_ms_ = now;
                start_scan_();
            }
            break;

        case GatewayState::WANT_CONFIG:
        case GatewayState::SYNCING:
        case GatewayState::READY:
            // fromRadio drain: handle_from_radio_() sets this flag when a
            // packet was decoded so more may be queued.  We issue the next
            // read here, outside the NimBLE callback context.
            if (pending_fromradio_read_) {
                pending_fromradio_read_ = false;
                read_fromradio_();
            }
            break;

        default:
            // All other states are driven by BLE callbacks.
            break;
    }
}

void MeshtasticBLEComponent::dump_config() {
    ESP_LOGCONFIG(TAG, "Meshtastic BLE Gateway:");
    ESP_LOGCONFIG(TAG, "  Node name        : %s", node_name_.c_str());
    if (use_mac_) {
        ESP_LOGCONFIG(TAG, "  Node MAC         : %012llX", node_mac_);
    }
    ESP_LOGCONFIG(TAG, "  MQTT prefix      : %s", topic_prefix_.c_str());
    ESP_LOGCONFIG(TAG, "  Reconnect interval: %us", reconnect_interval_s_);
}

// ── BLE scanning & connecting ─────────────────────────────────────────────────

void MeshtasticBLEComponent::start_scan_() {
    ESP_LOGI(TAG, "Starting BLE scan for '%s'", node_name_.c_str());
    state_ = GatewayState::SCANNING;

    // TODO: configure and start a passive GAP scan
    //   struct ble_gap_disc_params disc_params = {};
    //   disc_params.passive = 1;
    //   disc_params.itvl    = BLE_GAP_SCAN_ITVL_MS(200);
    //   disc_params.window  = BLE_GAP_SCAN_WIN_MS(150);
    //   ble_gap_disc(own_addr_type, BLE_HS_FOREVER, &disc_params, on_gap_event_, this);
}

void MeshtasticBLEComponent::connect_(const ble_addr_t &addr) {
    ESP_LOGI(TAG, "Connecting to Meshtastic node...");
    state_ = GatewayState::CONNECTING;
    peer_addr_ = addr;

    // TODO: stop scan, then initiate connection
    //   ble_gap_disc_cancel();
    //   ble_gap_connect(own_addr_type, &addr, 5000, nullptr, on_gap_event_, this);
}

// ── GATT discovery ────────────────────────────────────────────────────────────

void MeshtasticBLEComponent::discover_services_() {
    ESP_LOGI(TAG, "Discovering GATT services");
    state_ = GatewayState::DISCOVERING;

    // TODO: discover the Meshtastic service by UUID
    //   ble_uuid128_t svc_uuid = ...;
    //   ble_gattc_disc_svc_by_uuid(conn_handle_, &svc_uuid.u, on_disc_complete_, this);
}

void MeshtasticBLEComponent::subscribe_fromnum_() {
    ESP_LOGI(TAG, "Subscribing to fromNum notifications");

    // TODO: write 0x0001 to the fromNum CCCD to enable notifications
    //   uint8_t val[2] = {0x01, 0x00};
    //   ble_gattc_write_flat(conn_handle_, fromnum_cccd_handle_, val, sizeof(val), nullptr, nullptr);
}

// ── WantConfig handshake ──────────────────────────────────────────────────────

void MeshtasticBLEComponent::send_want_config_() {
    ESP_LOGI(TAG, "Sending WantConfig (id=0x%08X)", want_config_id_);
    state_ = GatewayState::WANT_CONFIG;

    // TODO: encode ToRadio{want_config_id: want_config_id_} with nanopb
    //   meshtastic_ToRadio to_radio = meshtastic_ToRadio_init_zero;
    //   to_radio.which_payload_variant = meshtastic_ToRadio_want_config_id_tag;
    //   to_radio.payload_variant.want_config_id = want_config_id_;
    //
    //   uint8_t buf[64];
    //   pb_ostream_t stream = pb_ostream_from_buffer(buf, sizeof(buf));
    //   pb_encode(&stream, meshtastic_ToRadio_fields, &to_radio);
    //
    //   ble_gattc_write_flat(conn_handle_, toradio_handle_, buf, stream.bytes_written, nullptr, nullptr);
}

// ── fromRadio read loop ───────────────────────────────────────────────────────

void MeshtasticBLEComponent::read_fromradio_() {
    // Called after each fromNum notification.  Must loop until server returns
    // an empty (0-byte) response.

    // TODO:
    //   ble_gattc_read(conn_handle_, fromradio_handle_, on_fromradio_read_, this);
}

// ── Packet handling ───────────────────────────────────────────────────────────

void MeshtasticBLEComponent::handle_from_radio_(const uint8_t *data, size_t len) {
    if (len == 0) {
        // Empty response — fromRadio drain complete.
        return;
    }

    meshtastic_FromRadio from_radio = meshtastic_FromRadio_init_zero;
    pb_istream_t stream = pb_istream_from_buffer(data, len);

    if (!pb_decode(&stream, meshtastic_FromRadio_fields, &from_radio)) {
        ESP_LOGW(TAG, "Failed to decode FromRadio: %s", stream.errmsg);
        return;
    }

    switch (from_radio.which_payload_variant) {
        case meshtastic_FromRadio_packet_tag:
            handle_mesh_packet_(from_radio.payload_variant.packet);
            break;
        case meshtastic_FromRadio_my_info_tag:
            handle_my_node_info_(from_radio.payload_variant.my_info);
            break;
        case meshtastic_FromRadio_node_info_tag:
            handle_node_info_(from_radio.payload_variant.node_info);
            break;
        case meshtastic_FromRadio_config_complete_id_tag:
            handle_config_complete_(from_radio.payload_variant.config_complete_id);
            break;
        default:
            ESP_LOGD(TAG, "Unhandled FromRadio variant: %d", from_radio.which_payload_variant);
            break;
    }

    // Signal that more packets may be waiting.  loop() will issue the next
    // ble_gattc_read() from outside this callback context, which avoids nested
    // GATTC calls that can deadlock NimBLE on some esp-idf versions.
    pending_fromradio_read_ = true;
}

void MeshtasticBLEComponent::handle_mesh_packet_(const meshtastic_MeshPacket &pkt) {
    if (is_duplicate_(pkt.id)) {
        ESP_LOGD(TAG, "Dropping duplicate packet id=0x%08X", pkt.id);
        return;
    }

    ESP_LOGD(TAG, "MeshPacket from=0x%08X id=0x%08X", pkt.from, pkt.id);

    // TODO: decode pkt.decoded (meshtastic_Data) and route by portnum
    //
    // switch (pkt.decoded.portnum) {
    //     case meshtastic_PortNum_TEXT_MESSAGE_APP:
    //         publish_(node_topic_(pkt.from, TOPIC_TEXT),
    //                  std::string((char *)pkt.decoded.payload.bytes, pkt.decoded.payload.size));
    //         break;
    //     case meshtastic_PortNum_TELEMETRY_APP:
    //         // decode meshtastic_Telemetry, publish sub-fields
    //         break;
    //     case meshtastic_PortNum_POSITION_APP:
    //         // decode meshtastic_Position, publish lat/lon/alt
    //         break;
    //     case meshtastic_PortNum_NODEINFO_APP:
    //         // decode meshtastic_User, publish long_name/hw_model
    //         break;
    //     default:
    //         break;
    // }
}

void MeshtasticBLEComponent::handle_my_node_info_(const meshtastic_MyNodeInfo &info) {
    my_node_num_ = info.my_node_num;
    ESP_LOGI(TAG, "My node number: 0x%08X", my_node_num_);
}

void MeshtasticBLEComponent::handle_node_info_(const meshtastic_NodeInfo &info) {
    ESP_LOGD(TAG, "NodeInfo: num=0x%08X name=%s", info.num, info.user.long_name);
    // TODO: store in node table, publish discovery payload
}

void MeshtasticBLEComponent::handle_config_complete_(uint32_t config_id) {
    if (config_id != want_config_id_) {
        ESP_LOGW(TAG, "config_complete_id mismatch (got 0x%08X, expected 0x%08X)",
                 config_id, want_config_id_);
        return;
    }
    ESP_LOGI(TAG, "Config sync complete — gateway is READY");
    state_ = GatewayState::READY;
    config_complete_ = true;
    publish_availability_(true);
}

// ── Deduplication ─────────────────────────────────────────────────────────────

bool MeshtasticBLEComponent::is_duplicate_(uint32_t packet_id) {
    for (size_t i = 0; i < DEDUP_SIZE; i++) {
        if (seen_ids_[i] == packet_id && packet_id != 0) return true;
    }
    seen_ids_[seen_idx_] = packet_id;
    seen_idx_ = (seen_idx_ + 1) % DEDUP_SIZE;
    return false;
}

// ── MQTT helpers ──────────────────────────────────────────────────────────────

void MeshtasticBLEComponent::publish_(const std::string &subtopic,
                                       const std::string &payload,
                                       bool retain) {
    if (mqtt::global_mqtt_client == nullptr || !mqtt::global_mqtt_client->is_connected()) {
        ESP_LOGV(TAG, "MQTT not ready, dropping: %s", subtopic.c_str());
        return;
    }
    const std::string full_topic = topic_prefix_ + "/" + subtopic;
    mqtt::global_mqtt_client->publish(full_topic, payload, 0, retain);
}

void MeshtasticBLEComponent::publish_availability_(bool online) {
    publish_("gateway/" TOPIC_AVAILABILITY, online ? "online" : "offline", true);
}

std::string MeshtasticBLEComponent::node_topic_(uint32_t node_num, const char *suffix) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%08X/%s", node_num, suffix);
    return std::string(buf);
}

// ── Static GAP event trampoline ───────────────────────────────────────────────

int MeshtasticBLEComponent::on_gap_event_(struct ble_gap_event *event, void *arg) {
    auto *self = static_cast<MeshtasticBLEComponent *>(arg);

    switch (event->type) {
        case BLE_GAP_EVENT_DISC:
            // TODO: check advertised name/MAC, call self->connect_() if matched
            break;

        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status == 0) {
                ESP_LOGI(TAG, "BLE connected (conn_handle=%d)", event->connect.conn_handle);
                self->conn_handle_ = event->connect.conn_handle;
                self->discover_services_();
            } else {
                ESP_LOGW(TAG, "BLE connect failed (status=%d)", event->connect.status);
                self->state_ = GatewayState::IDLE;
            }
            break;

        case BLE_GAP_EVENT_DISCONNECT:
            ESP_LOGW(TAG, "BLE disconnected (reason=%d)", event->disconnect.reason);
            self->conn_handle_ = BLE_HS_CONN_HANDLE_NONE;
            self->state_ = GatewayState::IDLE;
            self->config_complete_ = false;
            self->publish_availability_(false);
            break;

        case BLE_GAP_EVENT_NOTIFY_RX:
            if (event->notify_rx.attr_handle == self->fromnum_handle_) {
                ESP_LOGD(TAG, "fromNum notify — reading fromRadio");
                self->read_fromradio_();
            }
            break;

        default:
            break;
    }
    return 0;
}

}  // namespace meshtastic_ble
}  // namespace esphome
