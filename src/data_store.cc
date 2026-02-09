/*
 * SPDX-FileCopyrightText: 2025 txp666
 * SPDX-License-Identifier: MIT
 */

#include "lvgl_msg_bus/data_store.h"

#include <cstring>

#include <esp_log.h>

static const char* TAG = "DataStore";

namespace msgbus {

// ---------------------------------------------------------------------------
// Singleton
// ---------------------------------------------------------------------------

DataStore& DataStore::GetInstance() {
    static DataStore instance;
    return instance;
}

DataStore::~DataStore() {
    if (mutex_) {
        vSemaphoreDelete(mutex_);
        mutex_ = nullptr;
    }
}

// ---------------------------------------------------------------------------
// Initialize
// ---------------------------------------------------------------------------

esp_err_t DataStore::Initialize(const DataStoreConfig& config,
                                uint32_t topic_base) {
    if (initialized_) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_ERR_INVALID_STATE;
    }

    config_     = config;
    topic_base_ = topic_base;

    mutex_ = xSemaphoreCreateMutex();
    if (!mutex_) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    initialized_ = true;
    ESP_LOGI(TAG, "Initialized (max_entry=%u, topic_base=0x%04lx)",
             (unsigned)config_.max_entry_size, (unsigned long)topic_base_);
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// SetRaw
// ---------------------------------------------------------------------------

void DataStore::SetRaw(uint32_t key, const void* data, size_t size) {
    if (!initialized_ || !data || size == 0) {
        return;
    }
    if (size > config_.max_entry_size) {
        ESP_LOGW(TAG, "Value too large for key 0x%04lx (%u > %u)",
                 (unsigned long)key, (unsigned)size,
                 (unsigned)config_.max_entry_size);
        return;
    }

    bool changed = false;

    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "Set: mutex timeout");
        return;
    }

    auto it = entries_.find(key);
    if (it == entries_.end()) {
        // New entry.
        Entry entry;
        entry.data.assign(static_cast<const uint8_t*>(data),
                          static_cast<const uint8_t*>(data) + size);
        entries_.emplace(key, std::move(entry));
        changed = true;
    } else {
        // Existing entry — only update if value differs.
        auto& existing = it->second.data;
        if (existing.size() != size ||
            memcmp(existing.data(), data, size) != 0) {
            existing.assign(static_cast<const uint8_t*>(data),
                            static_cast<const uint8_t*>(data) + size);
            changed = true;
        }
    }

    xSemaphoreGive(mutex_);

    // Publish change notification outside the lock.
    if (changed) {
        MessageBus::GetInstance().Publish(topic_base_ + key, data, size);
    }
}

// ---------------------------------------------------------------------------
// GetRaw
// ---------------------------------------------------------------------------

bool DataStore::GetRaw(uint32_t key, void* out, size_t size) const {
    if (!initialized_ || !out || size == 0) {
        return false;
    }

    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "Get: mutex timeout");
        return false;
    }

    bool ok = false;
    auto it = entries_.find(key);
    if (it != entries_.end() && it->second.data.size() == size) {
        memcpy(out, it->second.data.data(), size);
        ok = true;
    }

    xSemaphoreGive(mutex_);
    return ok;
}

// ---------------------------------------------------------------------------
// Watch / Unwatch
// ---------------------------------------------------------------------------

SubscriptionId DataStore::Watch(uint32_t key,
                                std::function<void(uint32_t key)> callback) {
    if (!initialized_ || !callback) {
        return kInvalidSubscription;
    }

    const uint32_t topic = topic_base_ + key;

    // Wrap the user callback so it receives just the key.
    return MessageBus::GetInstance().Subscribe(
        topic,
        [key, cb = std::move(callback)](const Message& /*msg*/) { cb(key); },
        DeliveryMode::LvglAsync);
}

void DataStore::Unwatch(SubscriptionId id) {
    MessageBus::GetInstance().Unsubscribe(id);
}

// ---------------------------------------------------------------------------
// Contains / Remove
// ---------------------------------------------------------------------------

bool DataStore::Contains(uint32_t key) const {
    if (!initialized_) {
        return false;
    }

    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return false;
    }

    bool found = entries_.count(key) > 0;

    xSemaphoreGive(mutex_);
    return found;
}

void DataStore::Remove(uint32_t key) {
    if (!initialized_) {
        return;
    }

    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "Remove: mutex timeout");
        return;
    }

    entries_.erase(key);

    xSemaphoreGive(mutex_);
}

} // namespace msgbus
