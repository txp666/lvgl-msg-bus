# lvgl-msg-bus

面向 ESP-IDF + LVGL 应用的**线程安全消息总线**与**响应式数据仓库**。

专为嵌入式场景设计：硬件任务需要与运行在 LVGL 任务中的 UI 层高效、安全地交换数据，且只需极少的样板代码。

## 特性

| 特性 | 说明 |
|------|------|
| **发布 / 订阅** | 基于 `uint32_t` Topic ID 的消息总线，O(1) 查找，无字符串比较。 |
| **LVGL 线程派发** | `LvglAsync` 投递模式通过 `lv_async_call()` 自动将回调派发到 LVGL 任务，订阅者可直接操作控件，无需手动加锁。 |
| **响应式 DataStore** | 线程安全的 key-value 存储，值变更时自动发布通知。 |
| **RAII 订阅管理** | `Subscription` 和 `SubscriptionGroup` 在析构时自动取消订阅，无泄漏。 |
| **零框架绑定** | 仅依赖 FreeRTOS + LVGL，不绑定特定开发板或显示驱动。 |

## 快速上手

### 安装

**作为本地组件**（开发期间推荐）：

```
your_project/
├── components/
│   └── lvgl-msg-bus/    ← 克隆或复制到这里
├── main/
│   └── ...
└── CMakeLists.txt
```

**作为托管组件**（发布后）：

```yaml
# main/idf_component.yml
dependencies:
  txp666/lvgl-msg-bus: "^1.0.0"
```

### 初始化

在启动时调用一次，需在 `lv_init()` 之后：

```cpp
#include "lvgl_msg_bus/message_bus.h"
#include "lvgl_msg_bus/data_store.h"

// 在 app_main() 或板级初始化中
msgbus::MessageBus::GetInstance().Initialize();
msgbus::DataStore::GetInstance().Initialize();
```

### 定义 Topic

```cpp
// msg_topics.h（应用层自定义）
namespace Topic {
    constexpr uint32_t SensorData    = 0x0001;  // 传感器数据
    constexpr uint32_t BatteryStatus = 0x0002;  // 电池状态
    constexpr uint32_t WifiStatus    = 0x0003;  // WiFi 状态
}
```

### 硬件任务中发布

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

### UI 页面中订阅

```cpp
#include "lvgl_msg_bus/message_bus.h"
#include "lvgl_msg_bus/subscription.h"
#include "msg_topics.h"

class HomePage : public PageBase {
    msgbus::Subscription sensor_sub_;

    void OnCreate(lv_obj_t* parent) override {
        sensor_sub_ = msgbus::Subscription(
            msgbus::MessageBus::GetInstance().Subscribe(
                Topic::SensorData,
                [this](const msgbus::Message& msg) {
                    // 在 LVGL 线程中执行 — 可安全操作控件
                    auto& r = msg.As<SensorReading>();
                    lv_label_set_text_fmt(label_, "%.2f mA", r.current_ma);
                }));
    }
    // sensor_sub_ 在页面销毁时自动取消订阅
};
```

### 使用 DataStore 共享状态

```cpp
#include "lvgl_msg_bus/data_store.h"

// 硬件侧 — 任意任务：
msgbus::DataStore::GetInstance().Set(Topic::BatteryStatus, battery_pct);

// UI 侧 — 监听变更（回调在 LVGL 线程中执行）：
battery_watch_ = msgbus::Subscription(
    msgbus::DataStore::GetInstance().Watch(Topic::BatteryStatus,
        [this](uint32_t key) {
            int pct = 0;
            msgbus::DataStore::GetInstance().Get(key, pct);
            lv_label_set_text_fmt(bat_label_, "%d%%", pct);
        }));
```

## API 参考

### MessageBus（消息总线）

| 方法 | 说明 |
|------|------|
| `Initialize(config)` | 一次性初始化。可选 `BusConfig` 调整容量。 |
| `Subscribe(topic, cb, mode)` | 注册回调。返回 `SubscriptionId`。 |
| `Unsubscribe(id)` | 移除订阅。 |
| `Publish(topic, data, size)` | 向匹配的订阅者发送消息。 |
| `Publish<T>(topic, value)` | 类型安全的便捷封装。 |

### DataStore（数据仓库）

| 方法 | 说明 |
|------|------|
| `Initialize(config, topic_base)` | 一次性初始化。`topic_base` 用于偏移变更通知的 topic。 |
| `Set<T>(key, value)` | 存储值；若值发生变化则发布通知。 |
| `Get<T>(key, out)` | 读取值。key 不存在时返回 `false`。 |
| `Watch(key, callback)` | 订阅变更（回调在 LVGL 线程中执行）。返回 `SubscriptionId`。 |
| `Unwatch(id)` | 取消监听。 |
| `Contains(key)` | 检查 key 是否存在。 |
| `Remove(key)` | 删除 key。 |

### Subscription / SubscriptionGroup（订阅守卫）

| 类 | 说明 |
|----|------|
| `Subscription` | 单个订阅的 RAII 守卫。仅支持移动，不可复制。 |
| `SubscriptionGroup` | 持有多个 `Subscription`，析构时统一取消。 |

### DeliveryMode（投递模式）

| 值 | 行为 |
|----|------|
| `Immediate` | 回调在发布者线程中同步执行。 |
| `LvglAsync` | 回调通过 `lv_async_call()` 派发到 LVGL 任务中执行。 |

## 线程安全

- `Subscribe()`、`Unsubscribe()`、`Publish()` — 可从任意 FreeRTOS 任务调用。
- `DataStore::Set()`、`Get()`、`Contains()`、`Remove()` — 可从任意任务调用。
- **不支持 ISR** — 请勿在中断处理函数中调用。
- `LvglAsync` 回调在 LVGL 任务上下文中执行，可直接操作控件，无需额外加锁。

## 依赖

- ESP-IDF >= 5.0
- LVGL >= 9.0

## 许可证

MIT — 详见 [LICENSE](LICENSE)。
