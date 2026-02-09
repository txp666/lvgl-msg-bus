/*
 * SPDX-FileCopyrightText: 2025 txp666
 * SPDX-License-Identifier: MIT
 *
 * Subscription / SubscriptionGroup are fully inline (header-only).
 * This translation unit exists so that the component always has a .cc file
 * for the build system, and provides a place for future non-inline helpers.
 */

#include "lvgl_msg_bus/subscription.h"

// Intentionally empty — all current logic is in the header.
