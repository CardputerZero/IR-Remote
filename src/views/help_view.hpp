#pragma once

#include <lvgl.h>

namespace ir_remote {

/** A small application-level help overlay shown by the Linux KEY_HELP event. */
class HelpView {
public:
    explicit HelpView(lv_obj_t* parent);
    ~HelpView();

    HelpView(const HelpView&) = delete;
    HelpView& operator=(const HelpView&) = delete;

    bool active() const
    {
        return _active;
    }

    void show();
    void hide();
    void toggle();

private:
    lv_obj_t* _overlay = nullptr;
    bool _active = false;
};

}  // namespace ir_remote
