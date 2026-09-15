#include "views/help_view.hpp"

#include "assets/assets.h"

namespace ir_remote {
namespace {

constexpr int32_t kPanelWidth  = 296;
constexpr int32_t kPanelHeight = 150;

void configureLabel(lv_obj_t* label, const lv_font_t* font, lv_color_t color)
{
    lv_obj_set_style_text_color(label, color, LV_PART_MAIN);
    if (font) {
        lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    }
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
}

}  // namespace

HelpView::HelpView(lv_obj_t* parent)
{
    if (!parent) {
        return;
    }

    _overlay = lv_obj_create(parent);
    if (!_overlay) {
        return;
    }

    lv_obj_set_size(_overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_pos(_overlay, 0, 0);
    lv_obj_set_style_bg_color(_overlay, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(_overlay, LV_OPA_80, LV_PART_MAIN);
    lv_obj_set_style_border_width(_overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(_overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(_overlay, 0, LV_PART_MAIN);
    lv_obj_remove_flag(_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(_overlay, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* panel = lv_obj_create(_overlay);
    lv_obj_set_size(panel, kPanelWidth, kPanelHeight);
    lv_obj_center(panel);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x202833), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(panel, lv_color_hex(0x557AFF), LV_PART_MAIN);
    lv_obj_set_style_border_width(panel, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(panel, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_all(panel, 0, LV_PART_MAIN);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* title = lv_label_create(panel);
    lv_obj_set_size(title, kPanelWidth - 20, 22);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 7);
    lv_label_set_text(title, "IR Remote");
    configureLabel(title, &font_chivo_medium_14, lv_color_hex(0xFED40D));

    lv_obj_t* body = lv_label_create(panel);
    lv_obj_set_size(body, kPanelWidth - 24, 88);
    lv_obj_align(body, LV_ALIGN_TOP_MID, 0, 33);
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_label_set_text(body,
                      "Record and replay infrared remote control signals to control home appliances and other devices. "
                      "Supports most infrared protocols.\n\nNumber keys 4-8: operations\nF / X / Z / C: navigate");
    configureLabel(body, &font_chivo_mono_medium_12, lv_color_hex(0xE0E0E0));
    lv_obj_set_style_text_align(body, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);

    lv_obj_t* footer = lv_label_create(panel);
    lv_obj_set_size(footer, kPanelWidth - 20, 18);
    lv_obj_align(footer, LV_ALIGN_BOTTOM_MID, 0, -7);
    lv_label_set_text(footer, "KEY_HELP / ESC: close");
    configureLabel(footer, &font_chivo_mono_medium_12, lv_color_hex(0x46DC87));

    lv_obj_add_flag(_overlay, LV_OBJ_FLAG_HIDDEN);
}

HelpView::~HelpView()
{
    if (_overlay) {
        lv_obj_delete(_overlay);
        _overlay = nullptr;
    }
}

void HelpView::show()
{
    if (!_overlay) {
        return;
    }
    _active = true;
    lv_obj_remove_flag(_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(_overlay);
}

void HelpView::hide()
{
    if (!_overlay) {
        return;
    }
    _active = false;
    lv_obj_add_flag(_overlay, LV_OBJ_FLAG_HIDDEN);
}

void HelpView::toggle()
{
    if (_active) {
        hide();
    } else {
        show();
    }
}

}  // namespace ir_remote
