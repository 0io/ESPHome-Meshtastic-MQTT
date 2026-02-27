# ESPHome Config Validation (`cv`) Reference

> Sources: [ESPHome Developer Docs](https://developers.esphome.io/architecture/components/),
> [esphome/config_validation.py on GitHub](https://github.com/esphome/esphome/blob/dev/esphome/config_validation.py)

The `config_validation` module (`cv`) is built on top of the
[voluptuous](https://github.com/alecthomas/voluptuous) library. Every value in a `CONFIG_SCHEMA`
dictionary is a *validator* — a callable that accepts a raw YAML value, validates and coerces it,
and returns the canonical Python value (or raises `cv.Invalid`).

```python
import esphome.config_validation as cv
```

---

## Schema Construction

### `cv.Schema({...})`

Creates a schema from a dict. Keys are wrapped with `cv.Required` or `cv.Optional`.

```python
CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID():                  cv.declare_id(MyClass),
    cv.Required("my_key"):            cv.string,
    cv.Optional("other", default=5):  cv.positive_int,
})
```

### `.extend(other_schema)`

Merges another schema dict or pre-built schema into the current one.

```python
CONFIG_SCHEMA = (
    cv.Schema({ ... })
    .extend(cv.COMPONENT_SCHEMA)       # adds optional setup_priority
)
```

### Pre-built schemas

| Schema | What it adds |
|--------|-------------|
| `cv.COMPONENT_SCHEMA` | `Optional(CONF_SETUP_PRIORITY): float_` |
| `cv.ENTITY_BASE_SCHEMA` | `name`, `icon`, `internal`, `disabled_by_default`, `entity_category` |
| `cv.polling_component_schema("60s")` | `update_interval` with a default |

---

## Key Markers

| Marker | Description |
|--------|-------------|
| `cv.Required("key")` | Key must be present; validation fails otherwise |
| `cv.Optional("key")` | Key may be omitted |
| `cv.Optional("key", default=value)` | Optional with a default; `default` is used verbatim if omitted |
| `cv.GenerateID()` | Auto-generates a unique ID; maps to `CONF_ID` (`"id"`) |

`default` is passed through the same validator, so you can write:
```python
cv.Optional(CONF_INTERVAL, default="30s"): cv.positive_time_period_milliseconds
```

---

## Validators

### String

| Validator | Accepts | Notes |
|-----------|---------|-------|
| `cv.string` | Any YAML scalar → `str` | Coerces numbers, booleans, etc. |
| `cv.string_strict` | Only real strings | Raises if the YAML value is not already a string |
| `cv.ssid` | WiFi SSID string | Max 32 chars |

### Numbers

| Validator | Accepts | Notes |
|-----------|---------|-------|
| `cv.int_` | Integer (no fractional part) | Raises if value has decimals |
| `cv.float_` | Floating-point | |
| `cv.positive_int` | Integer ≥ 0 | |
| `cv.positive_not_null_int` | Integer ≥ 1 | |
| `cv.uint32_t` | `uint32_t` range (0–4294967295) | |
| `cv.int_range(min, max)` | Integer within `[min, max]` | Inclusive |
| `cv.float_range(min, max)` | Float within `[min, max]` | |

### Boolean

| Validator | Accepts |
|-----------|---------|
| `cv.boolean` | `true`/`false`, `yes`/`no`, `on`/`off`, `1`/`0` → `bool` |

### Time / Frequency

| Validator | Accepts | Returns |
|-----------|---------|---------|
| `cv.positive_time_period_milliseconds` | `"500ms"`, `"1s"`, `"2min"` | `int` (ms) |
| `cv.positive_time_period_microseconds` | Same units | `int` (µs) |
| `cv.update_interval` | `"never"` or a time period | `uint32_t` ms |
| `cv.framerate` | `"10fps"`, `"10 fps"` | `float` |
| `cv.frequency` | `"20MHz"`, `"400kHz"` | `float` (Hz) |

### Hardware / Network

| Validator | Accepts |
|-----------|---------|
| `cv.mac_address` | `"AA:BB:CC:DD:EE:FF"` → `MacAddress` object (`.as_hex` gives `uint64_t`) |
| `cv.ipv4address` | `"192.168.1.1"` |
| `cv.declare_id(Type)` | ID string → typed `ID` object (use with `cv.GenerateID()`) |
| `cv.use_id(Type)` | ID string → reference to a declared ID of that type |

### GPIO / Pins

```python
from esphome import pins
cv.Required(CONF_PIN): pins.gpio_output_pin_schema
cv.Required(CONF_PIN): pins.gpio_input_pin_schema
cv.Required(CONF_PIN): pins.gpio_input_pullup_pin_schema
```

### Lists

```python
cv.ensure_list(cv.string)           # single string or list of strings → list
cv.ensure_list(cv.int_)
cv.All([cv.string], cv.Length(min=1, max=8))   # list with length constraint
```

---

## Combinator / Meta Validators

| Validator | Description |
|-----------|-------------|
| `cv.All(v1, v2, ...)` | Value must pass each validator in sequence; result of each is passed to next |
| `cv.one_of(*values, **kwargs)` | Value must equal one of the given literals |
| `cv.enum(mapping, upper=False)` | Maps a string key to a value (e.g., an enum) |
| `cv.ensure_list(validator)` | Wraps a scalar in a list if needed, then validates each element |
| `cv.Length(min=n, max=m)` | Validates length of a list or string |
| `cv.templatable(validator)` | Allows the value to be a lambda/template expression |
| `cv.hex_int` | Validates a hex integer string (`"0xDEAD"`) |

### `cv.one_of` example

```python
cv.Required(CONF_MODE): cv.one_of("fast", "slow", "auto", lower=True)
```

### `cv.enum` example

```python
LogLevel = my_ns.enum("LogLevel")
LOG_LEVELS = { "debug": LogLevel.DEBUG, "info": LogLevel.INFO }
cv.Optional(CONF_LOG_LEVEL, default="info"): cv.enum(LOG_LEVELS, lower=True)
```

---

## Custom Validators

A custom validator is any callable `(value) -> value` that raises `cv.Invalid` on bad input:

```python
def validate_node_name(value):
    value = cv.string(value)
    if not value.startswith("Meshtastic_"):
        raise cv.Invalid(
            f"node_name must start with 'Meshtastic_', got '{value}'"
        )
    return value

CONFIG_SCHEMA = cv.Schema({
    cv.Required(CONF_NODE_NAME): validate_node_name,
})
```

### Cross-field validation with `cv.All`

```python
def validate_one_required(config):
    if CONF_NODE_NAME not in config and CONF_NODE_MAC not in config:
        raise cv.Invalid("At least one of 'node_name' or 'node_mac' must be set.")
    return config

CONFIG_SCHEMA = cv.All(
    cv.Schema({ ... }),
    validate_one_required,
)
```

---

## Error Handling

`cv.Invalid` takes a message string and an optional `path` argument pointing to the offending key.
ESPHome will display the full config path to the user, making errors easy to diagnose.

```python
raise cv.Invalid("Timeout must be between 1 and 3600 seconds", path=[CONF_TIMEOUT])
```

---

## Deprecating a Config Key

Keep the old key during a deprecation window, then replace with `cv.invalid`:

```python
# During deprecation:
cv.Optional(CONF_OLD_KEY): cv.string,   # still works, but log a warning in to_code

# After deprecation period:
cv.Optional(CONF_OLD_KEY): cv.invalid("'old_key' was removed, use 'new_key' instead"),
```

---

## References

- [esphome/config_validation.py source](https://github.com/esphome/esphome/blob/dev/esphome/config_validation.py)
- [ESPHome Developer Docs — Components overview](https://developers.esphome.io/architecture/components/)
- [ESPHome Developer Docs — Code guide](https://developers.esphome.io/contributing/code/)
