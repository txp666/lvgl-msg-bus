/*
 * SPDX-FileCopyrightText: 2025 txp666
 * SPDX-License-Identifier: MIT
 *
 * lvgl-msg-bus basic usage example.
 *
 * Demonstrates:
 *   1. MessageBus publish / subscribe with Immediate and LvglAsync modes.
 *   2. DataStore reactive key-value storage with Watch().
 *   3. Subscription RAII guard and SubscriptionGroup.
 */

#include <cstdio>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lvgl.h>

#include "lvgl_msg_bus/message_bus.h"
#include "lvgl_msg_bus/data_store.h"
#include "lvgl_msg_bus/subscription.h"

static const char* TAG = "Example";

// ---- Application-specific topic IDs ----------------------------------------

namespace Topic {
    constexpr uint32_t SensorData    = 0x0001;
    constexpr uint32_t ButtonPress   = 0x0002;
    constexpr uint32_t BatteryLevel  = 0x0003;
}

// ---- Application-specific data types ----------------------------------------

struct SensorReading {
    float    current_ma;
    float    physical_value;
    uint8_t  channel;
};

// ---- Simulated hardware task ------------------------------------------------

static void hardware_task(void* arg) {
    auto& bus   = msgbus::MessageBus::GetInstance();
    auto& store = msgbus::DataStore::GetInstance();

    uint32_t counter = 0;
    while (true) {
        // Simulate sensor data arriving every 500 ms.
        SensorReading reading = {
            .current_ma    = 4.0f + (counter % 160) * 0.1f,
            .physical_value = (counter % 160) * 0.625f,
            .channel       = 0,
        };
        bus.Publish(Topic::SensorData, reading);

        // Simulate battery level stored in DataStore.
        int battery_pct = 100 - static_cast<int>(counter % 101);
        store.Set(Topic::BatteryLevel, battery_pct);

        counter++;
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

// ---- Main -------------------------------------------------------------------

extern "C" void app_main(void) {
    // 1. Initialize LVGL (omitted in this skeleton — your board code does it).
    //    lv_init();  ...

    // 2. Initialize the message bus and data store.
    msgbus::MessageBus::GetInstance().Initialize();
    msgbus::DataStore::GetInstance().Initialize();

    // 3. Subscribe to topics.

    // 3a. MessageBus — LvglAsync delivery (default).
    //     The callback runs in the LVGL task, safe to update UI widgets.
    auto& bus = msgbus::MessageBus::GetInstance();

    msgbus::Subscription sensor_sub(
        bus.Subscribe(Topic::SensorData, [](const msgbus::Message& msg) {
            const auto& r = msg.As<SensorReading>();
            ESP_LOGI(TAG, "[LVGL] CH%u: %.2f mA  (%.2f)",
                     r.channel, r.current_ma, r.physical_value);
        })
    );

    // 3b. MessageBus — Immediate delivery (runs in publisher's thread).
    msgbus::Subscription button_sub(
        bus.Subscribe(Topic::ButtonPress, [](const msgbus::Message& msg) {
            ESP_LOGI(TAG, "[Immediate] Button pressed, data_size=%u",
                     (unsigned)msg.data_size);
        }, msgbus::DeliveryMode::Immediate)
    );

    // 3c. DataStore — Watch a key (callback in LVGL thread).
    auto& store = msgbus::DataStore::GetInstance();

    msgbus::Subscription battery_watch(
        store.Watch(Topic::BatteryLevel, [](uint32_t key) {
            int level = 0;
            msgbus::DataStore::GetInstance().Get(key, level);
            ESP_LOGI(TAG, "[LVGL] Battery: %d%%", level);
        })
    );

    // 3d. SubscriptionGroup — manage multiple subscriptions at once.
    {
        msgbus::SubscriptionGroup group;
        group.Add(bus.Subscribe(Topic::SensorData, [](const msgbus::Message&) {
            ESP_LOGD(TAG, "[Group] sensor tick");
        }));
        group.Add(bus.Subscribe(Topic::ButtonPress, [](const msgbus::Message&) {
            ESP_LOGD(TAG, "[Group] button tick");
        }, msgbus::DeliveryMode::Immediate));

        // group goes out of scope → all subscriptions auto-cancelled.
    }

    // 4. Start the simulated hardware task.
    xTaskCreate(hardware_task, "hw_task", 4096, nullptr, 5, nullptr);

    // 5. Main loop — in a real app the LVGL task handles this.
    ESP_LOGI(TAG, "Example running.  Sensor data every 500 ms.");
}
