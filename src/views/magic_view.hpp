#pragma once

#include <lvgl.h>
#include <memory>

namespace ir_remote {

class MagicView {
public:
    explicit MagicView(lv_obj_t* parent);
    ~MagicView();

    void generate(uint32_t magicSerial);
    void tick(uint32_t nowMs);

private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
};

}  // namespace ir_remote
