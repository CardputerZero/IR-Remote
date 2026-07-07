#include "views/remote_view.hpp"
#include "assets/assets.h"
#include "views/magic_view.hpp"
#include <core/easing/ease.hpp>
#include <lvgl/lvgl_cpp/label.hpp>
#include <lvgl/lvgl_cpp/text_area.hpp>
#include <widget/select_menu/smooth_selector.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace ir_remote {

namespace {

constexpr int32_t kMenuWidth              = 320;
constexpr int32_t kMenuHeight             = 123;
constexpr int32_t kMenuSelectedX          = 20;
constexpr int32_t kMenuSelectedY          = 9;
constexpr int32_t kMenuItemMinWidth       = 88;
constexpr int32_t kMenuItemMaxWidth       = 280;
constexpr int32_t kMenuItemHeight         = 24;
constexpr int32_t kMenuItemPitch          = 41;
constexpr int32_t kMenuTextPaddingLeft    = 11;
constexpr int32_t kMenuTextPaddingRight   = 11;
constexpr int32_t kMenuSelectorRadius     = 8;
constexpr int32_t kMenuCameraPaddingY     = 9;
constexpr uint32_t kSelectorColor         = 0x626262;
constexpr uint32_t kTextColor             = 0xFFFFFF;
constexpr uint32_t kEmptyTextColor        = 0x666666;
constexpr int32_t kScrollBarX             = 301;
constexpr int32_t kScrollBarY             = 12;
constexpr int32_t kScrollBarWidth         = 3;
constexpr int32_t kScrollBarHeight        = 100;
constexpr int32_t kScrollBarThumbHeight   = 14;
constexpr int32_t kScrollBarRadius        = 1;
constexpr uint32_t kScrollBarColor        = 0x2B2B2B;
constexpr uint32_t kScrollBarThumbColor   = 0x848484;
constexpr int32_t kDialogWidth            = 258;
constexpr int32_t kDialogHeight           = 100;
constexpr int32_t kDialogY                = -21;
constexpr int32_t kDialogRadius           = 14;
constexpr int32_t kNameAreaWidth          = 238;
constexpr int32_t kButtonHeight           = 23;
constexpr int32_t kButtonRadius           = 5;
constexpr int32_t kPromptCenterY          = -32;
constexpr int32_t kNameCenterY            = -5;
constexpr int32_t kButtonCenterY          = 28;
constexpr int32_t kDialogOpenOriginX      = 0;
constexpr int32_t kDialogOpenOriginY      = -150;
constexpr int32_t kDialogOpenOriginWidth  = 180;
constexpr int32_t kDialogOpenOriginHeight = 120;
constexpr int32_t kDialogCloseTargetX     = 112;
constexpr int32_t kDialogCloseTargetY     = 128;
constexpr int32_t kDialogCloseTargetSize  = 18;
constexpr int32_t kCaptureWindowWidth     = 300;
constexpr int32_t kCaptureWindowHeight    = 124;
constexpr int32_t kCaptureWindowY         = -16;
constexpr int32_t kCaptureWindowRadius    = 20;
constexpr int32_t kCaptureOpenOriginX     = -112;
constexpr int32_t kCaptureOpenOriginY     = 64;
constexpr int32_t kCaptureOpenOriginSize  = 18;
constexpr float kSelectorMoveDuration     = 0.32f;
constexpr float kSelectorMoveBounce       = 0.30f;
constexpr float kSelectorShapeDuration    = 0.30f;
constexpr float kSelectorShapeBounce      = 0.18f;
constexpr float kCameraDuration           = 0.44f;
constexpr uint32_t kMenuRenderIntervalMs  = 16;
constexpr float kRootFadeDuration         = 0.18f;

lv_opa_t fadeMaskOpacityFromFloat(float value)
{
    return static_cast<lv_opa_t>(std::clamp(static_cast<int>(std::round(value)), 0, 255));
}

std::string durationText(uint64_t durationUs)
{
    if (durationUs >= 1000000) {
        char buffer[24] = {};
        std::snprintf(buffer, sizeof(buffer), "%.1fs", static_cast<double>(durationUs) / 1000000.0);
        return buffer;
    }
    return std::to_string(static_cast<unsigned long long>((durationUs + 500) / 1000)) + "ms";
}

std::string fileDisplayName(const IrRecordingFile& file)
{
    return file.name + " (" + std::to_string(file.pulseCount) + "p, " + durationText(file.totalDurationUs) + ")";
}

int32_t textWidth(const std::string& text)
{
    lv_point_t size{};
    lv_text_get_size(&size, text.c_str(), &font_chivo_medium_14, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return size.x;
}

int32_t optionWidth(const std::string& text)
{
    return std::clamp(textWidth(text) + kMenuTextPaddingLeft + kMenuTextPaddingRight, kMenuItemMinWidth,
                      kMenuItemMaxWidth);
}

lv_group_t* keyboardGroup()
{
    lv_indev_t* indev = lv_indev_get_next(nullptr);
    while (indev) {
        if (lv_indev_get_type(indev) == LV_INDEV_TYPE_KEYPAD) {
            lv_group_t* group = lv_indev_get_group(indev);
            if (group) {
                return group;
            }
        }
        indev = lv_indev_get_next(indev);
    }
    return nullptr;
}

}  // namespace

class IrFilesMenu : public smooth_ui_toolkit::SmoothSelectorMenu {
public:
    explicit IrFilesMenu(lv_obj_t* parent)
        : _panel(std::make_unique<smooth_ui_toolkit::lvgl_cpp::Container>(parent)),
          _selector(std::make_unique<smooth_ui_toolkit::lvgl_cpp::Container>(_panel->raw_ptr())),
          _empty_title_label(std::make_unique<smooth_ui_toolkit::lvgl_cpp::Label>(_panel->raw_ptr())),
          _empty_hint_label(std::make_unique<smooth_ui_toolkit::lvgl_cpp::Label>(_panel->raw_ptr())),
          _scroll_bar(std::make_unique<smooth_ui_toolkit::lvgl_cpp::Container>(_panel->raw_ptr())),
          _scroll_thumb(std::make_unique<smooth_ui_toolkit::lvgl_cpp::Container>(_panel->raw_ptr()))
    {
        _panel->setSize(kMenuWidth, kMenuHeight);
        _panel->align(LV_ALIGN_TOP_MID, 0, 6);
        _panel->setBgOpa(LV_OPA_TRANSP);
        _panel->setBorderWidth(0);
        _panel->setShadowWidth(0);
        _panel->setPaddingAll(0);
        _panel->setScrollbarMode(LV_SCROLLBAR_MODE_OFF);
        _panel->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

        _selector->setBgColor(lv_color_hex(kSelectorColor));
        _selector->setBgOpa(LV_OPA_COVER);
        _selector->setRadius(kMenuSelectorRadius);
        _selector->setBorderWidth(0);
        _selector->setShadowWidth(0);
        _selector->setPaddingAll(0);
        _selector->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
        _selector->addFlag(LV_OBJ_FLAG_HIDDEN);

        _empty_title_label->setText("No saved IR signals");
        _empty_title_label->setTextFont(&font_chivo_medium_14);
        _empty_title_label->setTextColor(lv_color_hex(0xA0A0A0));
        _empty_title_label->setTextAlign(LV_TEXT_ALIGN_CENTER);
        _empty_title_label->setSize(kMenuWidth, LV_SIZE_CONTENT);
        _empty_title_label->align(LV_ALIGN_CENTER, 0, -18);
        _empty_title_label->addFlag(LV_OBJ_FLAG_HIDDEN);

        _empty_hint_label->setText("Press Add (4) to start recording");
        _empty_hint_label->setTextFont(&font_chivo_medium_14);
        _empty_hint_label->setTextColor(lv_color_hex(kEmptyTextColor));
        _empty_hint_label->setTextAlign(LV_TEXT_ALIGN_CENTER);
        _empty_hint_label->setSize(kMenuWidth, LV_SIZE_CONTENT);
        _empty_hint_label->align(LV_ALIGN_CENTER, 0, 6);
        _empty_hint_label->addFlag(LV_OBJ_FLAG_HIDDEN);

        setupScrollBar();
        setupAnimation();
        setConfig().moveInLoop        = true;
        setConfig().renderInterval    = kMenuRenderIntervalMs;
        setConfig().readInputInterval = 0;
        setCameraSize(kMenuWidth, kMenuHeight);
    }

    void setFiles(const std::vector<IrRecordingFile>& files, int selectedIndex)
    {
        _rows.clear();
        _data.option_list.clear();
        _data.selected_option_index = 0;

        if (files.empty()) {
            _selector->addFlag(LV_OBJ_FLAG_HIDDEN);
            _empty_title_label->removeFlag(LV_OBJ_FLAG_HIDDEN);
            _empty_hint_label->removeFlag(LV_OBJ_FLAG_HIDDEN);
            _scroll_bar->addFlag(LV_OBJ_FLAG_HIDDEN);
            _scroll_thumb->addFlag(LV_OBJ_FLAG_HIDDEN);
            return;
        }

        _selector->removeFlag(LV_OBJ_FLAG_HIDDEN);
        _empty_title_label->addFlag(LV_OBJ_FLAG_HIDDEN);
        _empty_hint_label->addFlag(LV_OBJ_FLAG_HIDDEN);

        _rows.reserve(files.size());
        for (size_t i = 0; i < files.size(); ++i) {
            std::string name    = fileDisplayName(files[i]);
            const int32_t width = optionWidth(name);
            addOption({{static_cast<float>(kMenuSelectedX),
                        static_cast<float>(kMenuSelectedY + static_cast<int32_t>(i) * kMenuItemPitch),
                        static_cast<float>(width), static_cast<float>(kMenuItemHeight)},
                       nullptr});
            _rows.push_back(std::make_unique<Row>(_panel->raw_ptr(), std::move(name), width));
        }

        jumpToInstant(clampIndex(selectedIndex));
        render();
    }

    void setSelectedIndex(int index)
    {
        if (_data.option_list.empty()) {
            return;
        }
        moveToWithCamera(clampIndex(index));
    }

    void update(uint32_t nowMs) override
    {
        smooth_ui_toolkit::SmoothSelectorMenu::update(nowMs);
        if (!_data.option_list.empty()) {
            render();
        }
    }

private:
    struct Row {
        Row(lv_obj_t* parent, std::string name, int32_t width)
            : container(std::make_unique<smooth_ui_toolkit::lvgl_cpp::Container>(parent)),
              label(std::make_unique<smooth_ui_toolkit::lvgl_cpp::Label>(container->raw_ptr())),
              itemWidth(width),
              text(std::move(name))
        {
            container->setSize(itemWidth, kMenuItemHeight);
            container->setBgOpa(LV_OPA_TRANSP);
            container->setBorderWidth(0);
            container->setShadowWidth(0);
            container->setPaddingAll(0);
            container->setScrollbarMode(LV_SCROLLBAR_MODE_OFF);
            container->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

            label->setText(text);
            label->setTextFont(&font_chivo_medium_14);
            label->setTextColor(lv_color_hex(kTextColor));
            label->setLongMode(LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
            label->setSize(itemWidth - kMenuTextPaddingLeft - kMenuTextPaddingRight, LV_SIZE_CONTENT);
            label->align(LV_ALIGN_LEFT_MID, kMenuTextPaddingLeft, 0);
        }

        std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Container> container;
        std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> label;
        int32_t itemWidth = 0;
        std::string text;
    };

    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Container> _panel;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Container> _selector;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _empty_title_label;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _empty_hint_label;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Container> _scroll_bar;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Container> _scroll_thumb;
    std::vector<std::unique_ptr<Row>> _rows;

    int clampIndex(int index) const
    {
        if (_data.option_list.empty()) {
            return 0;
        }
        return std::clamp(index, 0, static_cast<int>(_data.option_list.size()) - 1);
    }

    void jumpToInstant(int index)
    {
        if (_data.option_list.empty()) {
            return;
        }

        _data.selected_option_index = clampIndex(index);
        const auto& keyframe        = _data.option_list[_data.selected_option_index].keyframe;
        getSelectorPostion().teleport(keyframe.x, keyframe.y);
        getSelectorShape().teleport(keyframe.width, keyframe.height);
        getCamera().teleport(0, cameraYFor(keyframe));
    }

    void moveToWithCamera(int index)
    {
        _data.selected_option_index = clampIndex(index);
        _update_selector_keyframe();
        _update_camera_keyframe();
    }

    void _update_camera_keyframe() override
    {
        if (_data.option_list.empty()) {
            return;
        }

        const auto& keyframe = getSelectedKeyframe();
        getCamera().move(0, cameraYFor(keyframe));
    }

    int32_t cameraYFor(const smooth_ui_toolkit::Vector4& keyframe)
    {
        int32_t offset           = static_cast<int32_t>(std::round(getCameraOffset().y));
        const int32_t top        = static_cast<int32_t>(std::round(keyframe.y));
        const int32_t bottom     = top + static_cast<int32_t>(std::round(keyframe.height));
        const int32_t max_offset = maxCameraY();

        if (top - offset < kMenuCameraPaddingY) {
            offset = top - kMenuCameraPaddingY;
        } else if (bottom - offset > kMenuHeight - kMenuCameraPaddingY) {
            offset = bottom - kMenuHeight + kMenuCameraPaddingY;
        }

        return std::clamp(offset, 0, max_offset);
    }

    int32_t maxCameraY() const
    {
        if (_data.option_list.empty()) {
            return 0;
        }

        const auto& keyframe         = _data.option_list.back().keyframe;
        const int32_t content_bottom = static_cast<int32_t>(std::round(keyframe.y + keyframe.height));
        return std::max(0, content_bottom + kMenuCameraPaddingY - kMenuHeight);
    }

    void setupScrollBar()
    {
        _scroll_bar->setSize(kScrollBarWidth, kScrollBarHeight);
        _scroll_bar->setPos(kScrollBarX, kScrollBarY);
        _scroll_bar->setBgColor(lv_color_hex(kScrollBarColor));
        _scroll_bar->setBgOpa(LV_OPA_COVER);
        _scroll_bar->setRadius(kScrollBarRadius);
        _scroll_bar->setBorderWidth(0);
        _scroll_bar->setShadowWidth(0);
        _scroll_bar->setPaddingAll(0);
        _scroll_bar->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
        _scroll_bar->addFlag(LV_OBJ_FLAG_HIDDEN);

        _scroll_thumb->setSize(kScrollBarWidth, kScrollBarThumbHeight);
        _scroll_thumb->setPos(kScrollBarX, kScrollBarY);
        _scroll_thumb->setBgColor(lv_color_hex(kScrollBarThumbColor));
        _scroll_thumb->setBgOpa(LV_OPA_COVER);
        _scroll_thumb->setRadius(kScrollBarRadius);
        _scroll_thumb->setBorderWidth(0);
        _scroll_thumb->setShadowWidth(0);
        _scroll_thumb->setPaddingAll(0);
        _scroll_thumb->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
        _scroll_thumb->addFlag(LV_OBJ_FLAG_HIDDEN);
    }

    void setupAnimation()
    {
        auto& selector_position_options          = getSelectorPostion().x.springOptions();
        selector_position_options.visualDuration = kSelectorMoveDuration;
        selector_position_options.bounce         = kSelectorMoveBounce;
        getSelectorPostion().y.springOptions()   = selector_position_options;

        auto& selector_shape_options          = getSelectorShape().x.springOptions();
        selector_shape_options.visualDuration = kSelectorShapeDuration;
        selector_shape_options.bounce         = kSelectorShapeBounce;
        getSelectorShape().y.springOptions()  = selector_shape_options;

        auto& camera_options          = getCamera().y.springOptions();
        camera_options.visualDuration = kCameraDuration;
        camera_options.bounce         = 0.0f;
        getCamera().x.springOptions() = camera_options;
    }

    void render()
    {
        const auto selector = getSelectorCurrentFrame();
        _selector->setSize(static_cast<int32_t>(std::round(selector.width)),
                           static_cast<int32_t>(std::round(selector.height)));
        const int32_t camera_x_offset = -static_cast<int32_t>(std::round(getCameraOffset().x));
        const int32_t camera_y_offset = -static_cast<int32_t>(std::round(getCameraOffset().y));
        _selector->setPos(static_cast<int32_t>(std::round(selector.x)) + camera_x_offset,
                          static_cast<int32_t>(std::round(selector.y)) + camera_y_offset);

        for (size_t i = 0; i < _rows.size() && i < _data.option_list.size(); ++i) {
            const auto& keyframe = _data.option_list[i].keyframe;
            auto& row            = *_rows[i];
            row.container->setSize(row.itemWidth, kMenuItemHeight);
            row.container->setPos(static_cast<int32_t>(std::round(keyframe.x)) + camera_x_offset,
                                  static_cast<int32_t>(std::round(keyframe.y)) + camera_y_offset);
        }

        renderScrollBar();
    }

    void renderScrollBar()
    {
        const int32_t max_offset = maxCameraY();
        if (max_offset <= 0) {
            _scroll_bar->addFlag(LV_OBJ_FLAG_HIDDEN);
            _scroll_thumb->addFlag(LV_OBJ_FLAG_HIDDEN);
            return;
        }

        const float offset         = std::clamp(getCameraOffset().y, 0.0f, static_cast<float>(max_offset));
        const float progress       = offset / static_cast<float>(max_offset);
        const int32_t thumb_travel = kScrollBarHeight - kScrollBarThumbHeight;
        const int32_t thumb_y      = kScrollBarY + static_cast<int32_t>(std::round(progress * thumb_travel));

        _scroll_bar->removeFlag(LV_OBJ_FLAG_HIDDEN);
        _scroll_thumb->removeFlag(LV_OBJ_FLAG_HIDDEN);
        _scroll_thumb->setPos(kScrollBarX, thumb_y);
    }
};

class RemoteView::CaptureWindow {
public:
    CaptureWindow(lv_obj_t* parent, RemoteViewModel& view_model)
        : _panel(std::make_unique<smooth_ui_toolkit::lvgl_cpp::Container>(parent)),
          _label(std::make_unique<smooth_ui_toolkit::lvgl_cpp::Label>(_panel->raw_ptr())),
          _view_model(view_model),
          _x(kCaptureOpenOriginX),
          _y(kCaptureOpenOriginY),
          _width(kCaptureOpenOriginSize),
          _height(kCaptureOpenOriginSize)
    {
        setupAnimation(_x, 0.35f, 0.25f);
        setupAnimation(_y, 0.35f, 0.25f);
        setupAnimation(_width, 0.35f, 0.25f);
        setupAnimation(_height, 0.35f, 0.25f);

        applyAnimatedValue();
        _panel->setBgColor(lv_color_hex(0xFFFBDE));
        _panel->setBgOpa(LV_OPA_COVER);
        _panel->setRadius(kCaptureWindowRadius);
        _panel->setBorderWidth(0);
        _panel->setShadowWidth(0);
        _panel->setPaddingAll(0);
        _panel->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
        _panel->addFlag(LV_OBJ_FLAG_HIDDEN);

        _label->setText("Recording...");
        _label->setTextFont(&font_chivo_medium_14);
        _label->setTextColor(lv_color_hex(0xB6A531));
        _label->setTextAlign(LV_TEXT_ALIGN_CENTER);
        _label->center();
    }

    void tick()
    {
        _x.update();
        _y.update();
        _width.update();
        _height.update();
        applyAnimatedValue();

        if (!_visible && _x.done() && _y.done() && _width.done() && _height.done()) {
            _panel->addFlag(LV_OBJ_FLAG_HIDDEN);
        }
        if (_visible && !_open_complete_notified && _x.done() && _y.done() && _width.done() && _height.done()) {
            _open_complete_notified = true;
            _view_model.onCaptureWindowOpened();
        }
    }

    void setActive(bool active)
    {
        if (active == _visible) {
            return;
        }

        _visible                = active;
        _open_complete_notified = false;
        if (active) {
            _panel->removeFlag(LV_OBJ_FLAG_HIDDEN);
            _x.teleport(kCaptureOpenOriginX);
            _y.teleport(kCaptureOpenOriginY);
            _width.teleport(kCaptureOpenOriginSize);
            _height.teleport(kCaptureOpenOriginSize);
            applyAnimatedValue();
            _x.move(0);
            _y.move(kCaptureWindowY);
            _width.move(kCaptureWindowWidth);
            _height.move(kCaptureWindowHeight);
        } else {
            _x.move(kCaptureOpenOriginX);
            _y.move(kCaptureOpenOriginY);
            _width.move(kCaptureOpenOriginSize);
            _height.move(kCaptureOpenOriginSize);
        }
    }

private:
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Container> _panel;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _label;
    RemoteViewModel& _view_model;
    smooth_ui_toolkit::AnimateValue _x;
    smooth_ui_toolkit::AnimateValue _y;
    smooth_ui_toolkit::AnimateValue _width;
    smooth_ui_toolkit::AnimateValue _height;
    bool _visible                = false;
    bool _open_complete_notified = false;

    static void setupAnimation(smooth_ui_toolkit::AnimateValue& value, float duration, float bounce)
    {
        value.springOptions().visualDuration = duration;
        value.springOptions().bounce         = bounce;
    }

    void applyAnimatedValue()
    {
        _panel->setSize(static_cast<int32_t>(std::round(_width.directValue())),
                        static_cast<int32_t>(std::round(_height.directValue())));
        _panel->align(LV_ALIGN_CENTER, static_cast<int32_t>(std::round(_x.directValue())),
                      static_cast<int32_t>(std::round(_y.directValue())));
    }
};

class DialogBase {
public:
    explicit DialogBase(lv_obj_t* parent)
        : _panel(std::make_unique<smooth_ui_toolkit::lvgl_cpp::Container>(parent)),
          _prompt_label(std::make_unique<smooth_ui_toolkit::lvgl_cpp::Label>(_panel->raw_ptr())),
          _name_label(std::make_unique<smooth_ui_toolkit::lvgl_cpp::Label>(_panel->raw_ptr())),
          _cancel_button(std::make_unique<smooth_ui_toolkit::lvgl_cpp::Container>(_panel->raw_ptr())),
          _confirm_button(std::make_unique<smooth_ui_toolkit::lvgl_cpp::Container>(_panel->raw_ptr())),
          _cancel_label(std::make_unique<smooth_ui_toolkit::lvgl_cpp::Label>(_cancel_button->raw_ptr())),
          _confirm_label(std::make_unique<smooth_ui_toolkit::lvgl_cpp::Label>(_confirm_button->raw_ptr())),
          _x(kDialogOpenOriginX),
          _y(kDialogOpenOriginY),
          _width(kDialogOpenOriginWidth),
          _height(kDialogOpenOriginHeight)
    {
        configureOpenAnimation();
        applyAnimatedValue();
        _panel->setBgColor(lv_color_hex(0x474747));
        _panel->setBgOpa(LV_OPA_COVER);
        _panel->setRadius(kDialogRadius);
        _panel->setBorderWidth(0);
        _panel->setShadowWidth(0);
        _panel->setPaddingAll(0);
        _panel->setScrollbarMode(LV_SCROLLBAR_MODE_OFF);
        _panel->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
        _panel->addFlag(LV_OBJ_FLAG_HIDDEN);
    }

    void tick()
    {
        _x.update();
        _y.update();
        _width.update();
        _height.update();
        applyAnimatedValue();

        if (!_visible && _x.done() && _y.done() && _width.done() && _height.done()) {
            _panel->addFlag(LV_OBJ_FLAG_HIDDEN);
        }
    }

protected:
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Container> _panel;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _prompt_label;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _name_label;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Container> _cancel_button;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Container> _confirm_button;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _cancel_label;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _confirm_label;
    smooth_ui_toolkit::AnimateValue _x;
    smooth_ui_toolkit::AnimateValue _y;
    smooth_ui_toolkit::AnimateValue _width;
    smooth_ui_toolkit::AnimateValue _height;
    bool _visible = false;

    static void setupAnimation(smooth_ui_toolkit::AnimateValue& value, float duration, float bounce)
    {
        value.springOptions().visualDuration = duration;
        value.springOptions().bounce         = bounce;
    }

    void configureOpenAnimation()
    {
        setupAnimation(_x, 0.35f, 0.4f);
        setupAnimation(_y, 0.35f, 0.3f);
        setupAnimation(_width, 0.35f, 0.2f);
        setupAnimation(_height, 0.35f, 0.2f);
        _y.delay = 0.0;
    }

    void configureCloseAnimation()
    {
        setupAnimation(_x, 0.4f, 0.18f);
        setupAnimation(_y, 0.5f, 0.18f);
        setupAnimation(_width, 0.4f, 0.12f);
        setupAnimation(_height, 0.4f, 0.12f);
        _y.delay = 0.2;
    }

    void open()
    {
        showControls();
        _panel->removeFlag(LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(_panel->raw_ptr());
        _visible = true;
        configureOpenAnimation();
        _x.teleport(kDialogOpenOriginX);
        _y.teleport(kDialogOpenOriginY);
        _width.teleport(kDialogOpenOriginWidth);
        _height.teleport(kDialogOpenOriginHeight);
        applyAnimatedValue();
        _x.move(0);
        _y.move(kDialogY);
        _width.move(kDialogWidth);
        _height.move(kDialogHeight);
    }

    template <typename Action>
    void close(Action action, Action cancelAction)
    {
        _visible = false;
        if (action == cancelAction) {
            configureOpenAnimation();
            _x.move(kDialogOpenOriginX);
            _y.move(kDialogOpenOriginY);
            _width.move(kDialogOpenOriginWidth);
            _height.move(kDialogOpenOriginHeight);
            return;
        }

        hideControls();
        configureCloseAnimation();
        _x.move(kDialogCloseTargetX);
        _y.move(kDialogCloseTargetY);
        _width.move(kDialogCloseTargetSize);
        _height.move(kDialogCloseTargetSize);
    }

    void closeToOpenOrigin()
    {
        _visible = false;
        configureOpenAnimation();
        _x.move(kDialogOpenOriginX);
        _y.move(kDialogOpenOriginY);
        _width.move(kDialogOpenOriginWidth);
        _height.move(kDialogOpenOriginHeight);
    }

    void setupPrompt(const char* text)
    {
        _prompt_label->setText(text);
        _prompt_label->setTextFont(&font_chivo_medium_14);
        _prompt_label->setTextColor(lv_color_hex(0xBCBCBC));
        _prompt_label->setTextAlign(LV_TEXT_ALIGN_LEFT);
        _prompt_label->setSize(220, LV_SIZE_CONTENT);
        _prompt_label->align(LV_ALIGN_CENTER, 0, kPromptCenterY);
    }

    void setupNameArea()
    {
        _name_label->setText("");
        _name_label->setTextFont(&font_chivo_medium_14);
        _name_label->setTextColor(lv_color_hex(0xFFFFFF));
        _name_label->setLongMode(LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
        _name_label->setSize(kNameAreaWidth - 18, LV_SIZE_CONTENT);
        _name_label->align(LV_ALIGN_CENTER, 0, kNameCenterY);
    }

    void setupButton(smooth_ui_toolkit::lvgl_cpp::Container& button, smooth_ui_toolkit::lvgl_cpp::Label& label,
                     int32_t x, int32_t width, const char* text, lv_color_t bg_color, lv_color_t label_color,
                     lv_event_cb_t callback, void* user_data)
    {
        button.setSize(width, kButtonHeight);
        button.align(LV_ALIGN_CENTER, x, kButtonCenterY);
        button.setBgColor(bg_color);
        button.setBgOpa(LV_OPA_COVER);
        button.setRadius(kButtonRadius);
        button.setBorderWidth(0);
        button.setShadowWidth(0);
        button.setPaddingAll(0);
        button.removeFlag(LV_OBJ_FLAG_SCROLLABLE);
        button.addFlag(LV_OBJ_FLAG_CLICKABLE);
        button.addEventCb(callback, LV_EVENT_CLICKED, user_data);

        label.setText(text);
        label.setTextFont(&font_chivo_medium_14);
        label.setTextColor(label_color);
        label.setTextAlign(LV_TEXT_ALIGN_CENTER);
        label.center();
    }

    void setName(const std::string& name)
    {
        _name_label->setText(name);
    }

    void showControls()
    {
        _prompt_label->removeFlag(LV_OBJ_FLAG_HIDDEN);
        _name_label->removeFlag(LV_OBJ_FLAG_HIDDEN);
        _cancel_button->removeFlag(LV_OBJ_FLAG_HIDDEN);
        _confirm_button->removeFlag(LV_OBJ_FLAG_HIDDEN);
    }

    void hideControls()
    {
        _prompt_label->addFlag(LV_OBJ_FLAG_HIDDEN);
        _name_label->addFlag(LV_OBJ_FLAG_HIDDEN);
        _cancel_button->addFlag(LV_OBJ_FLAG_HIDDEN);
        _confirm_button->addFlag(LV_OBJ_FLAG_HIDDEN);
    }

private:
    void applyAnimatedValue()
    {
        _panel->setSize(static_cast<int32_t>(std::round(_width.directValue())),
                        static_cast<int32_t>(std::round(_height.directValue())));
        _panel->align(LV_ALIGN_CENTER, static_cast<int32_t>(std::round(_x.directValue())),
                      static_cast<int32_t>(std::round(_y.directValue())));
    }
};

class RemoteView::SaveConfirmDialog {
public:
    SaveConfirmDialog(lv_obj_t* parent, RemoteViewModel& view_model)
        : _view_model(view_model),
          _panel(std::make_unique<smooth_ui_toolkit::lvgl_cpp::Container>(parent)),
          _prompt_label(std::make_unique<smooth_ui_toolkit::lvgl_cpp::Label>(_panel->raw_ptr())),
          _input(std::make_unique<smooth_ui_toolkit::lvgl_cpp::TextArea>(_panel->raw_ptr())),
          _discard_button(std::make_unique<smooth_ui_toolkit::lvgl_cpp::Container>(_panel->raw_ptr())),
          _confirm_button(std::make_unique<smooth_ui_toolkit::lvgl_cpp::Container>(_panel->raw_ptr())),
          _discard_label(std::make_unique<smooth_ui_toolkit::lvgl_cpp::Label>(_discard_button->raw_ptr())),
          _confirm_label(std::make_unique<smooth_ui_toolkit::lvgl_cpp::Label>(_confirm_button->raw_ptr())),
          _x(kDialogOpenOriginX),
          _y(kDialogOpenOriginY),
          _width(kDialogOpenOriginWidth),
          _height(kDialogOpenOriginHeight)
    {
        configureOpenAnimation();
        applyAnimatedValue();
        _panel->setBgColor(lv_color_hex(0x474747));
        _panel->setBgOpa(LV_OPA_COVER);
        _panel->setRadius(kDialogRadius);
        _panel->setBorderWidth(0);
        _panel->setShadowWidth(0);
        _panel->setPaddingAll(0);
        _panel->setScrollbarMode(LV_SCROLLBAR_MODE_OFF);
        _panel->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
        _panel->addFlag(LV_OBJ_FLAG_HIDDEN);

        setupPrompt();
        setupInput();
        setupButton(*_discard_button, *_discard_label, -31, 110, "ESC: Discard", lv_color_hex(0xC33630),
                    lv_color_hex(0xFFECEC), onDiscardClicked);
        setupButton(*_confirm_button, *_confirm_label, 75, 87, "Enter: OK", lv_color_hex(0xFED40D),
                    lv_color_hex(0x5E4D00), onConfirmClicked);
    }

    ~SaveConfirmDialog()
    {
        removeInputFromGroup();
    }

    void setPending(const PendingSaveIrSignal& pending)
    {
        if (pending.active) {
            setName(pending.name);
            showControls();
            _panel->removeFlag(LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(_panel->raw_ptr());
            _visible = true;
            configureOpenAnimation();
            _x.teleport(kDialogOpenOriginX);
            _y.teleport(kDialogOpenOriginY);
            _width.teleport(kDialogOpenOriginWidth);
            _height.teleport(kDialogOpenOriginHeight);
            applyAnimatedValue();
            _x.move(0);
            _y.move(kDialogY);
            _width.move(kDialogWidth);
            _height.move(kDialogHeight);
            focusInput();
        } else {
            removeInputFromGroup();
            _visible = false;
            closeToOpenOrigin();
        }
    }

    void setName(const std::string& name)
    {
        const char* current = lv_textarea_get_text(_input->raw_ptr());
        if (current && name == current) {
            return;
        }

        _updating_text = true;
        _input->setText(name);
        _input->setCursorPos(static_cast<int32_t>(name.size()));
        _updating_text = false;
    }

    void tick()
    {
        _x.update();
        _y.update();
        _width.update();
        _height.update();
        applyAnimatedValue();

        if (!_visible && _x.done() && _y.done() && _width.done() && _height.done()) {
            _panel->addFlag(LV_OBJ_FLAG_HIDDEN);
        }
    }

private:
    RemoteViewModel& _view_model;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Container> _panel;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _prompt_label;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::TextArea> _input;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Container> _discard_button;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Container> _confirm_button;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _discard_label;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _confirm_label;
    smooth_ui_toolkit::AnimateValue _x;
    smooth_ui_toolkit::AnimateValue _y;
    smooth_ui_toolkit::AnimateValue _width;
    smooth_ui_toolkit::AnimateValue _height;
    bool _input_in_group = false;
    bool _updating_text  = false;
    bool _visible        = false;

    static void setupAnimation(smooth_ui_toolkit::AnimateValue& value, float duration, float bounce)
    {
        value.springOptions().visualDuration = duration;
        value.springOptions().bounce         = bounce;
    }

    void configureOpenAnimation()
    {
        setupAnimation(_x, 0.35f, 0.4f);
        setupAnimation(_y, 0.35f, 0.3f);
        setupAnimation(_width, 0.35f, 0.2f);
        setupAnimation(_height, 0.35f, 0.2f);
        _y.delay = 0.0;
    }

    void closeToOpenOrigin()
    {
        configureOpenAnimation();
        _x.move(kDialogOpenOriginX);
        _y.move(kDialogOpenOriginY);
        _width.move(kDialogOpenOriginWidth);
        _height.move(kDialogOpenOriginHeight);
    }

    void applyAnimatedValue()
    {
        _panel->setSize(static_cast<int32_t>(std::round(_width.directValue())),
                        static_cast<int32_t>(std::round(_height.directValue())));
        _panel->align(LV_ALIGN_CENTER, static_cast<int32_t>(std::round(_x.directValue())),
                      static_cast<int32_t>(std::round(_y.directValue())));
    }

    void setupPrompt()
    {
        _prompt_label->setText("Save signal as");
        _prompt_label->setTextFont(&font_chivo_medium_14);
        _prompt_label->setTextColor(lv_color_hex(0xA1A1A1));
        _prompt_label->setTextAlign(LV_TEXT_ALIGN_LEFT);
        _prompt_label->setSize(230, LV_SIZE_CONTENT);
        _prompt_label->align(LV_ALIGN_CENTER, 0, kPromptCenterY);
    }

    void setupInput()
    {
        _input->setSize(kNameAreaWidth, 28);
        _input->align(LV_ALIGN_CENTER, 0, kNameCenterY);
        _input->setBgColor(lv_color_hex(0x676767));
        _input->setBgOpa(LV_OPA_COVER);
        _input->setRadius(5);
        _input->setBorderWidth(0);
        _input->setShadowWidth(0);
        _input->setPadding(4, 4, 9, 9);
        _input->setScrollbarMode(LV_SCROLLBAR_MODE_OFF);
        _input->setTextFont(&font_chivo_medium_14);
        _input->setTextColor(lv_color_hex(0xFFFFFF));
        _input->setOneLine(true);
        _input->setMaxLength(64);
        _input->addEventCb(onInputValueChanged, LV_EVENT_VALUE_CHANGED, this);
        _input->setOutlineWidth(0, LV_STATE_FOCUSED | LV_STATE_FOCUS_KEY);
    }

    void setupButton(smooth_ui_toolkit::lvgl_cpp::Container& button, smooth_ui_toolkit::lvgl_cpp::Label& label,
                     int32_t x, int32_t width, const char* text, lv_color_t bg_color, lv_color_t label_color,
                     lv_event_cb_t callback)
    {
        button.setSize(width, kButtonHeight);
        button.align(LV_ALIGN_CENTER, x, kButtonCenterY);
        button.setBgColor(bg_color);
        button.setBgOpa(LV_OPA_COVER);
        button.setRadius(kButtonRadius);
        button.setBorderWidth(0);
        button.setShadowWidth(0);
        button.setPaddingAll(0);
        button.removeFlag(LV_OBJ_FLAG_SCROLLABLE);
        button.addFlag(LV_OBJ_FLAG_CLICKABLE);
        button.addEventCb(callback, LV_EVENT_CLICKED, this);

        label.setText(text);
        label.setTextFont(&font_chivo_medium_14);
        label.setTextColor(label_color);
        label.setTextAlign(LV_TEXT_ALIGN_CENTER);
        label.center();
    }

    void showControls()
    {
        _prompt_label->removeFlag(LV_OBJ_FLAG_HIDDEN);
        _input->removeFlag(LV_OBJ_FLAG_HIDDEN);
        _discard_button->removeFlag(LV_OBJ_FLAG_HIDDEN);
        _confirm_button->removeFlag(LV_OBJ_FLAG_HIDDEN);
    }

    void focusInput()
    {
        lv_group_t* group = keyboardGroup();
        if (!group) {
            return;
        }

        if (!_input_in_group) {
            lv_group_add_obj(group, _input->raw_ptr());
            _input_in_group = true;
        }
        lv_group_focus_obj(_input->raw_ptr());
    }

    void removeInputFromGroup()
    {
        if (!_input_in_group) {
            return;
        }

        lv_group_remove_obj(_input->raw_ptr());
        _input_in_group = false;
    }

    static void onInputValueChanged(lv_event_t* event)
    {
        auto* self = static_cast<SaveConfirmDialog*>(lv_event_get_user_data(event));
        if (!self || self->_updating_text) {
            return;
        }

        const char* text = lv_textarea_get_text(self->_input->raw_ptr());
        self->_view_model.setPendingSaveSignalName(text ? text : "");
    }

    static void onDiscardClicked(lv_event_t* event)
    {
        auto* self = static_cast<SaveConfirmDialog*>(lv_event_get_user_data(event));
        if (self) {
            self->_view_model.cancelSaveSignal();
        }
    }

    static void onConfirmClicked(lv_event_t* event)
    {
        auto* self = static_cast<SaveConfirmDialog*>(lv_event_get_user_data(event));
        if (self) {
            self->_view_model.confirmSaveSignal();
        }
    }
};

class RemoteView::DeleteConfirmDialog : public DialogBase {
public:
    DeleteConfirmDialog(lv_obj_t* parent, RemoteViewModel& view_model) : DialogBase(parent), _view_model(view_model)
    {
        setupPrompt("Delete signal?");
        setupNameArea();
        setupButton(*_cancel_button, *_cancel_label, -46, 102, "ESC: Cancel", lv_color_hex(0x6D6D6D),
                    lv_color_hex(0xF3F3F3), onCancelClicked, this);
        setupButton(*_confirm_button, *_confirm_label, 67, 106, "Enter: Delete", lv_color_hex(0xC33630),
                    lv_color_hex(0xFFECEC), onConfirmClicked, this);
    }

    void setPending(const PendingDeleteIrRecordingFile& pending)
    {
        if (pending.active) {
            setName(fileDisplayName(pending.file));
            open();
        } else {
            close(_view_model.deleteCloseAction(), RemoteViewModel::DeleteCloseAction::Cancel);
        }
    }

private:
    RemoteViewModel& _view_model;

    static void onCancelClicked(lv_event_t* event)
    {
        auto* self = static_cast<DeleteConfirmDialog*>(lv_event_get_user_data(event));
        if (self) {
            self->_view_model.cancelDeleteRecording();
        }
    }

    static void onConfirmClicked(lv_event_t* event)
    {
        auto* self = static_cast<DeleteConfirmDialog*>(lv_event_get_user_data(event));
        if (self) {
            self->_view_model.confirmDeleteRecording();
        }
    }
};

RemoteView::RemoteView(RemoteViewModel& vm) : _vm(vm)
{
}

RemoteView::~RemoteView()
{
    destroy();
}

void RemoteView::onEnter(lv_obj_t* parent)
{
    build(parent);
    _vm.files().observe(this, onFilesChanged);
    _vm.selectedIndex().observe(this, onSelectedIndexChanged);
    _vm.captureActive().observe(this, onCaptureActiveChanged);
    _vm.pendingSaveSignal().observe(this, onPendingSaveChanged);
    _vm.pendingSaveSignalName().observe(this, onPendingSaveNameChanged);
    _vm.pendingDeleteRecording().observe(this, onPendingDeleteChanged);
    _magic_serial_seen = _vm.magic().get();
    _vm.magic().observe(this, onMagicChanged);
}

void RemoteView::onExit()
{
    destroy();
}

void RemoteView::build(lv_obj_t* parent)
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

    _menu                  = std::make_unique<IrFilesMenu>(_root->raw_ptr());
    _capture_window        = std::make_unique<CaptureWindow>(_root->raw_ptr(), _vm);
    _save_confirm_dialog   = std::make_unique<SaveConfirmDialog>(_root->raw_ptr(), _vm);
    _delete_confirm_dialog = std::make_unique<DeleteConfirmDialog>(_root->raw_ptr(), _vm);

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
        {'4', &image_icon_add},
        {'5', &image_icon_nav_up},
        {'6', &image_icon_nav_down},
        {'7', &image_icon_play},
        {'8', &image_icon_delete},
    });

    _magic_view = std::make_unique<MagicView>(_root->raw_ptr());
}

void RemoteView::destroy()
{
    _vm.magic().removeObserver();
    _vm.pendingDeleteRecording().removeObserver();
    _vm.pendingSaveSignalName().removeObserver();
    _vm.pendingSaveSignal().removeObserver();
    _vm.captureActive().removeObserver();
    _vm.selectedIndex().removeObserver();
    _vm.files().removeObserver();

    _magic_view.reset();
    _key_bar.reset();
    _delete_confirm_dialog.reset();
    _save_confirm_dialog.reset();
    _capture_window.reset();
    _menu.reset();
    _fade_mask.reset();
    _root.reset();
}

void RemoteView::tick(uint32_t nowMs)
{
    if (_fade_mask) {
        _fade_mask_opacity.update();
        _fade_mask->setBgOpa(fadeMaskOpacityFromFloat(_fade_mask_opacity.directValue()));
        if (_fade_mask_opacity.done() && _fade_mask_opacity.directValue() <= 0.0f) {
            _fade_mask->addFlag(LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (_menu) {
        _menu->update(nowMs);
    }
    if (_key_bar) {
        _key_bar->tick();
    }
    if (_magic_view) {
        _magic_view->tick(nowMs);
    }
    if (_capture_window) {
        _capture_window->tick();
    }
    if (_save_confirm_dialog) {
        _save_confirm_dialog->tick();
    }
    if (_delete_confirm_dialog) {
        _delete_confirm_dialog->tick();
    }
}

void RemoteView::renderFiles(const std::vector<IrRecordingFile>& files)
{
    if (_menu) {
        _menu->setFiles(files, _vm.selectedIndex().get());
    }
}

void RemoteView::renderSelectedIndex(int index)
{
    if (_menu) {
        _menu->setSelectedIndex(index);
    }
}

void RemoteView::renderCaptureActive(const bool& active)
{
    if (_capture_window) {
        _capture_window->setActive(active);
    }
}

void RemoteView::renderPendingSave(const PendingSaveIrSignal& pending)
{
    if (_save_confirm_dialog) {
        _save_confirm_dialog->setPending(pending);
    }
}

void RemoteView::renderPendingSaveName(const std::string& name)
{
    if (_save_confirm_dialog) {
        _save_confirm_dialog->setName(name);
    }
}

void RemoteView::renderPendingDelete(const PendingDeleteIrRecordingFile& pending)
{
    if (_delete_confirm_dialog) {
        _delete_confirm_dialog->setPending(pending);
    }
}

void RemoteView::renderMagic(uint32_t magicSerial)
{
    if (magicSerial == 0 || magicSerial == _magic_serial_seen) {
        return;
    }

    _magic_serial_seen = magicSerial;
    if (_magic_view) {
        _magic_view->generate(magicSerial);
    }
}

void RemoteView::onFilesChanged(void* context, const std::vector<IrRecordingFile>& files)
{
    auto* self = static_cast<RemoteView*>(context);
    if (self) {
        self->renderFiles(files);
    }
}

void RemoteView::onSelectedIndexChanged(void* context, const int& index)
{
    auto* self = static_cast<RemoteView*>(context);
    if (self) {
        self->renderSelectedIndex(index);
    }
}

void RemoteView::onCaptureActiveChanged(void* context, const bool& active)
{
    auto* self = static_cast<RemoteView*>(context);
    if (self) {
        self->renderCaptureActive(active);
    }
}

void RemoteView::onPendingSaveChanged(void* context, const PendingSaveIrSignal& pending)
{
    auto* self = static_cast<RemoteView*>(context);
    if (self) {
        self->renderPendingSave(pending);
    }
}

void RemoteView::onPendingSaveNameChanged(void* context, const std::string& name)
{
    auto* self = static_cast<RemoteView*>(context);
    if (self) {
        self->renderPendingSaveName(name);
    }
}

void RemoteView::onPendingDeleteChanged(void* context, const PendingDeleteIrRecordingFile& pending)
{
    auto* self = static_cast<RemoteView*>(context);
    if (self) {
        self->renderPendingDelete(pending);
    }
}

void RemoteView::onMagicChanged(void* context, const uint32_t& magicSerial)
{
    auto* self = static_cast<RemoteView*>(context);
    if (self) {
        self->renderMagic(magicSerial);
    }
}

}  // namespace ir_remote
