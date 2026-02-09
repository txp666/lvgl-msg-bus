/*
 * SPDX-FileCopyrightText: 2025 txp666
 * SPDX-License-Identifier: MIT
 *
 * lvgl-msg-bus — Thread-safe reactive data store.
 */

#ifndef LVGL_MSG_BUS_DATA_STORE_H
#define LVGL_MSG_BUS_DATA_STORE_H

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <vector>

#include <esp_err.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "lvgl_msg_bus/message_bus.h"

namespace msgbus {

// ---------------------------------------------------------------------------
// DataStore configuration
// ---------------------------------------------------------------------------

/**
 * @brief Optional tunables for DataStore::Initialize().
 */
struct DataStoreConfig {
    size_t max_entry_size = 256;  ///< Maximum value size in bytes.
};

// ---------------------------------------------------------------------------
// DataStore
// ---------------------------------------------------------------------------

/**
 * @brief Thread-safe key-value store with automatic change notifications.
 *
 * Stores binary blobs keyed by @c uint32_t.  Every successful @c Set()
 * publishes a change notification through the MessageBus so that subscribers
 * (typically UI pages) are informed automatically.
 *
 * Topic convention
 * ----------------
 * Change notifications are published on topic = @c (kDataStoreTopicBase + key).
 * The notification payload is the new value.
 *
 * Thread safety
 * -------------
 * All public methods are safe to call from any FreeRTOS task.
 */
class DataStore {
public:
    /**
     * @brief Base topic offset for change notifications.
     *
     * Override before Initialize() if the default collides with your
     * application topics.  The effective topic for key @c k is
     * @c (topic_base + k).
     */
    static constexpr uint32_t kDefaultTopicBase = 0x8000;

    /**
     * @brief Access the singleton.
     */
    static DataStore& GetInstance();

    /**
     * @brief One-time initialisation.
     * @param config     Optional tunables.
     * @param topic_base Base added to each key to form the MessageBus topic.
     * @return ESP_OK on success.
     */
    esp_err_t Initialize(const DataStoreConfig& config = {},
                         uint32_t topic_base = kDefaultTopicBase);

    // ---- typed helpers (inline, header-only) --------------------------------

    /**
     * @brief Store a value and publish a change notification.
     *
     * If the new value differs from the currently stored one the change
     * notification is published via MessageBus.  If they are identical the
     * write is skipped (no notification).
     */
    template <typename T>
    void Set(uint32_t key, const T& value) {
        SetRaw(key, &value, sizeof(T));
    }

    /**
     * @brief Read a previously stored value.
     * @return @c true if the key exists and the size matches.
     */
    template <typename T>
    bool Get(uint32_t key, T& out) const {
        return GetRaw(key, &out, sizeof(T));
    }

    /**
     * @brief Watch a key for changes.  The callback runs in the LVGL thread.
     *
     * This is a convenience wrapper around
     * @c MessageBus::Subscribe(topic_base + key, ...).
     *
     * @return SubscriptionId for later Unwatch().
     */
    SubscriptionId Watch(uint32_t key,
                         std::function<void(uint32_t key)> callback);

    /**
     * @brief Remove a watch previously registered with Watch().
     */
    void Unwatch(SubscriptionId id);

    /**
     * @brief Check if a key exists in the store.
     */
    bool Contains(uint32_t key) const;

    /**
     * @brief Remove a key and its value from the store.
     */
    void Remove(uint32_t key);

    /** @brief Return true if Initialize() has been called. */
    bool IsInitialized() const { return initialized_; }

    /** @brief Topic base currently in use. */
    uint32_t GetTopicBase() const { return topic_base_; }

    // ---- raw API (for variable-size data) -----------------------------------

    void SetRaw(uint32_t key, const void* data, size_t size);
    bool GetRaw(uint32_t key, void* out, size_t size) const;

private:
    DataStore() = default;
    ~DataStore();
    DataStore(const DataStore&) = delete;
    DataStore& operator=(const DataStore&) = delete;

    struct Entry {
        std::vector<uint8_t> data;
    };

    bool                          initialized_ = false;
    DataStoreConfig               config_{};
    uint32_t                      topic_base_ = kDefaultTopicBase;
    mutable SemaphoreHandle_t     mutex_ = nullptr;
    std::map<uint32_t, Entry>     entries_;
};

} // namespace msgbus

#endif // LVGL_MSG_BUS_DATA_STORE_H
