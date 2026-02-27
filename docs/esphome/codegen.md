# ESPHome Code Generation (`cg`) Reference

> Sources: [ESPHome Developer Docs — Architecture](https://developers.esphome.io/architecture/components/),
> [esphome/cpp_generator.py on GitHub](https://github.com/esphome/esphome/blob/dev/esphome/cpp_generator.py),
> [Snyk esphome.codegen reference](https://snyk.io/advisor/python/esphome/functions/esphome.codegen)

The `codegen` module (`cg`) translates Python config values into C++ source code that is
appended to the generated `main.cpp`. Nothing in `to_code` runs on the device — it builds the
C++ text that will be compiled and linked.

```python
import esphome.codegen as cg
```

---

## Namespace & Class Declarations

These calls produce the type information used later by `new_Pvariable` and `declare_id`.

```python
# Declare a C++ namespace inside esphome::
my_ns = cg.esphome_ns.namespace("my_component")

# Declare a class with base class(es)
MyClass = my_ns.class_("MyClass", cg.Component)

# Multiple base classes
MyClass = my_ns.class_("MyClass", cg.Component, uart.UARTDevice)

# Polling component (has update() called on an interval)
MyClass = my_ns.class_("MyClass", cg.PollingComponent)

# Enum
MyEnum = my_ns.enum("MyEnum", is_class=True)   # scoped enum (C++11)

# Struct
MyStruct = my_ns.struct("MyStruct")
```

### Pre-defined base classes

| Symbol | C++ type |
|--------|----------|
| `cg.Component` | `esphome::Component` |
| `cg.PollingComponent` | `esphome::PollingComponent` |
| `cg.global_ns` | Global (non-namespaced) scope |
| `cg.std_string` | `std::string` |
| `cg.uint8` | `uint8_t` |
| `cg.uint16` | `uint16_t` |
| `cg.uint32` | `uint32_t` |
| `cg.int32` | `int32_t` |
| `cg.float_` | `float` |
| `cg.bool_` | `bool` |

---

## `to_code` Core Functions

### `cg.new_Pvariable(id, *constructor_args)`

Generates `MyClass *var = new MyClass(arg1, arg2);` and returns a mock object representing the
pointer. The `id` comes from `config[CONF_ID]` and carries the type from `cv.declare_id(MyClass)`.

```python
var = cg.new_Pvariable(config[CONF_ID])
# With constructor args:
var = cg.new_Pvariable(config[CONF_ID], 42, "hello")
# → MyClass *var = new MyClass(42, "hello");
```

### `await cg.register_component(var, config)`

Generates `App.register_component(var);` so ESPHome's scheduler calls `setup()`, `loop()`, and
(for `PollingComponent`) `update()` on the component. Always `await` this.

```python
await cg.register_component(var, config)
```

For `PollingComponent`, also call:
```python
cg.add(var.set_update_interval(config[CONF_UPDATE_INTERVAL]))
```

### `cg.add(expression)`

Appends one line of C++ to the generated code.

```python
cg.add(var.set_name("foo"))         # → var->set_name("foo");
cg.add(var.set_count(42))           # → var->set_count(42);
cg.add(var.set_flag(True))          # → var->set_flag(true);
```

### `await cg.get_variable(id)`

Retrieves a previously declared component variable by its config ID. Use to wire two components
together.

```python
mqtt_var = await cg.get_variable(config[CONF_MQTT_ID])
cg.add(var.set_mqtt_client(mqtt_var))
```

### `await cg.gpio_pin_expression(config[CONF_PIN])`

Resolves a GPIO pin config dict into a C++ pin expression.

```python
pin = await cg.gpio_pin_expression(config[CONF_PIN])
cg.add(var.set_pin(pin))
```

---

## Build System Helpers

### `cg.add_library(name, version, repository=None)`

Adds a PlatformIO library dependency.

```python
cg.add_library("nanopb", "0.4.8")
cg.add_library("esphome/ESPAsyncTCP-esphome", "2.0.0")
# With a git repository:
cg.add_library("MyLib", None, "https://github.com/user/repo.git")
```

### `cg.add_define(name, value=None)`

Adds a `#define` to the generated code.

```python
cg.add_define("USE_MESHTASTIC_BLE")
cg.add_define("MESHTASTIC_MAX_NODES", 64)
# → #define USE_MESHTASTIC_BLE
# → #define MESHTASTIC_MAX_NODES 64
```

### `cg.add_global(expression)`

Adds a top-level (global scope) C++ statement outside `setup()`.

```python
cg.add_global(cg.RawExpression("static uint8_t my_buf[256];"))
```

### `cg.add_build_flag(flag)`

Adds a compiler flag.

```python
cg.add_build_flag("-DSOME_FLAG")
cg.add_build_flag("-O2")
```

---

## Advanced: Struct Initializers & Templates

### `cg.StructInitializer(type, *fields)`

Generates a C++ designated initializer or brace-init-list.

```python
cfg = cg.StructInitializer(
    MyConfig,
    ("timeout_ms", 5000),
    ("retry_count", 3),
)
cg.add(var.set_config(cfg))
# → var->set_config(MyConfig{.timeout_ms = 5000, .retry_count = 3});
```

### `cg.TemplateArguments(*args)`

Generates C++ template arguments.

```python
ArrayType = cg.TemplateArguments(cg.uint8, 16)
# → <uint8_t, 16>
```

### `cg.RawExpression(string)` and `cg.RawStatement(string)`

Inject arbitrary C++ when no typed helper exists.

```python
cg.add(cg.RawStatement("my_function();"))
```

---

## Conditional Code Generation

```python
# Only emit C++ if an optional key is present:
if bar_config := config.get(CONF_BAR):
    cg.add(var.set_bar(bar_config))

# Using walrus operator (Python 3.8+):
if (val := config.get(CONF_OPTIONAL)) is not None:
    cg.add(var.set_value(val))
```

---

## Full `to_code` Example

```python
async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    # Required setters
    cg.add(var.set_node_name(config[CONF_NODE_NAME]))
    cg.add(var.set_topic_prefix(config[CONF_TOPIC_PREFIX]))
    cg.add(var.set_reconnect_interval(config[CONF_RECONNECT_INTERVAL]))

    # Optional setter
    if CONF_NODE_MAC in config:
        cg.add(var.set_node_mac(config[CONF_NODE_MAC].as_hex))

    # Wire to another component
    if CONF_MQTT_ID in config:
        mqtt_client = await cg.get_variable(config[CONF_MQTT_ID])
        cg.add(var.set_mqtt_client(mqtt_client))

    # Library dependency
    cg.add_library("nanopb", "0.4.8")

    # Compile-time feature flag
    cg.add_define("USE_MESHTASTIC_BLE")
```

---

## References

- [esphome/cpp_generator.py — full codegen source](https://github.com/esphome/esphome/blob/dev/esphome/cpp_generator.py)
- [ESPHome Developer Docs — Architecture/Components](https://developers.esphome.io/architecture/components/)
- [Snyk — esphome.codegen.new_Pvariable](https://snyk.io/advisor/python/esphome/functions/esphome.codegen.new_Pvariable)
- [Snyk — esphome.codegen.add](https://snyk.io/advisor/python/esphome/functions/esphome.codegen.add)
- [Snyk — esphome.codegen.register_component](https://snyk.io/advisor/python/esphome/functions/esphome.codegen.register_component)
