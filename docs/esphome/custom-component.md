# ESPHome Custom Component Structure

> Sources: [ESPHome Developer Docs — Architecture](https://developers.esphome.io/architecture/components/),
> [External Components docs](https://esphome.io/components/external_components/),
> [jesserockz/esphome-external-component-examples](https://github.com/jesserockz/esphome-external-component-examples)

---

## Directory Layout

A component that lives as a top-level YAML key (like `meshtastic_ble:`) requires exactly this
structure:

```
components/
└── meshtastic_ble/
    ├── __init__.py           ← Python: schema + code-gen
    ├── meshtastic_ble.h      ← C++ class declaration
    └── meshtastic_ble.cpp    ← C++ implementation
```

Platform sub-components (e.g., a sensor or switch *implementation* of a hub) add a sub-directory:

```
components/
└── my_hub/
    ├── __init__.py           ← hub registration
    ├── my_hub.h
    ├── my_hub.cpp
    └── sensor/
        ├── __init__.py       ← sensor platform registration
        └── my_sensor.h
```

The `path` in `external_components` must point to the **parent** of the component directory — not
to the component directory itself:

```yaml
external_components:
  - source:
      type: local
      path: components          # contains meshtastic_ble/, not meshtastic_ble/__init__.py
```

---

## `__init__.py` Anatomy

```python
# ── 1. Imports ────────────────────────────────────────────────────────────────
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

# ── 2. Module-level metadata ──────────────────────────────────────────────────
CODEOWNERS   = ["@your-github-handle"]   # optional
DEPENDENCIES = ["esp32"]                 # components that must already be present
AUTO_LOAD    = ["mqtt"]                  # components auto-added if not listed
MULTI_CONF   = False                     # True → allow multiple instances

# ── 3. C++ namespace + class declaration ──────────────────────────────────────
my_ns = cg.esphome_ns.namespace("my_component")   # must match C++ namespace name
MyComponent = my_ns.class_("MyComponent", cg.Component)
# Additional base classes come as extra positional args:
# MyComponent = my_ns.class_("MyComponent", cg.Component, uart.UARTDevice)

# ── 4. Config key constants ───────────────────────────────────────────────────
CONF_FOO = "foo"
CONF_BAR = "bar"

# ── 5. Schema ─────────────────────────────────────────────────────────────────
CONFIG_SCHEMA = (
    cv.Schema({
        cv.GenerateID(): cv.declare_id(MyComponent),
        cv.Required(CONF_FOO): cv.string,
        cv.Optional(CONF_BAR, default="default_val"): cv.string,
    })
    .extend(cv.COMPONENT_SCHEMA)    # adds optional setup_priority key
)

# ── 6. Optional cross-field validation ────────────────────────────────────────
def validate(config):
    if CONF_FOO not in config and CONF_BAR not in config:
        raise cv.Invalid("At least one of 'foo' or 'bar' must be set.")
    return config

CONFIG_SCHEMA = cv.All(CONFIG_SCHEMA, validate)

# ── 7. Code generation ────────────────────────────────────────────────────────
async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])    # creates: MyComponent *var = new MyComponent();
    await cg.register_component(var, config)   # registers setup()/loop() with App

    cg.add(var.set_foo(config[CONF_FOO]))      # var->set_foo("...");

    if CONF_BAR in config:                     # optional key — check before indexing
        cg.add(var.set_bar(config[CONF_BAR]))
```

---

## Module-Level Variables

| Variable | Type | Purpose |
|----------|------|---------|
| `CODEOWNERS` | `list[str]` | GitHub handles (`"@username"`) for the component. Used to auto-update CODEOWNERS file in the main esphome repo; can be omitted for external components. |
| `DEPENDENCIES` | `list[str]` | Components that **must** already be in the user's config. Validation fails if they are absent. Example: `["esp32", "mqtt"]` |
| `AUTO_LOAD` | `list[str]` | Components automatically added to the compilation even if not listed in the YAML. Use for helper components (e.g., `"json"`, `"mqtt"`). |
| `MULTI_CONF` | `bool` or `int` | `False` (default) — only one instance allowed. `True` — unlimited instances. An integer caps the maximum count. |

---

## Declaring C++ Types

```python
# Namespace (must match the `namespace esphome { namespace my_ns { ... } }` in C++)
my_ns = cg.esphome_ns.namespace("my_ns")

# Class
MyClass = my_ns.class_("MyClass", cg.Component)

# Enum (declared in C++ as enum MyEnum { ... })
MyEnum = my_ns.enum("MyEnum")

# Struct (used with cg.StructInitializer)
MyStruct = my_ns.struct("MyStruct")

# Referencing an existing ESPHome class as a base:
# cg.Component, cg.PollingComponent, uart.UARTDevice, etc.
```

---

## `to_code` Patterns

### Passing a string setter

```python
cg.add(var.set_name(config[CONF_NAME]))
# generates: var->set_name("SomeName");
```

### Passing an integer / float

```python
cg.add(var.set_interval(config[CONF_INTERVAL]))
# generates: var->set_interval(30);
```

### Referencing another component by ID

```python
mqtt_client = await cg.get_variable(config[CONF_MQTT_ID])
cg.add(var.set_mqtt_client(mqtt_client))
```

### Resolving a GPIO pin

```python
pin = await cg.gpio_pin_expression(config[CONF_PIN])
cg.add(var.set_pin(pin))
```

### Adding a PlatformIO library

```python
cg.add_library("nanopb", "0.4.8")
```

### Adding a compile-time define

```python
cg.add_define("USE_MESHTASTIC_BLE")
```

### Registering a polling component

```python
await cg.register_component(var, config)
# For PollingComponent, also register the update interval:
cg.add(var.set_update_interval(config[CONF_UPDATE_INTERVAL]))
```

---

## `external_components` YAML Reference

### Local source

```yaml
external_components:
  - source:
      type: local
      path: components          # relative to the YAML file's directory
    # components: [meshtastic_ble]   # optional — load only specific ones
```

### Git source

```yaml
external_components:
  - source:
      type: git
      url: https://github.com/you/repo
      ref: main                 # branch, tag, or commit hash
    components: [meshtastic_ble]
    refresh: 1d                 # how often to re-fetch; default 1day
```

### Shorthand (GitHub)

```yaml
external_components:
  - source: github://you/repo@main
    components: [meshtastic_ble]
```

The cloned copy is cached in `.esphome/external_components/`. The `components:` key is optional —
if omitted, **all** components found in the repo are loaded.

---

## Platform vs. Hub Pattern

A *hub* component (like a sensor chip accessible via I²C) is registered once at the top level.
Individual *platform* sub-components (e.g., `sensor:`, `binary_sensor:`) then reference the hub:

```yaml
# user config
my_sensor_hub:
  id: hub1

sensor:
  - platform: my_sensor_hub
    hub_id: hub1
    name: "Temperature"
```

The hub's `__init__.py` registers `MyHub` as a `Component`. The `sensor/` platform's `__init__.py`
does `await cg.get_variable(config[CONF_HUB_ID])` to retrieve the hub instance and call
`hub.register_sensor(sensor)` on it.

---

## References

- [ESPHome Developer Docs — Architecture/Components](https://developers.esphome.io/architecture/components/)
- [ESPHome External Components docs](https://esphome.io/components/external_components/)
- [Example external components (jesserockz)](https://github.com/jesserockz/esphome-external-component-examples)
- [Medium tutorial — external component part 1](https://medium.com/@vinsce/create-an-esphome-external-component-part-1-introduction-config-validation-and-code-generation-e0389e674bd6)
- [esphome/esphome — cpp_generator.py](https://github.com/esphome/esphome/blob/dev/esphome/cpp_generator.py)
