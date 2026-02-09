/*
 * SPDX-FileCopyrightText: 2025 txp666
 * SPDX-License-Identifier: MIT
 *
 * lvgl-msg-bus — RAII subscription guard.
 */

#ifndef LVGL_MSG_BUS_SUBSCRIPTION_H
#define LVGL_MSG_BUS_SUBSCRIPTION_H

#include <utility>
#include <vector>

#include "lvgl_msg_bus/message_bus.h"

namespace msgbus {

// ---------------------------------------------------------------------------
// Subscription — single RAII guard
// ---------------------------------------------------------------------------

/**
 * @brief RAII wrapper around a MessageBus subscription.
 *
 * Automatically calls @c MessageBus::Unsubscribe() on destruction.
 * Move-only; cannot be copied.
 *
 * @code
 * class MyPage : public PageBase {
 *     msgbus::Subscription sensor_sub_;
 *
 *     void OnCreate(lv_obj_t* parent) override {
 *         sensor_sub_ = msgbus::Subscription(
 *             MessageBus::GetInstance().Subscribe(
 *                 Topic::SensorData,
 *                 [this](const Message& m) { OnSensorData(m); }));
 *     }
 *     // sensor_sub_ automatically unsubscribes when page is destroyed.
 * };
 * @endcode
 */
class Subscription {
public:
    /** @brief Default-construct an empty (invalid) subscription. */
    Subscription() = default;

    /** @brief Take ownership of an existing subscription id. */
    explicit Subscription(SubscriptionId id) : id_(id) {}

    /** @brief Unsubscribe on destruction. */
    ~Subscription() { Reset(); }

    // Move semantics.
    Subscription(Subscription&& other) noexcept : id_(other.id_) {
        other.id_ = kInvalidSubscription;
    }

    Subscription& operator=(Subscription&& other) noexcept {
        if (this != &other) {
            Reset();
            id_ = other.id_;
            other.id_ = kInvalidSubscription;
        }
        return *this;
    }

    // Non-copyable.
    Subscription(const Subscription&) = delete;
    Subscription& operator=(const Subscription&) = delete;

    /** @brief Unsubscribe immediately (idempotent). */
    void Reset() {
        if (id_ != kInvalidSubscription) {
            MessageBus::GetInstance().Unsubscribe(id_);
            id_ = kInvalidSubscription;
        }
    }

    /** @brief Return true if currently holding a valid subscription. */
    bool IsValid() const { return id_ != kInvalidSubscription; }

    /** @brief Return the underlying id (for debugging). */
    SubscriptionId Id() const { return id_; }

private:
    SubscriptionId id_ = kInvalidSubscription;
};

// ---------------------------------------------------------------------------
// SubscriptionGroup — manage multiple subscriptions together
// ---------------------------------------------------------------------------

/**
 * @brief Convenience container that holds multiple Subscription objects.
 *
 * All subscriptions are automatically cancelled when the group is destroyed
 * or when @c Clear() is called.
 *
 * @code
 * class MyPage : public PageBase {
 *     msgbus::SubscriptionGroup subs_;
 *
 *     void OnCreate(lv_obj_t* parent) override {
 *         auto& bus = MessageBus::GetInstance();
 *         subs_.Add(bus.Subscribe(Topic::SensorData,  [this](auto& m){ ... }));
 *         subs_.Add(bus.Subscribe(Topic::WifiStatus,   [this](auto& m){ ... }));
 *         subs_.Add(bus.Subscribe(Topic::BatteryStatus, [this](auto& m){ ... }));
 *     }
 *     // All three subscriptions are cancelled on page destroy.
 * };
 * @endcode
 */
class SubscriptionGroup {
public:
    SubscriptionGroup() = default;
    ~SubscriptionGroup() { Clear(); }

    // Move semantics.
    SubscriptionGroup(SubscriptionGroup&&) = default;
    SubscriptionGroup& operator=(SubscriptionGroup&&) = default;

    // Non-copyable.
    SubscriptionGroup(const SubscriptionGroup&) = delete;
    SubscriptionGroup& operator=(const SubscriptionGroup&) = delete;

    /** @brief Add a subscription id to the group. */
    void Add(SubscriptionId id) {
        if (id != kInvalidSubscription) {
            subs_.emplace_back(id);
        }
    }

    /** @brief Add an existing Subscription (takes ownership via move). */
    void Add(Subscription&& sub) {
        if (sub.IsValid()) {
            subs_.push_back(std::move(sub));
        }
    }

    /** @brief Cancel all subscriptions in the group. */
    void Clear() { subs_.clear(); }

    /** @brief Number of active subscriptions. */
    size_t Size() const { return subs_.size(); }

    /** @brief True if the group has no subscriptions. */
    bool Empty() const { return subs_.empty(); }

private:
    std::vector<Subscription> subs_;
};

} // namespace msgbus

#endif // LVGL_MSG_BUS_SUBSCRIPTION_H
