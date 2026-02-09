# lvgl-msg-bus

Thread-safe **message bus** and **reactive data store** for ESP-IDF + LVGL applications.

Designed for embedded systems where hardware tasks need to exchange data with a UI layer running in the LVGL task — safely, efficiently, and with minimal boilerplate.

## Features

| Feature | Description |
|---------|-------------|
| **Publish / Subscribe** | Topic-based message bus with `uint32_t` topic IDs. |
| **Per-subscriber throttle** | `min_interval_ms` on `Subscribe()` — bus skips over-frequent deliveries automatically. |
| **LVGL-thread dispatch** | `LvglAsync` delivery mode uses `lv_async_call()` — subscribers safely update widgets without manual locking. |
| **Reactive DataStore** | Thread-safe key-value store that auto-publishes change notifications. |
| **RAII Subscriptions** | `Subscription` and `SubscriptionGroup` automatically unsubscribe on destruction. |
| **Zero framework lock-in** | Pure FreeRTOS + LVGL — no dependency on a specific board or display driver. |

## Quick Start

### Installation

Add to your project's `main/idf_component.yml`:

```yaml
dependencies:
  txp666/lvgl-msg-bus: "^1.1.0"
```

The IDF Component Manager will download and link the component automatically on `idf.py build`.

### Initialise

Call once at startup, before creating any subscriptions:

```cpp
#include "lvgl_msg_bus/message_bus.h"
#include "lvgl_msg_bus/data_store.h"

// In app_main() or board init — after lv_init().
msgbus::MessageBus::GetInstance().Initialize();
msgbus::DataStore::GetInstance().Initialize();
```

### Define topics

```cpp
// msg_topics.h (application-specific)
namespace Topic {
    constexpr uint32_t SensorData    = 0x0001;
    constexpr uint32_t BatteryStatus = 0x0002;
    constexpr uint32_t WifiStatus    = 0x0003;
}
```

### Publish from a hardware task

```cpp
#include "lvgl_msg_bus/message_bus.h"
#include "msg_topics.h"

struct SensorReading { float current_ma; uint8_t channel; };

void adc_task(void*) {
    while (true) {
        SensorReading r = { .current_ma = read_adc(), .channel = 0 };
        msgbus::MessageBus::GetInstance().Publish(Topic::SensorData, r);
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}
```

### Subscribe in a UI page

```cpp
#include "lvgl_msg_bus/message_bus.h"
#include "lvgl_msg_bus/subscription.h"
#include "msg_topics.h"

class HomePage : public PageBase {
    msgbus::SubscriptionGroup subs_;

    void OnEnter() override {
        auto& bus = msgbus::MessageBus::GetInstance();

        // Chart updates at ~100 ms (10 fps) — bus-level throttle
        subs_.Add(bus.Subscribe(
            Topic::SensorData,
            [this](const msgbus::Message& msg) {
                auto& r = msg.As<SensorReading>();
                UpdateChart(r);
            },
            msgbus::DeliveryMode::LvglAsync, 100));

        // Label updates at ~200 ms — bus-level throttle
        subs_.Add(bus.Subscribe(
            Topic::SensorData,
            [this](const msgbus::Message& msg) {
                auto& r = msg.As<SensorReading>();
                lv_label_set_text_fmt(label_, "%.2f mA", r.current_ma);
            },
            msgbus::DeliveryMode::LvglAsync, 200));
    }

    void OnLeave() override {
        subs_.Clear();  // All subscriptions cancelled
    }
};
```

### Use the DataStore for shared state

```cpp
#include "lvgl_msg_bus/data_store.h"

// Hardware side — any task:
msgbus::DataStore::GetInstance().Set(Topic::BatteryStatus, battery_pct);

// UI side — watch for changes (callback in LVGL thread):
battery_watch_ = msgbus::Subscription(
    msgbus::DataStore::GetInstance().Watch(Topic::BatteryStatus,
        [this](uint32_t key) {
            int pct = 0;
            msgbus::DataStore::GetInstance().Get(key, pct);
            lv_label_set_text_fmt(bat_label_, "%d%%", pct);
        }));
```

## API Reference

### MessageBus

| Method | Description |
|--------|-------------|
| `Initialize(config)` | One-time init. Optional `BusConfig` to tune capacity. |
| `Subscribe(topic, cb, mode, min_interval_ms)` | Register a callback. `min_interval_ms` (default 0) enables bus-level throttle — deliveries arriving sooner than the interval are skipped. Returns `SubscriptionId`. |
| `Unsubscribe(id)` | Remove a subscription. |
| `Publish(topic, data, size)` | Send a message to all matching subscribers (respects per-subscriber throttle). |
| `Publish<T>(topic, value)` | Typed convenience wrapper. |

### DataStore

| Method | Description |
|--------|-------------|
| `Initialize(config, topic_base)` | One-time init. `topic_base` offsets change-notification topics. |
| `Set<T>(key, value)` | Store a value; publishes notification if changed. |
| `Get<T>(key, out)` | Read a value. Returns `false` if key not found. |
| `Watch(key, callback)` | Subscribe to changes (LVGL thread). Returns `SubscriptionId`. |
| `Unwatch(id)` | Cancel a watch. |
| `Contains(key)` | Check if a key exists. |
| `Remove(key)` | Delete a key. |

### Subscription / SubscriptionGroup

| Class | Description |
|-------|-------------|
| `Subscription` | RAII guard for a single subscription. Move-only. |
| `SubscriptionGroup` | Holds multiple `Subscription` objects; clears all on destruction. |

### DeliveryMode

| Value | Behaviour |
|-------|-----------|
| `Immediate` | Callback runs synchronously in the publisher's thread. |
| `LvglAsync` | Callback dispatched to the LVGL task via `lv_async_call()`. |

## Thread Safety

- `Subscribe()`, `Unsubscribe()`, `Publish()` — safe from any FreeRTOS task.
- `DataStore::Set()`, `Get()`, `Contains()`, `Remove()` — safe from any task.
- **Not ISR-safe** — do not call from interrupt handlers.
- `LvglAsync` callbacks execute in the LVGL task context, so widget operations are safe without additional locking.

## Requirements

- ESP-IDF >= 5.0
- LVGL >= 9.0

## License

MIT — see [LICENSE](LICENSE).
