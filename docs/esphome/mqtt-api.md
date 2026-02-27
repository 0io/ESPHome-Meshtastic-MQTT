# ESPHome MQTT C++ API

> Sources: [ESPHome MQTT docs](https://esphome.io/components/mqtt/),
> [CustomMQTTDevice class reference](https://api-docs.esphome.io/classesphome_1_1mqtt_1_1_custom_m_q_t_t_device),
> [MQTTClientComponent class reference](https://api-docs.esphome.io/classesphome_1_1mqtt_1_1_m_q_t_t_client_component)

---

## Required Includes

```cpp
// Preferred — use CustomMQTTDevice as a base class
#include "esphome/components/mqtt/custom_mqtt_device.h"

// Alternative — access the global client directly
#include "esphome/components/mqtt/mqtt_client.h"
```

In `__init__.py`, declare `mqtt` as an `AUTO_LOAD` dependency so ESPHome always compiles it in:

```python
AUTO_LOAD = ["mqtt"]
```

---

## Option 1: Inherit from `CustomMQTTDevice` (Recommended)

`CustomMQTTDevice` is the cleanest way for a custom component to use MQTT. It wraps
`global_mqtt_client` and provides member-function-pointer callbacks.

```cpp
#include "esphome/core/component.h"
#include "esphome/components/mqtt/custom_mqtt_device.h"

namespace esphome {
namespace my_component {

class MyComponent : public Component, public mqtt::CustomMQTTDevice {
 public:
  void setup() override {
    // Subscribe — callbacks are member function pointers
    this->subscribe("cmnd/my/command",
                    &MyComponent::on_command_message);
    this->subscribe_json("cmnd/my/json",
                         &MyComponent::on_json_message);
  }

  void on_command_message(const std::string &topic,
                           const std::string &payload) {
    ESP_LOGI(TAG, "Received: %s", payload.c_str());
  }

  void on_json_message(const std::string &topic, JsonObject root) {
    float value = root["value"];
    ESP_LOGI(TAG, "JSON value: %.2f", value);
  }

  void loop() override {
    if (!this->is_connected()) return;   // MQTT not ready yet

    // Publish a plain string
    this->publish("stat/my/sensor", "online");

    // Publish with QoS and retain
    this->publish("stat/my/availability", "online", 0, true);
  }
};

}  // namespace my_component
}  // namespace esphome
```

### `CustomMQTTDevice` method reference

| Method | Signature | Description |
|--------|-----------|-------------|
| `publish` | `publish(topic, payload, qos=0, retain=false)` | Publish a plain string payload |
| `publish_json` | `publish_json(topic, builder_lambda, qos=0, retain=false)` | Build and publish a JSON payload |
| `subscribe` | `subscribe(topic, &Class::method, qos=0)` | Subscribe; callback receives `(topic, payload)` |
| `subscribe_json` | `subscribe_json(topic, &Class::method, qos=0)` | Subscribe; callback receives `(topic, JsonObject)` — skips invalid JSON |
| `is_connected` | `is_connected()` | Returns `true` if the MQTT broker connection is live |

---

## Option 2: `global_mqtt_client` directly

When you cannot inherit from `CustomMQTTDevice` (e.g., because of multiple inheritance constraints
or existing base classes), call `global_mqtt_client` directly.

```cpp
#include "esphome/components/mqtt/mqtt_client.h"

// In your method:
void MyComponent::publish_to_mqtt(const std::string &topic,
                                   const std::string &payload,
                                   bool retain) {
  if (mqtt::global_mqtt_client == nullptr) return;
  mqtt::global_mqtt_client->publish(topic, payload, 0, retain);
}
```

### `MQTTClientComponent` method signatures

```cpp
// Publish (several overloads)
bool publish(const std::string &topic, const std::string &payload,
             uint8_t qos = 0, bool retain = false);
bool publish(const std::string &topic, const char *payload,
             size_t payload_length, uint8_t qos = 0, bool retain = false);

// Publish JSON
bool publish_json(const std::string &topic,
                  const json::json_build_t &builder,
                  uint8_t qos = 0, bool retain = false);

// Subscribe (lambda callback)
void subscribe(const std::string &topic,
               mqtt_callback_t callback,   // std::function<void(string,string)>
               uint8_t qos = 0);

// Subscribe JSON
void subscribe_json(const std::string &topic,
                    mqtt_json_callback_t callback,
                    uint8_t qos = 0);

// Unsubscribe (removes all subscriptions to the topic)
void unsubscribe(const std::string &topic);

// Connection state
bool is_connected();
```

### Lambda subscribe example

```cpp
mqtt::global_mqtt_client->subscribe(
    "meshtastic/gateway/command",
    [this](const std::string &topic, const std::string &payload) {
        this->handle_command_(payload);
    },
    0);
```

---

## Publishing JSON

Use `publish_json` with a lambda that builds the JSON object:

```cpp
// CustomMQTTDevice version:
this->publish_json("meshtastic/" + node_id + "/position",
    [lat, lon, alt](JsonObject root) {
        root["latitude"]  = lat;
        root["longitude"] = lon;
        root["altitude"]  = alt;
    }, 0, false);

// global_mqtt_client version:
mqtt::global_mqtt_client->publish_json(
    "meshtastic/" + node_id + "/position",
    [lat, lon, alt](JsonObject root) {
        root["latitude"]  = lat;
        root["longitude"] = lon;
        root["altitude"]  = alt;
    });
```

Add `"json"` to `AUTO_LOAD` in `__init__.py` if using `publish_json`:

```python
AUTO_LOAD = ["mqtt", "json"]
```

---

## MQTT Birth / Will Messages (YAML)

Set in `meshtastic_gw.yaml` — these are handled by the MQTT component, not in C++ code:

```yaml
mqtt:
  broker: !secret mqtt_broker
  birth_message:
    topic: meshtastic/gateway/status
    payload: online
    retain: true
  will_message:
    topic: meshtastic/gateway/status
    payload: offline
    retain: true
```

From C++, publish a matching `online` message in `setup()` / after sync:

```cpp
this->publish("meshtastic/gateway/status", "online", 0, true);
```

---

## Helper: Building Topic Strings

```cpp
// Format: "<prefix>/<node_id_hex>/<suffix>"
std::string MeshtasticBLEComponent::node_topic_(uint32_t node_num,
                                                  const char *suffix) {
    char buf[80];
    snprintf(buf, sizeof(buf), "%s/%08X/%s",
             topic_prefix_.c_str(), node_num, suffix);
    return std::string(buf);
}

// Usage:
this->publish(this->node_topic_(pkt.from, "text"), text_payload);
this->publish(this->node_topic_(pkt.from, "position/latitude"),
              std::to_string(lat), 0, true);
```

---

## Checking Connection Before Publishing

Always guard publishes with `is_connected()` (or `global_mqtt_client->is_connected()`):

```cpp
void MeshtasticBLEComponent::publish_(const std::string &topic,
                                       const std::string &payload,
                                       bool retain) {
    if (!this->is_connected()) {
        ESP_LOGW(TAG, "MQTT not connected, dropping: %s", topic.c_str());
        return;
    }
    this->publish(topic, payload, 0, retain);
}
```

---

## References

- [ESPHome MQTT Component docs](https://esphome.io/components/mqtt/)
- [CustomMQTTDevice C++ class reference](https://api-docs.esphome.io/classesphome_1_1mqtt_1_1_custom_m_q_t_t_device)
- [MQTTClientComponent C++ class reference](https://api-docs.esphome.io/classesphome_1_1mqtt_1_1_m_q_t_t_client_component)
- [mqtt_client.cpp source](https://api-docs.esphome.io/mqtt__client_8cpp_source)
