#include "views/detail_view.hpp"
#include "assets/assets.h"
#include <core/color/color.hpp>
#include <core/easing/ease.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <string>

namespace ir_remote {

namespace {

constexpr int32_t kTitleWidth           = 250;
constexpr int32_t kTitleY               = 6;
constexpr int32_t kWaveformWidth        = 280;
constexpr int32_t kWaveformHeight       = 58;
constexpr int32_t kWaveformX            = 0;
constexpr int32_t kWaveformY            = -32;
constexpr int32_t kWaveformHighOffset   = -15;
constexpr int32_t kWaveformLowOffset    = 15;
constexpr int32_t kWaveformLineWidth    = 1;
constexpr uint32_t kWaveformIdleColor   = 0xFED40D;
constexpr uint32_t kWaveformPlayColor   = 0x557AFF;
constexpr float kWaveformColorDuration  = 0.34f;
constexpr int32_t kInfoLeftX            = 20;
constexpr int32_t kInfoRightX           = 160;
constexpr int32_t kInfoRow1Y            = 91;
constexpr int32_t kInfoRow2Y            = 107;
constexpr int32_t kInfoHalfWidth        = 140;
constexpr uint32_t kInfoTextColor       = 0x8B8B8B;
constexpr int32_t kSendToastWidth       = 96;
constexpr int32_t kSendToastHeight      = 22;
constexpr int32_t kSendToastHiddenWidth = 12;
constexpr int32_t kSendToastHiddenHeight = 12;
constexpr int32_t kSendToastX           = 56;
constexpr int32_t kSendToastY           = 34;
constexpr int32_t kSendToastHiddenY     = 64;
constexpr uint32_t kSendToastBgColor    = 0x557AFF;
constexpr uint32_t kSendToastTextColor  = 0xFFFFFF;
constexpr uint32_t kSendToastHoldMs     = 1500;
constexpr float kRootFadeDuration       = 0.18f;

lv_opa_t fadeMaskOpacityFromFloat(float value)
{
    return static_cast<lv_opa_t>(std::clamp(static_cast<int>(std::round(value)), 0, 255));
}

uint64_t totalDurationUs(const IrSignal& signal)
{
    uint64_t total = 0;
    for (const auto& pulse : signal.pulses) {
        total += pulse.durationUs;
    }
    return total;
}

std::string durationText(uint64_t durationUs)
{
    if (durationUs >= 1000000) {
        char buffer[24] = {};
        std::snprintf(buffer, sizeof(buffer), "%.2fs", static_cast<double>(durationUs) / 1000000.0);
        return buffer;
    }
    return std::to_string(static_cast<unsigned long long>((durationUs + 500) / 1000)) + "ms";
}

std::string carrierText(uint32_t carrierHz)
{
    char buffer[24] = {};
    std::snprintf(buffer, sizeof(buffer), "%ukHz", static_cast<unsigned>((carrierHz + 500) / 1000));
    return buffer;
}

std::string hexText(uint32_t value)
{
    std::ostringstream stream;
    stream << "0x" << std::uppercase << std::hex << value;
    return stream.str();
}

}  // namespace

class DetailView::WaveformView {
public:
    explicit WaveformView(lv_obj_t* parent)
        : _panel(std::make_unique<smooth_ui_toolkit::lvgl_cpp::Container>(parent)), _color(kWaveformIdleColor)
    {
        _color.duration       = kWaveformColorDuration;
        _color.easingFunction = smooth_ui_toolkit::ease::ease_out_quad;
        _color.begin();

        _panel->setSize(kWaveformWidth, kWaveformHeight);
        _panel->align(LV_ALIGN_CENTER, kWaveformX, kWaveformY);
        _panel->setBgOpa(LV_OPA_TRANSP);
        _panel->setBorderWidth(0);
        _panel->setShadowWidth(0);
        _panel->setPaddingAll(0);
        _panel->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
        _panel->addEventCb(onDraw, LV_EVENT_DRAW_MAIN, this);
    }

    void load(const IrSignal& signal)
    {
        _signal = signal;
        lv_obj_invalidate(_panel->raw_ptr());
    }

    void flashPlay()
    {
        _color.teleport(kWaveformPlayColor);
        _color.move(kWaveformIdleColor);
        lv_obj_invalidate(_panel->raw_ptr());
    }

    void tick()
    {
        _color.update();
        lv_obj_invalidate(_panel->raw_ptr());
    }

private:
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Container> _panel;
    smooth_ui_toolkit::color::AnimateRgb_t _color;
    IrSignal _signal;

    lv_color_t currentColor() const
    {
        return lv_color_hex(_color.toHex());
    }

    void draw(lv_event_t* event)
    {
        lv_layer_t* layer = lv_event_get_layer(event);
        if (!layer || _signal.pulses.empty()) {
            return;
        }

        const uint64_t total_us = std::max<uint64_t>(totalDurationUs(_signal), 1);

        lv_area_t coords;
        lv_obj_get_coords(_panel->raw_ptr(), &coords);
        const int32_t mid_y  = coords.y1 + kWaveformHeight / 2;
        const int32_t high_y = mid_y + kWaveformHighOffset;
        const int32_t low_y  = mid_y + kWaveformLowOffset;

        lv_draw_line_dsc_t line_dsc;
        lv_draw_line_dsc_init(&line_dsc);
        line_dsc.color = currentColor();
        line_dsc.width = kWaveformLineWidth;
        line_dsc.opa   = LV_OPA_COVER;

        uint64_t cursor_us = 0;
        int32_t previous_y = _signal.pulses.front().pulse ? high_y : low_y;
        int32_t previous_x = coords.x1;

        for (const auto& pulse : _signal.pulses) {
            const int32_t y  = pulse.pulse ? high_y : low_y;
            const int32_t x1 = coords.x1 + static_cast<int32_t>((cursor_us * kWaveformWidth) / total_us);
            cursor_us += pulse.durationUs;
            const int32_t x2 =
                coords.x1 + static_cast<int32_t>(std::max<uint64_t>((cursor_us * kWaveformWidth) / total_us, 1));

            if (x1 != previous_x || y != previous_y) {
                line_dsc.p1 = {previous_x, previous_y};
                line_dsc.p2 = {x1, y};
                lv_draw_line(layer, &line_dsc);
            }

            line_dsc.p1 = {x1, y};
            line_dsc.p2 = {std::max(x1 + 1, x2), y};
            lv_draw_line(layer, &line_dsc);
            previous_x = std::max(x1 + 1, x2);
            previous_y = y;
        }
    }

    static void onDraw(lv_event_t* event)
    {
        auto* self = static_cast<WaveformView*>(lv_event_get_user_data(event));
        if (self) {
            self->draw(event);
        }
    }
};

class DetailView::SendToast {
public:
    explicit SendToast(lv_obj_t* parent)
        : _panel(std::make_unique<smooth_ui_toolkit::lvgl_cpp::Container>(parent)),
          _label(std::make_unique<smooth_ui_toolkit::lvgl_cpp::Label>(_panel->raw_ptr())),
          _width(kSendToastHiddenWidth),
          _height(kSendToastHiddenHeight),
          _y(kSendToastHiddenY)
    {
        setupAnimation(_width, 0.35f, 0.5f);
        setupAnimation(_height, 0.35f, 0.5f);
        setupAnimation(_y, 0.3f, 0.5f);

        _panel->setSize(kSendToastHiddenWidth, kSendToastHiddenHeight);
        _panel->align(LV_ALIGN_CENTER, kSendToastX, kSendToastHiddenY);
        _panel->setBgColor(lv_color_hex(kSendToastBgColor));
        _panel->setBgOpa(LV_OPA_COVER);
        _panel->setRadius(6);
        _panel->setBorderWidth(0);
        _panel->setShadowWidth(0);
        _panel->setPaddingAll(0);
        _panel->setScrollbarMode(LV_SCROLLBAR_MODE_OFF);
        _panel->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
        _panel->addFlag(LV_OBJ_FLAG_HIDDEN);

        _label->setText("Signal sent");
        _label->setTextFont(&font_chivo_medium_14);
        _label->setTextColor(lv_color_hex(kSendToastTextColor));
        _label->setTextAlign(LV_TEXT_ALIGN_CENTER);
        _label->setSize(kSendToastWidth, LV_SIZE_CONTENT);
        _label->center();
        _label->addFlag(LV_OBJ_FLAG_HIDDEN);
    }

    void show(uint32_t nowMs)
    {
        _visible        = true;
        _shown_until_ms = nowMs + kSendToastHoldMs;
        _width.teleport(kSendToastHiddenWidth);
        _height.teleport(kSendToastHiddenHeight);
        _y.teleport(kSendToastHiddenY);
        applyAnimatedValue();
        _panel->removeFlag(LV_OBJ_FLAG_HIDDEN);
        _label->removeFlag(LV_OBJ_FLAG_HIDDEN);
        _width.move(kSendToastWidth);
        _height.move(kSendToastHeight);
        _y.move(kSendToastY);
    }

    void tick(uint32_t nowMs)
    {
        if (_visible && _shown_until_ms != 0 && nowMs >= _shown_until_ms) {
            hide();
        }

        _width.update();
        _height.update();
        _y.update();
        applyAnimatedValue();

        if (!_visible && _width.done() && _height.done() && _y.done()) {
            _panel->addFlag(LV_OBJ_FLAG_HIDDEN);
        }
    }

private:
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Container> _panel;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _label;
    smooth_ui_toolkit::AnimateValue _width;
    smooth_ui_toolkit::AnimateValue _height;
    smooth_ui_toolkit::AnimateValue _y;
    uint32_t _shown_until_ms = 0;
    bool _visible            = false;

    static void setupAnimation(smooth_ui_toolkit::AnimateValue& value, float duration, float bounce)
    {
        value.springOptions().visualDuration = duration;
        value.springOptions().bounce         = bounce;
    }

    void hide()
    {
        _visible        = false;
        _shown_until_ms = 0;
        _label->addFlag(LV_OBJ_FLAG_HIDDEN);
        _width.move(kSendToastHiddenWidth);
        _height.move(kSendToastHiddenHeight);
        _y.move(kSendToastHiddenY);
    }

    void applyAnimatedValue()
    {
        _panel->setWidth(static_cast<int32_t>(std::round(_width.directValue())));
        _panel->setHeight(static_cast<int32_t>(std::round(_height.directValue())));
        _panel->align(LV_ALIGN_CENTER, kSendToastX, static_cast<int32_t>(std::round(_y.directValue())));
    }
};

DetailView::DetailView(DetailViewModel& vm) : _vm(vm)
{
}

DetailView::~DetailView()
{
    destroy();
}

void DetailView::onEnter(lv_obj_t* parent)
{
    destroy();

    _root = std::make_unique<smooth_ui_toolkit::lvgl_cpp::Container>(parent);
    _root->setSize(lv_pct(100), lv_pct(100));
    _root->setBgColor(lv_color_hex(0x000000));
    _root->setBgOpa(LV_OPA_COVER);
    _root->setBorderWidth(0);
    _root->setPaddingAll(0);
    _root->setScrollbarMode(LV_SCROLLBAR_MODE_OFF);
    _root->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

    _title_label = std::make_unique<smooth_ui_toolkit::lvgl_cpp::Label>(_root->raw_ptr());
    _title_label->setTextFont(&font_chivo_medium_14);
    _title_label->setTextColor(lv_color_hex(0x777777));
    _title_label->setTextAlign(LV_TEXT_ALIGN_CENTER);
    _title_label->setLongMode(LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    _title_label->setSize(kTitleWidth, LV_SIZE_CONTENT);
    _title_label->align(LV_ALIGN_TOP_MID, 0, kTitleY);

    _waveform = std::make_unique<WaveformView>(_root->raw_ptr());

    auto make_info_label = [this](int32_t x, int32_t y, int32_t width, lv_text_align_t align) {
        auto label = std::make_unique<smooth_ui_toolkit::lvgl_cpp::Label>(_root->raw_ptr());
        label->setTextFont(&font_chivo_mono_medium_12);
        label->setTextColor(lv_color_hex(kInfoTextColor));
        label->setTextAlign(align);
        label->setLongMode(LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
        label->setSize(width, LV_SIZE_CONTENT);
        label->setPos(x, y);
        return label;
    };

    _protocol_label = make_info_label(kInfoLeftX, kInfoRow1Y, kInfoHalfWidth, LV_TEXT_ALIGN_LEFT);
    _carrier_label  = make_info_label(kInfoRightX, kInfoRow1Y, kInfoHalfWidth, LV_TEXT_ALIGN_LEFT);
    _duration_label = make_info_label(kInfoLeftX, kInfoRow2Y, kInfoHalfWidth, LV_TEXT_ALIGN_LEFT);
    _pulse_label    = make_info_label(kInfoRightX, kInfoRow2Y, kInfoHalfWidth, LV_TEXT_ALIGN_LEFT);
    _send_toast     = std::make_unique<SendToast>(_root->raw_ptr());

    _fade_mask = std::make_unique<smooth_ui_toolkit::lvgl_cpp::Container>(_root->raw_ptr());
    _fade_mask->setSize(lv_pct(100), lv_pct(100));
    _fade_mask->setBgColor(lv_color_hex(0x000000));
    _fade_mask->setBgOpa(LV_OPA_COVER);
    _fade_mask->setBorderWidth(0);
    _fade_mask->setPaddingAll(0);
    _fade_mask->setScrollbarMode(LV_SCROLLBAR_MODE_OFF);
    _fade_mask->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

    _fade_mask_opacity.easingOptions().duration       = kRootFadeDuration;
    _fade_mask_opacity.easingOptions().easingFunction = smooth_ui_toolkit::ease::ease_out_quad;
    _fade_mask_opacity.teleport(255);
    _fade_mask_opacity.move(0);

    _key_bar = std::make_unique<BottomKeyBar>(_root->raw_ptr());
    _key_bar->setItems({
        {'4', &image_icon_nav_back},
        {'7', &image_icon_play},
    });

    _play_serial_seen = _vm.playSerial().get();
    _vm.file().observe(this, onFileChanged);
    _vm.signal().observe(this, onSignalChanged);
    _vm.decodeResult().observe(this, onDecodeResultChanged);
    _vm.playSerial().observe(this, onPlaySerialChanged);
}

void DetailView::onExit()
{
    destroy();
}

void DetailView::tick(uint32_t nowMs)
{
    (void)nowMs;

    if (_fade_mask) {
        _fade_mask_opacity.update();
        _fade_mask->setBgOpa(fadeMaskOpacityFromFloat(_fade_mask_opacity.directValue()));
        if (_fade_mask_opacity.done() && _fade_mask_opacity.directValue() <= 0.0f) {
            _fade_mask->addFlag(LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (_waveform) {
        _waveform->tick();
    }
    if (_key_bar) {
        _key_bar->tick();
    }
    if (_send_toast) {
        _send_toast->tick(nowMs);
    }
}

void DetailView::destroy()
{
    _vm.playSerial().removeObserver();
    _vm.decodeResult().removeObserver();
    _vm.signal().removeObserver();
    _vm.file().removeObserver();

    _key_bar.reset();
    _send_toast.reset();
    _pulse_label.reset();
    _duration_label.reset();
    _carrier_label.reset();
    _protocol_label.reset();
    _waveform.reset();
    _title_label.reset();
    _fade_mask.reset();
    _root.reset();
}

void DetailView::renderFile(const IrRecordingFile& file)
{
    if (_title_label) {
        _title_label->setText(file.name.empty() ? "No signal" : file.name);
    }
    if (_protocol_label) {
        _protocol_label->setText("Protocol Unknown");
    }
    if (_carrier_label) {
        _carrier_label->setText("Carrier " + carrierText(file.carrierHz));
    }
    if (_duration_label) {
        _duration_label->setText("Duration " + durationText(file.totalDurationUs));
    }
    if (_pulse_label) {
        _pulse_label->setText("Pulses " + std::to_string(file.pulseCount));
    }
}

void DetailView::renderDecodeResult(const IrProtocolDecodeResult& result)
{
    if (_protocol_label) {
        _protocol_label->setText("Protocol " + result.protocol);
    }
    const auto& file = _vm.file().get();
    if (_duration_label) {
        _duration_label->setText(result.decoded ? "Address " + hexText(result.address)
                                                : "Duration " + durationText(file.totalDurationUs));
    }
    if (_pulse_label) {
        _pulse_label->setText(result.decoded ? "Command " + hexText(result.command)
                                             : "Pulses " + std::to_string(file.pulseCount));
    }
}

void DetailView::renderSignal(const IrSignal& signal)
{
    if (_waveform) {
        _waveform->load(signal);
    }
}

void DetailView::renderPlay(uint32_t serial)
{
    if (serial == _play_serial_seen) {
        return;
    }
    _play_serial_seen = serial;
    if (_waveform) {
        _waveform->flashPlay();
    }
    if (_send_toast) {
        _send_toast->show(lv_tick_get());
    }
}

void DetailView::onFileChanged(void* context, const IrRecordingFile& file)
{
    auto* self = static_cast<DetailView*>(context);
    if (self) {
        self->renderFile(file);
    }
}

void DetailView::onSignalChanged(void* context, const IrSignal& signal)
{
    auto* self = static_cast<DetailView*>(context);
    if (self) {
        self->renderSignal(signal);
    }
}

void DetailView::onDecodeResultChanged(void* context, const IrProtocolDecodeResult& result)
{
    auto* self = static_cast<DetailView*>(context);
    if (self) {
        self->renderDecodeResult(result);
    }
}

void DetailView::onPlaySerialChanged(void* context, const uint32_t& serial)
{
    auto* self = static_cast<DetailView*>(context);
    if (self) {
        self->renderPlay(serial);
    }
}

}  // namespace ir_remote
