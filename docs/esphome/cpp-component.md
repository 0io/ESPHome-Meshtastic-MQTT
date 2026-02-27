# ESPHome C++ Component Reference

> Sources: [ESPHome Developer Docs — Architecture](https://developers.esphome.io/architecture/components/),
> [Logging Best Practices](https://developers.esphome.io/architecture/logging/),
> [ESPHome C++ API — Component class](https://api-docs.esphome.io/classesphome_1_1_component),
> [setup_priority namespace](https://api-docs.esphome.io/namespaceesphome_1_1setup__priority),
> [esphome/core/component.h on GitHub](https://github.com/esphome/esphome/blob/dev/esphome/core/component.h)

---

## Required Header

```cpp
#include "esphome/core/component.h"
#include "esphome/core/log.h"
```

---

## Component Base Class

All ESPHome components inherit from `esphome::Component`. Override lifecycle methods as needed.

```cpp
namespace esphome {
namespace my_component {

class MyComponent : public Component {
 public:
  // ── Lifecycle ───────────────────────────────────────────────────────────
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override;

  // ── Setters (called from to_code before setup()) ─────────────────────
  void set_name(const std::string &name) { this->name_ = name; }
  void set_count(uint32_t count) { this->count_ = count; }

 protected:
  std::string name_;
  uint32_t count_{0};
};

}  // namespace my_component
}  // namespace esphome
```

---

## Lifecycle Methods

### `setup()`

Called **once** during startup. Initialize hardware, verify connectivity, call `mark_failed()` if
anything goes wrong.

```cpp
void MyComponent::setup() {
  ESP_LOGCONFIG(TAG, "Setting up MyComponent");
  if (!verify_hardware()) {
    this->mark_failed();
    return;
  }
}
```

- Setter methods from `to_code` are called **before** `setup()`.
- Do not block for more than a few hundred milliseconds — other components are waiting.

### `loop()`

Called at every iteration of the main application loop, approximately every 7 ms. Avoid blocking.

```cpp
void MyComponent::loop() {
  const uint32_t now = millis();
  if (now - this->last_update_ms_ >= this->interval_ms_) {
    this->last_update_ms_ = now;
    this->do_update_();
  }
}
```

**Never** call `delay()` inside `loop()`. Use `millis()`-based timers or `set_timeout()` /
`set_interval()` from the `Component` base class instead.

### `dump_config()`

Called when an API client connects (e.g., Home Assistant). Log all configuration with
`ESP_LOGCONFIG`.

```cpp
void MyComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "MyComponent:");
  ESP_LOGCONFIG(TAG, "  Name:             %s", this->name_.c_str());
  ESP_LOGCONFIG(TAG, "  Reconnect interval: %us", this->reconnect_interval_s_);
  LOG_PIN("  CS Pin: ", this->cs_pin_);    // macro for GPIO pins
  if (this->is_failed()) {
    ESP_LOGE(TAG, "  Setup failed!");
  }
}
```

### `get_setup_priority()`

Return the priority constant appropriate for your component (see table below).

```cpp
float MyComponent::get_setup_priority() const {
  return setup_priority::AFTER_WIFI;   // initialize after WiFi connects
}
```

---

## `setup_priority` Constants

Defined in `esphome::setup_priority`. Higher value = earlier initialization.

| Constant | Value | When to use |
|----------|-------|-------------|
| `setup_priority::BUS` | 1000.0 | I²C, SPI buses |
| `setup_priority::IO` | 900.0 | GPIO expanders (e.g., PCF8574) |
| `setup_priority::HARDWARE` | 800.0 | Direct hardware components |
| `setup_priority::DATA` | 600.0 | Sensors, default |
| `setup_priority::PROCESSOR` | 400.0 | Data processors |
| `setup_priority::BLUETOOTH` | 350.0 | BLE component init |
| `setup_priority::AFTER_BLUETOOTH` | 300.0 | After BLE host init |
| `setup_priority::WIFI` | 250.0 | The WiFi component itself |
| `setup_priority::ETHERNET` | 250.0 | The Ethernet component |
| `setup_priority::BEFORE_CONNECTION` | 220.0 | Before network connects |
| `setup_priority::AFTER_WIFI` | 200.0 | After WiFi, before API/MQTT |
| `setup_priority::AFTER_CONNECTION` | 100.0 | After API/MQTT connects |
| `setup_priority::LATE` | -100.0 | Very last in setup |

`meshtastic_ble` should use `setup_priority::AFTER_WIFI` because BLE scanning can start
immediately but MQTT publishing requires WiFi.

---

## Error Handling

### `mark_failed()`

Signals that the component failed to initialize. After this call, ESPHome will **not** call
`loop()` or further `setup()` steps on the component.

```cpp
void MyComponent::setup() {
  if (!this->init_sensor_()) {
    ESP_LOGE(TAG, "Failed to initialize — check wiring");
    this->mark_failed();
    return;
  }
}
```

### `is_failed()`

Check in `dump_config()` to log a clear error:

```cpp
if (this->is_failed()) {
  ESP_LOGE(TAG, "  Setup failed!");
}
```

---

## Timers

Available on any `Component`:

```cpp
// One-shot timer (fires once after delay_ms)
this->set_timeout("my_timer", delay_ms, [this]() { this->do_thing_(); });

// Repeating timer
this->set_interval("my_interval", interval_ms, [this]() { this->do_thing_(); });

// Cancel
this->cancel_timeout("my_timer");
this->cancel_interval("my_interval");
```

---

## PollingComponent

For components that fetch new data on a configurable interval. The user sets `update_interval:` in
YAML; ESPHome calls `update()` at that rate.

```cpp
class MySensor : public PollingComponent {
 public:
  void setup() override;
  void update() override;   // called at update_interval rate
  float get_setup_priority() const override { return setup_priority::DATA; }
};
```

In `__init__.py`, replace `cg.Component` with `cg.PollingComponent` and use
`cv.polling_component_schema("60s")` in `CONFIG_SCHEMA`:

```python
CONFIG_SCHEMA = cv.polling_component_schema("60s").extend({
    cv.GenerateID(): cv.declare_id(MySensor),
})
```

---

## Logging Macros

Always define a `TAG` at the top of the `.cpp` file:

```cpp
static const char *const TAG = "meshtastic_ble";
```

| Macro | Level | When to use |
|-------|-------|-------------|
| `ESP_LOGVV(TAG, fmt, ...)` | VERY VERBOSE | Raw packet bytes, deep state traces |
| `ESP_LOGV(TAG, fmt, ...)`  | VERBOSE | Per-packet events during development |
| `ESP_LOGD(TAG, fmt, ...)`  | DEBUG | Normal operational events (state changes) |
| `ESP_LOGCONFIG(TAG, fmt, ...)` | CONFIG | Inside `dump_config()` only |
| `ESP_LOGI(TAG, fmt, ...)`  | INFO | User-facing milestones (connected, synced) |
| `ESP_LOGW(TAG, fmt, ...)`  | WARNING | Recoverable issues (retry, decode fail) |
| `ESP_LOGE(TAG, fmt, ...)`  | ERROR | Failures requiring user attention |

### Format specifiers

Standard `printf` format strings work:

```cpp
ESP_LOGI(TAG, "Connected to node 0x%08X (%s)", node_num, node_name);
ESP_LOGD(TAG, "Packet from=0x%08X to=0x%08X id=0x%08X len=%zu",
         pkt.from, pkt.to, pkt.id, len);
ESP_LOGW(TAG, "Decode error: %s", stream.errmsg);
ESP_LOGE(TAG, "Setup failed (err=%d)", rc);
```

### Avoid logging in `loop()`

```cpp
// Bad: spams logs at 142 Hz
void loop() {
  ESP_LOGD(TAG, "state=%d", (int)state_);
}

// Good: log only on state change
void loop() {
  if (state_ != prev_state_) {
    ESP_LOGD(TAG, "State: %d → %d", (int)prev_state_, (int)state_);
    prev_state_ = state_;
  }
}
```

### Per-component log level in YAML

```yaml
logger:
  level: DEBUG
  logs:
    meshtastic_ble: VERBOSE   # override for this TAG
    mqtt.component: WARN      # suppress MQTT noise
```

---

## Complete Component Header Template

```cpp
#pragma once

#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/components/mqtt/mqtt_client.h"

namespace esphome {
namespace meshtastic_ble {

static const char *const TAG = "meshtastic_ble";

class MeshtasticBLEComponent : public Component {
 public:
  // ── ESPHome lifecycle ──────────────────────────────────────────────────
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override {
    return setup_priority::AFTER_WIFI;
  }

  // ── Setters called from to_code ────────────────────────────────────────
  void set_node_name(const std::string &name) { node_name_ = name; }
  void set_node_mac(uint64_t mac) { node_mac_ = mac; use_mac_ = true; }
  void set_topic_prefix(const std::string &prefix) { topic_prefix_ = prefix; }
  void set_reconnect_interval(uint32_t seconds) { reconnect_interval_s_ = seconds; }

 private:
  std::string node_name_;
  uint64_t node_mac_{0};
  bool use_mac_{false};
  std::string topic_prefix_{"meshtastic"};
  uint32_t reconnect_interval_s_{30};

  uint32_t last_connect_ms_{0};
};

}  // namespace meshtastic_ble
}  // namespace esphome
```

---

## References

- [ESPHome C++ API — Component class](https://api-docs.esphome.io/classesphome_1_1_component)
- [ESPHome setup_priority namespace](https://api-docs.esphome.io/namespaceesphome_1_1setup__priority)
- [ESPHome Developer Docs — Logging best practices](https://developers.esphome.io/architecture/logging/)
- [ESPHome Developer Docs — Architecture](https://developers.esphome.io/architecture/components/)
- [esphome/core/component.h on GitHub](https://github.com/esphome/esphome/blob/dev/esphome/core/component.h)
