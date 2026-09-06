# icu_post - reusable power-on self-test for an ICU-20201 reached via SonicLib.
# SonicLib-level only (no esp-idf/BSP calls beyond esp_log); any app can call post_run() at boot.
# See docs/hardware_bringup.md.

idf_component_register(
    SRCS "icu_post.c"
    INCLUDE_DIRS "include"
    REQUIRES invn-soniclib log
)
