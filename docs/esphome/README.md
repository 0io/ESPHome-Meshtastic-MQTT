# ESPHome Custom Component Reference

Reference material for building the `meshtastic_ble` ESPHome custom component.

| Document | Contents |
|----------|----------|
| [custom-component.md](custom-component.md) | Component file layout, `__init__.py` anatomy, module-level variables, `to_code`, `external_components` YAML |
| [config-validation.md](config-validation.md) | `cv` validators — `Required`, `Optional`, strings, numbers, booleans, time, MAC, combinators, custom validators |
| [codegen.md](codegen.md) | `cg` code-gen API — `new_Pvariable`, `register_component`, `add`, `get_variable`, `add_library`, `add_define` |
| [cpp-component.md](cpp-component.md) | C++ `Component` base class lifecycle, `setup_priority` constants, `mark_failed`, `PollingComponent`, logging macros |
| [mqtt-api.md](mqtt-api.md) | Publishing and subscribing from C++ via `global_mqtt_client` and `CustomMQTTDevice` |
| [esp-idf-framework.md](esp-idf-framework.md) | `esp-idf` framework YAML options, `sdkconfig_options`, `loop_task_stack_size`, NimBLE config keys |

---

## Quick Reference

### Minimal `__init__.py`

```python
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

my_ns = cg.esphome_ns.namespace("my_component")
MyComponent = my_ns.class_("MyComponent", cg.Component)

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(MyComponent),
    cv.Required("my_param"): cv.string,
}).extend(cv.COMPONENT_SCHEMA)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_my_param(config["my_param"]))
```

### `external_components` (local source)

```yaml
external_components:
  - source:
      type: local
      path: components       # relative to the YAML file
```

### setup_priority values

| Constant | Value | Use when |
|----------|-------|----------|
| `setup_priority::BUS` | 1000 | Communication buses (I²C, SPI) |
| `setup_priority::IO` | 900 | GPIO expanders |
| `setup_priority::HARDWARE` | 800 | Direct hardware components |
| `setup_priority::DATA` | 600 | Sensors (default) |
| `setup_priority::BLUETOOTH` | 350 | Bluetooth components |
| `setup_priority::AFTER_BLUETOOTH` | 300 | After BLE init |
| `setup_priority::AFTER_WIFI` | 200 | After WiFi connects |
| `setup_priority::AFTER_CONNECTION` | 100 | After API/MQTT connects |
| `setup_priority::LATE` | -100 | Very last |

---

## Official Sources

- [ESPHome Developer Docs — Architecture/Components](https://developers.esphome.io/architecture/components/)
- [ESPHome Developer Docs — Contributing/Code](https://developers.esphome.io/contributing/code/)
- [ESPHome Developer Docs — Logging Best Practices](https://developers.esphome.io/architecture/logging/)
- [ESPHome external_components docs](https://esphome.io/components/external_components/)
- [ESPHome ESP32 platform docs](https://esphome.io/components/esp32/)
- [ESPHome C++ API reference](https://api-docs.esphome.io/)
- [esphome/esphome GitHub](https://github.com/esphome/esphome)
