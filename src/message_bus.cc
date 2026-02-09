/*
 * SPDX-FileCopyrightText: 2025 txp666
 * SPDX-License-Identifier: MIT
 */

#include "lvgl_msg_bus/message_bus.h"

#include <cstdlib>
#include <cstring>

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <lvgl.h>

static const char* TAG = "MsgBus";

namespace msgbus {

// ---------------------------------------------------------------------------
// Singleton
// ---------------------------------------------------------------------------

MessageBus& MessageBus::GetInstance() {
    static MessageBus instance;
    return instance;
}

MessageBus::~MessageBus() {
    if (mutex_) {
        vSemaphoreDelete(mutex_);
        mutex_ = nullptr;
    }
}

// ---------------------------------------------------------------------------
// Initialize
// ---------------------------------------------------------------------------

esp_err_t MessageBus::Initialize(const BusConfig& config) {
    if (initialized_) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_ERR_INVALID_STATE;
    }

    config_ = config;

    mutex_ = xSemaphoreCreateMutex();
    if (!mutex_) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    subscribers_.reserve(config_.max_subscribers);
    initialized_ = true;

    ESP_LOGI(TAG, "Initialized (max_subscribers=%u, max_data=%u)",
             (unsigned)config_.max_subscribers, (unsigned)config_.max_data_size);
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Subscribe
// ---------------------------------------------------------------------------

SubscriptionId MessageBus::Subscribe(uint32_t topic, MessageCallback cb,
                                     DeliveryMode mode,
                                     uint32_t min_interval_ms) {
    if (!initialized_ || !cb) {
        ESP_LOGW(TAG, "Subscribe failed: bus %s, callback %s",
                 initialized_ ? "ok" : "not init", cb ? "ok" : "null");
        return kInvalidSubscription;
    }

    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "Subscribe: mutex timeout");
        return kInvalidSubscription;
    }

    SubscriptionId id = next_id_++;
    // Wrap-around guard (skip 0).
    if (next_id_ == kInvalidSubscription) {
        next_id_ = 1;
    }

    const uint32_t interval_ticks =
        min_interval_ms > 0 ? pdMS_TO_TICKS(min_interval_ms) : 0;

    subscribers_.push_back(
        {id, topic, std::move(cb), mode, interval_ticks, 0});

    xSemaphoreGive(mutex_);

    ESP_LOGD(TAG, "Subscribed id=%lu topic=0x%04lx mode=%d interval=%lums",
             (unsigned long)id, (unsigned long)topic,
             static_cast<int>(mode), (unsigned long)min_interval_ms);
    return id;
}

// ---------------------------------------------------------------------------
// Unsubscribe
// ---------------------------------------------------------------------------

void MessageBus::Unsubscribe(SubscriptionId id) {
    if (id == kInvalidSubscription || !initialized_) {
        return;
    }

    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "Unsubscribe: mutex timeout");
        return;
    }

    for (auto it = subscribers_.begin(); it != subscribers_.end(); ++it) {
        if (it->id == id) {
            subscribers_.erase(it);
            ESP_LOGD(TAG, "Unsubscribed id=%lu", (unsigned long)id);
            break;
        }
    }

    xSemaphoreGive(mutex_);
}

// ---------------------------------------------------------------------------
// Publish
// ---------------------------------------------------------------------------

void MessageBus::Publish(uint32_t topic, const void* data, size_t size) {
    if (!initialized_) {
        return;
    }

    if (size > config_.max_data_size) {
        ESP_LOGW(TAG, "Payload too large (%u > %u), truncated",
                 (unsigned)size, (unsigned)config_.max_data_size);
        size = config_.max_data_size;
    }

    const uint32_t now = xTaskGetTickCount();

    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "Publish: mutex timeout");
        return;
    }

    // Snapshot matching subscribers while holding the lock.
    // Use a small local vector to avoid allocation in the hot path for
    // typical subscriber counts.  If there are more, it will heap-allocate.
    std::vector<SubscriberEntry> matches;
    matches.reserve(4);
    for (auto& sub : subscribers_) {
        if (sub.topic == topic) {
            // Per-subscriber throttle: skip if interval not yet elapsed.
            if (sub.min_interval_ticks > 0) {
                const uint32_t elapsed = now - sub.last_delivery_tick;
                if (elapsed < sub.min_interval_ticks) {
                    continue;
                }
            }
            sub.last_delivery_tick = now;
            matches.push_back(sub);
        }
    }

    xSemaphoreGive(mutex_);

    if (matches.empty()) {
        return;
    }

    // Deliver to each matching subscriber outside the lock.
    for (const auto& sub : matches) {
        if (sub.mode == DeliveryMode::Immediate) {
            // Synchronous delivery in caller's thread.
            Message msg{topic, data, size, now};
            sub.callback(msg);
        } else {
            // Asynchronous delivery via LVGL thread.
            const size_t alloc_size = sizeof(AsyncPayload) + size;
            auto* payload = static_cast<AsyncPayload*>(malloc(alloc_size));
            if (!payload) {
                ESP_LOGE(TAG, "Async alloc failed (%u bytes)", (unsigned)alloc_size);
                continue;
            }

            // Placement-construct the callback (std::function).
            new (&payload->callback) MessageCallback(sub.callback);
            payload->topic     = topic;
            payload->timestamp = now;
            payload->data_size = size;
            if (size > 0 && data) {
                memcpy(payload->DataPtr(), data, size);
            }

            lv_async_call(LvglAsyncCb, payload);
        }
    }
}

// ---------------------------------------------------------------------------
// LVGL async callback (runs in LVGL task)
// ---------------------------------------------------------------------------

void MessageBus::LvglAsyncCb(void* user_data) {
    auto* payload = static_cast<AsyncPayload*>(user_data);
    if (!payload) {
        return;
    }

    Message msg{
        payload->topic,
        payload->data_size > 0 ? payload->DataPtr() : nullptr,
        payload->data_size,
        payload->timestamp,
    };

    if (payload->callback) {
        payload->callback(msg);
    }

    // Destroy the std::function and free the block.
    payload->callback.~MessageCallback();
    free(payload);
}

} // namespace msgbus
