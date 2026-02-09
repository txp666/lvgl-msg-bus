# lvgl-msg-bus: thread-safe message bus & reactive data store for ESP-IDF + LVGL.

idf_component_register(
    SRCS
        "src/message_bus.cc"
        "src/data_store.cc"
        "src/subscription.cc"
    INCLUDE_DIRS
        "include"
        "include/lvgl_msg_bus"
    REQUIRES
        lvgl
        log
)
