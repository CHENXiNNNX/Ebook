# =============================================================================
# main 组件源文件 — Ebook 框架
# =============================================================================

set(SOURCES
    "main.cc"

    # ── ebook 入口 / boot ───────────────────────────────────────────────
    "app/ebook/ebook_app.cc"
    "app/ebook/platform/boot.cc"
    "app/ebook/platform/housekeeper.cc"
    "app/ebook/platform/battery_sampler.cc"
    "app/ebook/platform/audio_codec.cc"
    "app/ebook/platform/music_player.cc"
    "app/ebook/platform/wooden_fish_sound.cc"

    # ── 显示 / 合成 / 上屏 ───────────────────────────────────────────────
    "app/ebook/display/display_port.cc"
    "app/ebook/composer/composer.cc"
    "app/ebook/presenter/present_plan.cc"
    "app/ebook/presenter/presenter.cc"

    # ── 路由 / 刷新边表 ─────────────────────────────────────────────────
    "app/ebook/router/page_registry.cc"
    "app/ebook/router/refresh_intent.cc"
    "app/ebook/router/refresh_edges.cc"
    "app/ebook/router/transition.cc"
    "app/ebook/router/route_stack.cc"
    "app/ebook/router/router.cc"

    # ── Shell 页面 ───────────────────────────────────────────────────────
    "app/ebook/shell/lock_page.cc"
    "app/ebook/shell/home_page.cc"
    "app/ebook/shell/app_grid_page.cc"
    "app/ebook/shell/app_host_page.cc"

    # ── gfx / input ─────────────────────────────────────────────────────
    "app/ebook/gfx/canvas.cc"
    "app/ebook/gfx/font.cc"
    "app/ebook/gfx/bitmap_font.cc"
    "app/ebook/gfx/text_layout.cc"
    "app/ebook/input/gesture_tuner.cc"
    "app/ebook/input/input_router.cc"
    "app/ebook/input/physical_types.cc"
    "app/ebook/input/physical_input.cc"
    "app/ebook/input/input_bindings.cc"
    "app/ebook/input/key_bindings.cc"
    "app/ebook/input/input_dispatch.cc"

    # ── ui ──────────────────────────────────────────────────────────────
    "app/ebook/ui/ui_bus.cc"
    "app/ebook/ui/ui_loop.cc"
    "app/ebook/ui/list_view.cc"
    "app/ebook/ui/widgets.cc"

    # ── overlays ──────────────────────────────────────────────────
    "app/ebook/overlays/status_bar.cc"
    "app/ebook/overlays/control_center.cc"
    "app/ebook/overlays/keyboard.cc"
    "app/ebook/overlays/toast.cc"
    "app/ebook/overlays/confirm_prompt.cc"

    # ── apps ─────────────────────────────────────────────────────────────
    "app/ebook/apps/app.cc"
    "app/ebook/apps/app_registry.cc"
    "app/ebook/apps/stub_app.cc"
    "app/ebook/apps/drawing/drawing_app.cc"
    "app/ebook/apps/files/files_app.cc"
    "app/ebook/apps/files/vfs_browser.cc"
    "app/ebook/apps/reader/book_shelf.cc"
    "app/ebook/apps/reader/reader_menu.cc"
    "app/ebook/apps/reader/reader_app.cc"
    "app/ebook/apps/reader/txt_book.cc"
    "app/ebook/apps/settings/auto_lock.cc"
    "app/ebook/apps/settings/power_save.cc"
    "app/ebook/apps/settings/lock_password.cc"
    "app/ebook/apps/settings/settings_app.cc"
    "app/ebook/apps/settings/settings_common.cc"
    "app/ebook/apps/settings/settings_security.cc"
    "app/ebook/apps/settings/settings_wifi.cc"
    "app/ebook/apps/settings/settings_hotspot.cc"
    "app/ebook/apps/settings/settings_bluetooth.cc"
    "app/ebook/apps/settings/settings_display.cc"
    "app/ebook/apps/settings/settings_key_bindings.cc"
    "app/ebook/apps/settings/settings_sound.cc"
    "app/ebook/apps/settings/settings_time.cc"
    "app/ebook/apps/settings/settings_storage.cc"
    "app/ebook/apps/settings/settings_about.cc"
    "app/ebook/apps/music/music_app.cc"
    "app/ebook/apps/notepad/notepad_app.cc"
    "app/ebook/apps/gallery/gallery_app.cc"
    "app/ebook/apps/wooden_fish/wooden_fish_app.cc"
    "app/ebook/apps/clock/clock_app.cc"
    "app/ebook/apps/clock/clock_store.cc"
    "app/ebook/apps/weather/weather_app.cc"
    "app/ebook/apps/weather/weather_cities.cc"
    "app/ebook/apps/calendar/calendar_app.cc"

    # ── data + text ────────────────────────────────────────────────────
    "app/ebook/data/persist.cc"
    "app/ebook/data/system_state.cc"
    "app/ebook/data/clock_provider.cc"
    "app/ebook/apps/reader/reading_store.cc"
    "app/ebook/apps/reader/txt_index_cache.cc"
    "app/ebook/apps/music/music_library.cc"
    "app/ebook/apps/music/music_store.cc"
    "app/ebook/text/path_encoding.cc"

    # ── app/network / protocol / system ───────────────────────────────
    "app/network/bluetooth/bluetooth.cc"
    "app/network/bluetooth/gatt/gatt.cc"
    "app/network/wifi/wifi.cc"
    "app/protocol/file_server/file_server.cc"
    "app/protocol/http/http.cc"
    "app/protocol/ntp/ntp.cc"
    "app/protocol/websocket/websocket.cc"
    "app/system/event/event.cc"
    "app/system/info/info.cc"
    "app/system/power/power.cc"
    "app/system/task/task.cc"

    # ── bsp ─────────────────────────────────────────────────────────────
    "bsp/driver/qmi8658a/qmi8658a.cc"
    "bsp/driver/ft6336u/ft6336u.cc"
    "bsp/driver/ft6336u/touch_gesture.cc"
    "bsp/driver/gdey027t91/gdey027t91.cc"
    "bsp/driver/gdey027t91/backlight.cc"
    "bsp/driver/button/button.cc"
    "bsp/i2c/i2c.cc"
    "bsp/battery/battery.cc"
    "bsp/driver/audio/es8311/es8311_codec.cc"

    # ── common ──────────────────────────────────────────────────────────
    "common/memory/memory.cc"
    "common/ota/ota.cc"
    "common/storage/storage.cc"
    "common/storage/storage_usb_msc.cc"
    "common/time/time.cc"
    "common/uuid/uuid.cc"
)

set(INCLUDE_DIRS
    "."
    "app"
    "app/ebook"
    "app/network/bluetooth"
    "app/network/bluetooth/gatt"
    "app/network/wifi"
    "app/protocol/file_server"
    "app/protocol/http"
    "app/protocol/ntp"
    "app/protocol/websocket"
    "app/system/event"
    "app/system/info"
    "app/system/power"
    "app/system/task"
    "bsp"
    "bsp/config"
    "bsp/driver/qmi8658a"
    "bsp/driver/ft6336u"
    "bsp/driver/gdey027t91"
    "bsp/driver/button"
    "bsp/i2c"
    "bsp/battery"
    "bsp/driver/audio/es8311"
    "common"
    "common/memory"
    "common/memory/double_buffer"
    "common/memory/ring_buffer"
    "common/ota"
    "common/storage"
    "common/time"
    "common/uuid"
)

idf_component_register(
    SRCS ${SOURCES}
    INCLUDE_DIRS ${INCLUDE_DIRS}
)

if(CONFIG_EBOOK_FLASH_EMBED_ASSETS)
    littlefs_create_partition_image(assets ../assets/littlefs_assets FLASH_IN_PROJECT)
endif()

if(CONFIG_EBOOK_FLASH_EMBED_USERDATA)
    fatfs_create_spiflash_image(userdata ../assets/fat_userdata FLASH_IN_PROJECT)
endif()
