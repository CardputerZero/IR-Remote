#include "core/ir_remote_app.hpp"
#include "hal/ir_remote_lvgl_hal.hpp"
#include "input/ir_remote_keypad.hpp"
#include <core/hal/hal.hpp>
#include <lvgl.h>
#include <spdlog/spdlog.h>
#include <stdio.h>
#include <unistd.h>

int main()
{
    constexpr int32_t kScreenWidth  = 320;
    constexpr int32_t kScreenHeight = 170;

    lv_init();
    if (!ir_remote::initLvglHal(kScreenWidth, kScreenHeight)) {
        return 1;
    }

    lv_display_t* disp = lv_display_get_default();
    if (disp == nullptr) {
        fprintf(stderr, "IR Remote: failed to create LVGL display\n");
        return 1;
    }

    spdlog::info("IR Remote: display {}x{}", static_cast<int>(lv_display_get_horizontal_resolution(disp)),
                 static_cast<int>(lv_display_get_vertical_resolution(disp)));

    smooth_ui_toolkit::ui_hal::on_get_tick([]() { return lv_tick_get(); });
    smooth_ui_toolkit::ui_hal::on_delay([](uint32_t ms) { usleep(ms * 1000); });

    ir_remote::IRRemoteApp app;

#if !LV_USE_SDL
    ir_remote::IRRemoteKeypad keypad;
    keypad.setKeyCallback(
        [&app](uint32_t key, const char* utf8, bool pressed) { return app.onLvglKeyState(key, utf8, pressed); });
    keypad.openDefault();
#endif

    app.start();
    lv_obj_invalidate(lv_screen_active());

    while (!app.quitRequested()) {
#if !LV_USE_SDL
        keypad.poll();
#endif
        lv_timer_handler();
        app.tick(lv_tick_get());
        usleep(10000);
    }

    spdlog::info("IR Remote: exit requested");
    ir_remote::shutdownLvglHal();
    return 0;
}
