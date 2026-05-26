# PRE Buddy ESP-IDF main component.
#
# Skeleton. Picked up by idf.py only. The pre_buddy_core/ headers from
# ../../core/include are added as include dirs so #include "pre_buddy/..."
# resolves to the shared host-testable core.

idf_component_register(
    SRCS
        "main.cpp"
        "esp32_servo.cpp"
        "esp32_led.cpp"
        "esp32_display.cpp"
        "esp32_ble.cpp"
        "esp32_character_store.cpp"
    INCLUDE_DIRS
        "."
        "${CMAKE_CURRENT_LIST_DIR}/../../core/include"
    REQUIRES
        bt          # NimBLE / Bluedroid host
        driver      # LEDC for servo PWM
        esp_timer
        nvs_flash
)
