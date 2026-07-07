#pragma once

#include "view_models/detail_view_model.hpp"
#include "views/bottom_key_bar.hpp"
#include "views/view.hpp"
#include <core/animation/animate_value/animate_value.hpp>
#include <lvgl.h>
#include <lvgl/lvgl_cpp/label.hpp>
#include <lvgl/lvgl_cpp/obj.hpp>
#include <memory>
#include <vector>

namespace ir_remote {

class DetailView : public View {
public:
    explicit DetailView(DetailViewModel& vm);
    ~DetailView() override;

    void onEnter(lv_obj_t* parent) override;
    void onExit() override;
    void tick(uint32_t nowMs) override;

private:
    class WaveformView;
    class SendToast;

    DetailViewModel& _vm;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Container> _root;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Container> _fade_mask;
    smooth_ui_toolkit::AnimateValue _fade_mask_opacity;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _title_label;
    std::unique_ptr<WaveformView> _waveform;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _protocol_label;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _carrier_label;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _duration_label;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _pulse_label;
    std::unique_ptr<SendToast> _send_toast;
    std::unique_ptr<BottomKeyBar> _key_bar;
    uint32_t _play_serial_seen = 0;

    void destroy();
    void renderFile(const IrRecordingFile& file);
    void renderSignal(const IrSignal& signal);
    void renderDecodeResult(const IrProtocolDecodeResult& result);
    void renderPlay(uint32_t serial);
    static void onFileChanged(void* context, const IrRecordingFile& file);
    static void onSignalChanged(void* context, const IrSignal& signal);
    static void onDecodeResultChanged(void* context, const IrProtocolDecodeResult& result);
    static void onPlaySerialChanged(void* context, const uint32_t& serial);
};

}  // namespace ir_remote
