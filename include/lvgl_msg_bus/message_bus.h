/*
 * SPDX-FileCopyrightText: 2025 txp666
 * SPDX-License-Identifier: MIT
 *
 * lvgl-msg-bus — Thread-safe message bus for ESP-IDF + LVGL
 */

#ifndef LVGL_MSG_BUS_MESSAGE_BUS_H
#define LVGL_MSG_BUS_MESSAGE_BUS_H

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <vector>

#include <esp_err.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <lvgl.h>

namespace msgbus {

// ---------------------------------------------------------------------------
// Delivery mode
// ---------------------------------------------------------------------------

/**
 * @brief How a subscriber callback is invoked.
 *
 * - Immediate : called synchronously in the publisher's thread.
 * - LvglAsync : dispatched to the LVGL thread via lv_async_call().
 */
enum class DeliveryMode {
    Immediate,
    LvglAsync,
};

// ---------------------------------------------------------------------------
// Message
// ---------------------------------------------------------------------------

/**
 * @brief Read-only message delivered to subscribers.
 *
 * The @c data pointer is valid only for the duration of the callback.
 * For @c LvglAsync delivery the bus makes an internal copy that is freed
 * automatically after the callback returns.
 */
struct Message {
    uint32_t    topic;      ///< Topic identifier.
    const void* data;       ///< Payload (may be nullptr).
    size_t      data_size;  ///< Payload size in bytes.
    uint32_t    timestamp;  ///< xTaskGetTickCount() at publish time.

    /**
     * @brief Convenience cast — caller is responsible for type safety.
     */
    template <typename T>
    const T& As() const {
        return *static_cast<const T*>(data);
    }
};

// ---------------------------------------------------------------------------
// Callback type and subscription handle
// ---------------------------------------------------------------------------

using MessageCallback  = std::function<void(const Message&)>;
using SubscriptionId   = uint32_t;

/// Reserved value that represents "no subscription".
static constexpr SubscriptionId kInvalidSubscription = 0;

// ---------------------------------------------------------------------------
// Bus configuration
// ---------------------------------------------------------------------------

/**
 * @brief Optional tunables passed to MessageBus::Initialize().
 */
struct BusConfig {
    size_t max_subscribers  = 32;   ///< Pre-reserved subscriber slots.
    size_t max_data_size    = 512;  ///< Max payload bytes per Publish().
};

// ---------------------------------------------------------------------------
// MessageBus
// ---------------------------------------------------------------------------

/**
 * @brief Singleton publish / subscribe message bus.
 *
 * Thread safety
 * -------------
 * - Subscribe() / Unsubscribe() / Publish() may be called from **any** thread
 *   or FreeRTOS task (but **not** from ISR).
 * - Subscriber callbacks with @c DeliveryMode::Immediate execute in the
 *   publisher's thread; the caller must ensure any shared state is protected.
 * - Subscriber callbacks with @c DeliveryMode::LvglAsync are guaranteed to
 *   execute inside the LVGL task context (via @c lv_async_call()).
 */
class MessageBus {
public:
    /**
     * @brief Access the singleton instance.
     */
    static MessageBus& GetInstance();

    /**
     * @brief One-time initialisation (call before any Subscribe / Publish).
     * @param config  Optional tuning parameters.
     * @return ESP_OK on success, ESP_ERR_INVALID_STATE if already initialised.
     */
    esp_err_t Initialize(const BusConfig& config = {});

    /**
     * @brief Register a callback for a topic.
     * @param topic    Numeric topic identifier.
     * @param cb       Callback to invoke when a message is published.
     * @param mode     Delivery mode (default: dispatched to LVGL thread).
     * @return A unique SubscriptionId (never 0), or kInvalidSubscription on error.
     */
    SubscriptionId Subscribe(uint32_t topic, MessageCallback cb,
                             DeliveryMode mode = DeliveryMode::LvglAsync);

    /**
     * @brief Remove a subscription.
     *
     * Safe to call with @c kInvalidSubscription (no-op).
     * After this call returns the callback will never be invoked again.
     */
    void Unsubscribe(SubscriptionId id);

    /**
     * @brief Publish a message to all subscribers of @p topic.
     * @param topic  Topic identifier.
     * @param data   Pointer to payload (may be nullptr).
     * @param size   Payload size in bytes (0 when data is nullptr).
     */
    void Publish(uint32_t topic, const void* data = nullptr, size_t size = 0);

    /**
     * @brief Convenience: publish a typed value.
     */
    template <typename T>
    void Publish(uint32_t topic, const T& value) {
        Publish(topic, &value, sizeof(T));
    }

    /** @brief Return true if Initialize() has been called successfully. */
    bool IsInitialized() const { return initialized_; }

private:
    MessageBus() = default;
    ~MessageBus();
    MessageBus(const MessageBus&) = delete;
    MessageBus& operator=(const MessageBus&) = delete;

    // --- internal types -----------------------------------------------------

    struct SubscriberEntry {
        SubscriptionId  id;
        uint32_t        topic;
        MessageCallback callback;
        DeliveryMode    mode;
    };

    /// Data block queued for lv_async_call().
    struct AsyncPayload {
        MessageCallback callback;
        uint32_t        topic;
        uint32_t        timestamp;
        size_t          data_size;
        // Followed by `data_size` bytes of payload (flexible member).
        void* DataPtr() { return reinterpret_cast<uint8_t*>(this) + sizeof(AsyncPayload); }
        const void* DataPtr() const { return reinterpret_cast<const uint8_t*>(this) + sizeof(AsyncPayload); }
    };

    static void LvglAsyncCb(void* user_data);

    // --- data ---------------------------------------------------------------

    bool                          initialized_ = false;
    BusConfig                     config_{};
    SemaphoreHandle_t             mutex_ = nullptr;
    std::vector<SubscriberEntry>  subscribers_;
    SubscriptionId                next_id_ = 1;
};

} // namespace msgbus

#endif // LVGL_MSG_BUS_MESSAGE_BUS_H
