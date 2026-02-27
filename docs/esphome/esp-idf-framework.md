# ESPHome ESP-IDF Framework Reference

> Sources: [ESPHome ESP32 platform docs](https://esphome.io/components/esp32/),
> [ESP32 Arduino→IDF Migration Guide](https://esphome.io/guides/esp32_arduino_to_idf/),
> [sdkconfig_options issue #7054](https://github.com/esphome/issues/issues/7054),
> [ESP-IDF Build System docs](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/build-system.html)

---

## Why ESP-IDF (not Arduino)

The `meshtastic_ble` component **requires** the `esp-idf` framework because:

1. NimBLE (Apache Mynewt NimBLE) is only available through esp-idf.
2. Low-level GATTC APIs (`ble_gattc_*`, `ble_gap_*`) are not exposed by the Arduino BLE libraries.
3. `esp-idf` gives direct control over FreeRTOS tasks and the BLE host task.
4. Better stack size and heap tuning via `sdkconfig_options`.

---

## YAML Framework Configuration

```yaml
esp32:
  board: esp32-c3-devkitm-1       # change to match your hardware
  framework:
    type: esp-idf
    version: recommended            # pin to a specific version if needed, e.g. "5.1.4"

    # Pass ESP-IDF Kconfig options directly
    sdkconfig_options:
      # ── BLE / NimBLE ────────────────────────────────────────────────────
      CONFIG_BT_ENABLED: "y"
      CONFIG_BT_NIMBLE_ENABLED: "y"
      CONFIG_BT_NIMBLE_ROLE_CENTRAL: "y"
      CONFIG_BT_NIMBLE_ROLE_PERIPHERAL: "n"       # saves ~20 KB RAM
      CONFIG_BT_NIMBLE_ATT_PREFERRED_MTU: "512"   # negotiate 512-byte MTU
      CONFIG_BT_NIMBLE_MAX_CONNECTIONS: "1"        # only one Meshtastic node
      CONFIG_BT_NIMBLE_SVC_GAP_DEVICE_NAME: "meshtastic-gw"

      # ── FreeRTOS / System ────────────────────────────────────────────────
      # Increase loop task stack if you hit stack overflow (default 8192)
      # CONFIG_ESP_MAIN_TASK_STACK_SIZE: "16384"

    # Optional: increase loop task stack from ESPHome side (ESPHome ≥ 2024.6)
    loop_task_stack_size: 16384     # bytes; valid range 8192–32768

    # Experimental features (use with caution)
    # enable_idf_experimental_features: true
```

### Board selection

| Board | Use for |
|-------|---------|
| `esp32dev` | Classic 38-pin ESP32 dev board |
| `esp32-c3-devkitm-1` | ESP32-C3 — recommended for low power + BLE central |
| `esp32-s3-devkitc-1` | ESP32-S3 — more RAM/flash, USB OTG |
| `esp32-s2-saola-1` | ESP32-S2 — **no BLE hardware**, cannot use this project |

### Version pinning

```yaml
framework:
  type: esp-idf
  version: "5.1.4"           # pin to exact esp-idf release
  # version: recommended     # latest stable (default)
  # version: dev             # bleeding edge
```

---

## Key `sdkconfig_options` for This Project

| Option | Recommended value | Notes |
|--------|------------------|-------|
| `CONFIG_BT_ENABLED` | `"y"` | Enable Bluetooth subsystem |
| `CONFIG_BT_NIMBLE_ENABLED` | `"y"` | Use NimBLE (not Bluedroid) |
| `CONFIG_BT_NIMBLE_ROLE_CENTRAL` | `"y"` | GATT client — connect to Meshtastic |
| `CONFIG_BT_NIMBLE_ROLE_PERIPHERAL` | `"n"` | Not needed; saves RAM |
| `CONFIG_BT_NIMBLE_ATT_PREFERRED_MTU` | `"512"` | Maximum ATT MTU |
| `CONFIG_BT_NIMBLE_MAX_CONNECTIONS` | `"1"` | Only one concurrent BLE connection |
| `CONFIG_BT_NIMBLE_SVC_GAP_DEVICE_NAME` | `"meshtastic-gw"` | Device name seen by BLE scanners |
| `CONFIG_BT_NIMBLE_PINNED_TO_CORE_1` | `"y"` | Pin NimBLE host task to core 1 (dual-core ESP32 only) |
| `CONFIG_FREERTOS_HZ` | `"1000"` | 1 kHz FreeRTOS tick for better timers |
| `CONFIG_ESP_MAIN_TASK_STACK_SIZE` | `"16384"` | Increase if stack overflow occurs |

> **Important:** ESPHome generates its own `sdkconfig` and merges `sdkconfig_options` on top.
> If an option does not appear in `.esphome/build/<name>/sdkconfig`, the value format may be
> wrong (always use quoted strings: `"y"`, `"n"`, `"512"`) or ESPHome may be overriding it.
> Check the generated file to verify.

---

## NimBLE Headers

With `esp-idf` framework, NimBLE headers are available without additional libraries:

```cpp
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/ble_gattc.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
```

---

## Loop Task Stack Size

ESPHome runs all `setup()` and `loop()` calls in a single FreeRTOS task (`loopTask`). If you see
`***ERROR*** A stack overflow in task loopTask has been detected`, increase the stack:

```yaml
esp32:
  framework:
    type: esp-idf
    loop_task_stack_size: 16384    # 16 KB (default 8192)
```

Or via sdkconfig:

```yaml
sdkconfig_options:
  CONFIG_ESP_MAIN_TASK_STACK_SIZE: "16384"
```

The NimBLE host runs in its **own** FreeRTOS task (`nimble_host_task`), started by
`nimble_port_freertos_init()`. Its default stack is set via
`CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE` (default 4096 bytes).

---

## IDF Component Dependencies (`idf_component.yml`)

For non-standard IDF components (e.g., third-party libraries not bundled with esp-idf), create
`idf_component.yml` alongside the component source. This is **not needed** for NimBLE (it ships
with esp-idf), but may be needed for other BLE or protocol libraries.

```yaml
# components/meshtastic_ble/idf_component.yml  (if ever needed)
dependencies:
  idf:
    version: ">=5.0.0"
```

Do **not** manually edit `dependencies.lock` — it is auto-generated.

---

## Arduino vs. ESP-IDF Differences

| Topic | Arduino | ESP-IDF |
|-------|---------|---------|
| BLE API | `BLEDevice`, `BLEScan` (bluedroid wrapper) | `ble_gap_*`, `ble_gattc_*` (NimBLE) |
| Logging | `Serial.println` | `ESP_LOGx(TAG, ...)` |
| Timing | `delay()`, `millis()` | `vTaskDelay()`, `esp_timer_get_time()` |
| Task creation | Mostly hidden | Direct FreeRTOS `xTaskCreate()` |
| Memory | Arduino heap | FreeRTOS heap; monitor with `heap_caps_get_free_size()` |
| sdkconfig | Limited | Full access via `sdkconfig_options` |

---

## Debugging Tips

```yaml
# Add to YAML for verbose esp-idf logs:
logger:
  level: VERBOSE
  logs:
    BT_NIMBLE: DEBUG
    esp.component: VERBOSE
```

Check free heap during `loop()`:

```cpp
ESP_LOGD(TAG, "Free heap: %zu bytes", heap_caps_get_free_size(MALLOC_CAP_8BIT));
```

Verify NimBLE host is running:

```cpp
ESP_LOGI(TAG, "NimBLE host synced: %d", ble_hs_synced());
```

---

## References

- [ESPHome ESP32 platform docs](https://esphome.io/components/esp32/)
- [ESPHome Arduino→IDF Migration Guide](https://esphome.io/guides/esp32_arduino_to_idf/)
- [sdkconfig_options known issue (2024.6.1+)](https://github.com/esphome/issues/issues/7054)
- [ESP-IDF Build System docs](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/build-system.html)
- [ESP-IDF NimBLE blecent example](https://github.com/espressif/esp-idf/blob/master/examples/bluetooth/nimble/blecent/main/main.c)
- [ESP32 BLE FAQ](https://docs.espressif.com/projects/esp-faq/en/latest/software-framework/bt/ble.html)
