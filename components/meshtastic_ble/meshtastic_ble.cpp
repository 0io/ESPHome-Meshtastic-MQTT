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

// Module-level pointer used by the static NimBLE callbacks (on_sync_,
// on_reset_) which receive no user-data argument from the NimBLE API.
// Safe because ESPHome creates exactly one instance of this component.
static MeshtasticBLEComponent *s_instance = nullptr;

// ── ESPHome lifecycle ─────────────────────────────────────────────────────────

void MeshtasticBLEComponent::setup() {
    ESP_LOGI(TAG, "Setting up Meshtastic BLE gateway");
    ESP_LOGI(TAG, "  Node name   : %s", node_name_.c_str());
    ESP_LOGI(TAG, "  Topic prefix: %s", topic_prefix_.c_str());

    // Stash the instance pointer for use by static NimBLE callbacks.
    s_instance = this;

    // Publish offline availability immediately so HA marks the gateway
    // unavailable until BLE sync completes and we flip it to online.
    publish_availability_(false);

    // ── NimBLE host initialisation ──────────────────────────────────────────
    // nimble_port_init() prepares the NimBLE controller and host layers.
    // It must be called before any ble_* API calls.
    int rc = nimble_port_init();
    if (rc != 0) {
        ESP_LOGE(TAG, "nimble_port_init failed (rc=%d) — BLE unavailable", rc);
        this->mark_failed();
        return;
    }

    // Register the sync and reset callbacks.
    // on_sync_ fires once the host has exchanged LE features with the
    // controller and is ready for GAP/GATTC operations.
    ble_hs_cfg.sync_cb  = on_sync_;
    // on_reset_ fires if the controller resets unexpectedly (e.g. watchdog).
    ble_hs_cfg.reset_cb = on_reset_;

    // Initialise the GAP service (sets device name, appearance, etc.).
    ble_svc_gap_init();

    // Start the NimBLE host task on core 1 (dual-core) or the only core.
    // nimble_port_freertos_init() creates a FreeRTOS task that runs
    // nimble_port_run(), blocking until nimble_port_stop() is called.
    nimble_port_freertos_init(nimble_host_task_);

    ESP_LOGI(TAG, "NimBLE host task started — waiting for sync");
}

// ── NimBLE host lifecycle callbacks ──────────────────────────────────────────

void MeshtasticBLEComponent::on_sync_() {
    // The NimBLE host has synchronised with the controller and is ready.
    // Infer the best available own address type (public or random) and
    // kick off the first BLE scan.
    ESP_LOGI(TAG, "NimBLE host synced");

    int rc = ble_hs_util_ensure_addr(0);  // 0 = prefer public address
    if (rc != 0) {
        ESP_LOGW(TAG, "ble_hs_util_ensure_addr failed (rc=%d), using random", rc);
    }

    if (s_instance != nullptr) {
        // Trigger the first scan immediately rather than waiting for the
        // reconnect_interval_ timer to fire.
        s_instance->last_connect_attempt_ms_ = 0;
        s_instance->state_ = GatewayState::IDLE;
    }
}

void MeshtasticBLEComponent::on_reset_(int reason) {
    // The controller reset — all existing connections are gone.
    ESP_LOGW(TAG, "NimBLE host reset (reason=%d)", reason);
    if (s_instance != nullptr) {
        s_instance->conn_handle_      = BLE_HS_CONN_HANDLE_NONE;
        s_instance->state_            = GatewayState::IDLE;
        s_instance->config_complete_  = false;
        s_instance->pending_fromradio_read_ = false;
        // publish_availability_ calls into MQTT — safe to call here because
        // this callback runs in the NimBLE host task, not an ISR.
        s_instance->publish_availability_(false);
    }
}

void MeshtasticBLEComponent::nimble_host_task_(void *param) {
    ESP_LOGI(TAG, "NimBLE host task running");
    // nimble_port_run() blocks, processing NimBLE events until
    // nimble_port_stop() is called.  In normal operation this task runs
    // forever alongside the ESPHome loop task.
    nimble_port_run();
    // Reached only if nimble_port_stop() is called (e.g. during shutdown).
    nimble_port_freertos_deinit();
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

    // Resolve the best available own address type (public preferred).
    int rc = ble_hs_id_infer_auto(0, &own_addr_type_);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_hs_id_infer_auto failed (rc=%d)", rc);
        state_ = GatewayState::IDLE;
        return;
    }

    struct ble_gap_disc_params disc_params = {};
    disc_params.passive     = 1;   // passive scan — no scan requests sent
    disc_params.filter_dups = 1;   // suppress duplicate advertising reports
    disc_params.itvl        = BLE_GAP_SCAN_ITVL_MS(200);
    disc_params.window      = BLE_GAP_SCAN_WIN_MS(150);

    // BLE_HS_FOREVER: scan until we find the device and call ble_gap_disc_cancel().
    rc = ble_gap_disc(own_addr_type_, BLE_HS_FOREVER, &disc_params, on_gap_event_, this);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_disc failed (rc=%d)", rc);
        state_ = GatewayState::IDLE;
    }
}

void MeshtasticBLEComponent::connect_(const ble_addr_t &addr) {
    ESP_LOGI(TAG, "Connecting to Meshtastic node...");
    state_ = GatewayState::CONNECTING;
    peer_addr_ = addr;

    // Cancel the scan before initiating a connection (NimBLE requires this).
    // The resulting BLE_GAP_EVENT_DISC_COMPLETE is harmless — state_ is already
    // CONNECTING so the SCANNING guard in that handler won't fire.
    ble_gap_disc_cancel();

    // nullptr for conn_params uses NimBLE defaults (suitable for most nodes).
    // 5000 ms timeout: if the connection isn't established in 5 s, NimBLE fires
    // BLE_GAP_EVENT_CONNECT with a non-zero status and we fall back to IDLE.
    int rc = ble_gap_connect(own_addr_type_, &peer_addr_, 5000,
                             nullptr, on_gap_event_, this);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_connect failed (rc=%d)", rc);
        state_ = GatewayState::IDLE;
    }
}

// ── GATT discovery ────────────────────────────────────────────────────────────

void MeshtasticBLEComponent::discover_services_() {
    ESP_LOGI(TAG, "Discovering GATT services");
    state_ = GatewayState::DISCOVERING;

    int rc = ble_gattc_disc_svc_by_uuid(conn_handle_, &MESH_SVC_UUID.u,
                                         on_disc_complete_, this);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gattc_disc_svc_by_uuid failed (rc=%d)", rc);
        ble_gap_terminate(conn_handle_, BLE_ERR_REM_USER_CONN_TERM);
    }
}

void MeshtasticBLEComponent::subscribe_fromnum_() {
    ESP_LOGI(TAG, "Subscribing to fromNum notifications");

    // ATT CCCD value: 0x0001 = enable notifications (little-endian uint16).
    static const uint8_t cccd_notify[2] = {0x01, 0x00};

    // Write the CCCD using a Write Request (ATT_WRITE_REQ).  on_notify_() is
    // called with the ATT Write Response, then triggers send_want_config_().
    int rc = ble_gattc_write_flat(conn_handle_, fromnum_cccd_handle_,
                                   cccd_notify, sizeof(cccd_notify),
                                   on_notify_, this);
    if (rc != 0) {
        ESP_LOGE(TAG, "CCCD write request failed (rc=%d)", rc);
        ble_gap_terminate(conn_handle_, BLE_ERR_REM_USER_CONN_TERM);
    }
}

// ── WantConfig handshake ──────────────────────────────────────────────────────

void MeshtasticBLEComponent::send_want_config_() {
    ESP_LOGI(TAG, "Sending WantConfig (id=0x%08X)", want_config_id_);
    state_ = GatewayState::WANT_CONFIG;

    // Encode ToRadio{want_config_id: N} with nanopb.
    // A WantConfig payload is a single varint field — 32 bytes is ample.
    meshtastic_ToRadio to_radio = meshtastic_ToRadio_init_zero;
    to_radio.which_payload_variant = meshtastic_ToRadio_want_config_id_tag;
    to_radio.payload_variant.want_config_id = want_config_id_;

    uint8_t buf[32];
    pb_ostream_t stream = pb_ostream_from_buffer(buf, sizeof(buf));
    if (!pb_encode(&stream, meshtastic_ToRadio_fields, &to_radio)) {
        ESP_LOGE(TAG, "Failed to encode WantConfig: %s", stream.errmsg);
        return;
    }

    // toRadio has the WRITE property (not WRITE_WITHOUT_RESPONSE), so use an
    // ATT Write Request.  We don't need the write response so pass nullptr cb.
    int rc = ble_gattc_write_flat(conn_handle_, toradio_handle_,
                                   buf, stream.bytes_written,
                                   nullptr, nullptr);
    if (rc != 0) {
        ESP_LOGE(TAG, "toRadio write (WantConfig) failed (rc=%d)", rc);
    }
    // The node will respond with a stream of FromRadio packets: MyNodeInfo,
    // NodeInfo×N, Channel×C, Config×C, then ConfigComplete.  Each packet
    // increments fromNum and fires a BLE_GAP_EVENT_NOTIFY_RX which drives
    // read_fromradio_() via the pending_fromradio_read_ flag in loop().
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
        case BLE_GAP_EVENT_DISC: {
            // Only act on discovery events while we are still scanning.
            if (self->state_ != GatewayState::SCANNING) break;

            const struct ble_gap_disc_desc *disc = &event->disc;

            if (self->use_mac_) {
                // Match by MAC address.  node_mac_ is big-endian (AA…FF for
                // "AA:BB:CC:DD:EE:FF"), BLE val[] is little-endian (FF…AA).
                uint8_t mac_le[6];
                uint64_t m = self->node_mac_;
                for (int i = 0; i < 6; i++) {
                    mac_le[i] = m & 0xFF;
                    m >>= 8;
                }
                if (memcmp(disc->addr.val, mac_le, 6) == 0) {
                    ESP_LOGI(TAG, "Matched Meshtastic node by MAC");
                    self->connect_(disc->addr);
                }
                break;
            }

            // Match by advertised device name (substring in either direction).
            struct ble_hs_adv_fields fields;
            if (ble_hs_adv_parse_fields(&fields, disc->data, disc->length_data) != 0) break;
            if (fields.name == nullptr || fields.name_len == 0) break;

            const std::string adv_name(reinterpret_cast<const char *>(fields.name),
                                       fields.name_len);
            if (adv_name.find(self->node_name_) != std::string::npos ||
                self->node_name_.find(adv_name) != std::string::npos) {
                ESP_LOGI(TAG, "Matched Meshtastic node by name: %s", adv_name.c_str());
                self->connect_(disc->addr);
            }
            break;
        }

        case BLE_GAP_EVENT_DISC_COMPLETE:
            // Fired when the scan window expires or is cancelled by connect_().
            // If we are still scanning (device not found) fall back to IDLE.
            if (self->state_ == GatewayState::SCANNING) {
                ESP_LOGW(TAG, "BLE scan complete — device not found");
                self->state_ = GatewayState::IDLE;
            }
            break;

        case BLE_GAP_EVENT_MTU:
            ESP_LOGI(TAG, "MTU negotiated: conn=%d mtu=%d",
                     event->mtu.conn_handle, event->mtu.value);
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

// ── GATT discovery callbacks ──────────────────────────────────────────────────

// Called once for each matching service and then once more with service == nullptr
// (BLE_HS_EDONE) to signal completion.
int MeshtasticBLEComponent::on_disc_complete_(uint16_t conn_handle,
                                               const struct ble_gatt_error *error,
                                               const struct ble_gatt_svc *service,
                                               void *arg) {
    auto *self = static_cast<MeshtasticBLEComponent *>(arg);

    if (error->status != 0 && error->status != BLE_HS_EDONE) {
        ESP_LOGE(TAG, "Service discovery error (status=%d)", error->status);
        ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        return 0;
    }

    if (service != nullptr) {
        // Meshtastic service found — record its attribute handle range.
        self->svc_start_handle_ = service->start_handle;
        self->svc_end_handle_   = service->end_handle;
        ESP_LOGI(TAG, "Meshtastic service found (handles %d–%d)",
                 service->start_handle, service->end_handle);
        return 0;
    }

    // service == nullptr: discovery complete (BLE_HS_EDONE).
    if (self->svc_start_handle_ == 0) {
        ESP_LOGE(TAG, "Meshtastic GATT service not found — disconnecting");
        ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        return 0;
    }

    // Discover all characteristics within the Meshtastic service.
    int rc = ble_gattc_disc_all_chrs(conn_handle,
                                      self->svc_start_handle_,
                                      self->svc_end_handle_,
                                      on_chr_discovered_, self);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gattc_disc_all_chrs failed (rc=%d)", rc);
        ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }
    return 0;
}

// Called once per characteristic and then once more with chr == nullptr (BLE_HS_EDONE).
int MeshtasticBLEComponent::on_chr_discovered_(uint16_t conn_handle,
                                                const struct ble_gatt_error *error,
                                                const struct ble_gatt_chr *chr,
                                                void *arg) {
    auto *self = static_cast<MeshtasticBLEComponent *>(arg);

    if (error->status != 0 && error->status != BLE_HS_EDONE) {
        ESP_LOGE(TAG, "Characteristic discovery error (status=%d)", error->status);
        ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        return 0;
    }

    if (chr != nullptr) {
        // Match each discovered characteristic by UUID and store its value handle.
        if (ble_uuid_cmp(&chr->uuid.u, &TORADIO_CHR_UUID.u) == 0) {
            self->toradio_handle_ = chr->val_handle;
            ESP_LOGI(TAG, "toRadio characteristic handle: %d", self->toradio_handle_);
        } else if (ble_uuid_cmp(&chr->uuid.u, &FROMRADIO_CHR_UUID.u) == 0) {
            self->fromradio_handle_ = chr->val_handle;
            ESP_LOGI(TAG, "fromRadio characteristic handle: %d", self->fromradio_handle_);
        } else if (ble_uuid_cmp(&chr->uuid.u, &FROMNUM_CHR_UUID.u) == 0) {
            self->fromnum_handle_ = chr->val_handle;
            ESP_LOGI(TAG, "fromNum characteristic handle: %d", self->fromnum_handle_);
        }
        return 0;
    }

    // chr == nullptr: all characteristics have been reported (BLE_HS_EDONE).
    if (self->toradio_handle_ == 0 || self->fromradio_handle_ == 0 ||
        self->fromnum_handle_ == 0) {
        ESP_LOGE(TAG, "One or more required characteristics not found");
        ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        return 0;
    }

    // Discover descriptors for the fromNum characteristic to locate its CCCD.
    // The CCCD descriptor for fromNum lies between fromnum_handle_ and
    // svc_end_handle_ — using the full service range is safe.
    int rc = ble_gattc_disc_all_dscs(conn_handle,
                                      self->fromnum_handle_,
                                      self->svc_end_handle_,
                                      on_desc_discovered_, self);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gattc_disc_all_dscs failed (rc=%d)", rc);
        ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }
    return 0;
}

// Called once per descriptor and then once more with dsc == nullptr (BLE_HS_EDONE).
int MeshtasticBLEComponent::on_desc_discovered_(uint16_t conn_handle,
                                                  const struct ble_gatt_error *error,
                                                  uint16_t chr_val_handle,
                                                  const struct ble_gatt_dsc *dsc,
                                                  void *arg) {
    auto *self = static_cast<MeshtasticBLEComponent *>(arg);

    if (error->status != 0 && error->status != BLE_HS_EDONE) {
        ESP_LOGE(TAG, "Descriptor discovery error (status=%d)", error->status);
        ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        return 0;
    }

    if (dsc != nullptr) {
        // Look for the standard CCCD descriptor (0x2902).
        if (ble_uuid_cmp(&dsc->uuid.u, &CCCD_UUID.u) == 0) {
            self->fromnum_cccd_handle_ = dsc->handle;
            ESP_LOGI(TAG, "fromNum CCCD handle: %d", self->fromnum_cccd_handle_);
        }
        return 0;
    }

    // dsc == nullptr: all descriptors have been reported (BLE_HS_EDONE).
    if (self->fromnum_cccd_handle_ == 0) {
        ESP_LOGE(TAG, "fromNum CCCD not found — cannot subscribe to notifications");
        ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        return 0;
    }

    // All required handles discovered.  Subscribe to fromNum notifications, then
    // send WantConfig to kick off the config sync stream.
    self->subscribe_fromnum_();
    return 0;
}

// ATT Write Response callback for the CCCD write issued by subscribe_fromnum_().
// Called by NimBLE when the peer acknowledges the Write Request.
int MeshtasticBLEComponent::on_notify_(uint16_t conn_handle,
                                        const struct ble_gatt_error *error,
                                        struct ble_gatt_attr *attr,
                                        void *arg) {
    auto *self = static_cast<MeshtasticBLEComponent *>(arg);

    if (error->status != 0) {
        ESP_LOGE(TAG, "CCCD write (notify enable) failed (status=%d)", error->status);
        ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        return 0;
    }

    ESP_LOGI(TAG, "fromNum notifications enabled — sending WantConfig");
    self->send_want_config_();
    return 0;
}

}  // namespace meshtastic_ble
}  // namespace esphome
