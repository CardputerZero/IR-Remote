#include "core/ir_remote_app.hpp"
#include <lvgl.h>
#include <spdlog/spdlog.h>

namespace ir_remote {

namespace {

lv_obj_t* focusedTextInput()
{
    lv_indev_t* indev = lv_indev_get_next(nullptr);
    while (indev) {
        lv_group_t* group = lv_indev_get_group(indev);
        if (group) {
            lv_obj_t* focused = lv_group_get_focused(group);
            if (focused && lv_obj_check_type(focused, &lv_textarea_class)) {
                return focused;
            }
        }
        indev = lv_indev_get_next(indev);
    }
    return nullptr;
}

bool textInputFocused()
{
    return focusedTextInput() != nullptr;
}

bool handleFocusedTextInput(uint32_t lv_key, const char* utf8, bool pressed)
{
    lv_obj_t* input = focusedTextInput();
    if (!input) {
        return false;
    }

    if (!pressed) {
        return true;
    }

    switch (lv_key) {
        case LV_KEY_BACKSPACE:
            lv_textarea_delete_char(input);
            return true;
        case LV_KEY_DEL:
            lv_textarea_delete_char_forward(input);
            return true;
        case LV_KEY_LEFT:
            lv_textarea_cursor_left(input);
            return true;
        case LV_KEY_RIGHT:
            lv_textarea_cursor_right(input);
            return true;
        case LV_KEY_HOME:
            lv_textarea_set_cursor_pos(input, 0);
            return true;
        case LV_KEY_END:
            lv_textarea_set_cursor_pos(input, LV_TEXTAREA_CURSOR_LAST);
            return true;
        default:
            break;
    }

    if (utf8 && utf8[0] >= 0x20 && utf8[0] < 0x7f && utf8[1] == '\0') {
        lv_textarea_add_text(input, utf8);
        return true;
    }

    return true;
}

}  // namespace

IRRemoteApp::IRRemoteApp()
    : _remote_vm(_router, _model),
      _detail_vm(_router, _model),
      _remote_view(_remote_vm),
      _detail_view(_detail_vm),
      _view_models{&_remote_vm, &_detail_vm},
      _views{&_remote_view, &_detail_view}
{
}

IRRemoteApp::~IRRemoteApp()
{
    if (_route_observer_id != 0) {
        _router.currentPage().removeObserver(_route_observer_id);
    }
    if (_input_group) {
        lv_group_del(_input_group);
        _input_group = nullptr;
    }
}

void IRRemoteApp::start()
{
    spdlog::info("IRRemoteApp start");
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(lv_screen_active(), LV_OPA_COVER, LV_PART_MAIN);
    setupInputGroup();
    _route_observer_id = _router.currentPage().observe(this, onRouteChanged);
    setCurrentPage(_router.page());
    _help_view = std::make_unique<HelpView>(lv_screen_active());
}

void IRRemoteApp::onKey(uint32_t key)
{
    if (_help_view && _help_view->active()) {
        if (key == ir_remote_key::Help || key == '\x1b') {
            _help_view->hide();
        }
        return;
    }

    if (key == ir_remote_key::Help) {
        if (_help_view) {
            _help_view->show();
        }
        return;
    }

    if (key == '\x1b' && _router.page() == PageId::Remote && !_remote_vm.modalActive()) {
        spdlog::info("IRRemoteApp: quit requested");
        _quit_requested = true;
        return;
    }

    if (_current_vm) {
        _current_vm->onKey(key);
    }
}

void IRRemoteApp::onLvglKey(uint32_t lv_key, const char* utf8)
{
    onLvglKeyState(lv_key, utf8, true);
}

bool IRRemoteApp::onLvglKeyState(uint32_t lv_key, const char* utf8, bool pressed)
{
    if (lv_key == ir_remote_key::Help) {
        if (pressed) {
            onKey(lv_key);
        }
        return true;
    }

    if (lv_key == LV_KEY_ESC) {
        if (pressed) {
            onKey('\x1b');
        }
        return true;
    }

    if (lv_key == LV_KEY_ENTER) {
        if (pressed) {
            onKey('\r');
        }
        return true;
    }

#if !LV_USE_SDL
    if (handleFocusedTextInput(lv_key, utf8, pressed)) {
        return true;
    }
#else
    if (textInputFocused()) {
        return true;
    }
#endif

    if (!pressed) {
        return true;
    }

    switch (lv_key) {
        case LV_KEY_UP:
            onKey(ir_remote_key::Up);
            return true;
        case LV_KEY_DOWN:
            onKey(ir_remote_key::Down);
            return true;
        case LV_KEY_LEFT:
            onKey(ir_remote_key::Left);
            return true;
        case LV_KEY_RIGHT:
            onKey(ir_remote_key::Right);
            return true;
        default:
            break;
    }

    if (utf8 && utf8[0] == ' ') {
        onKey(' ');
        return true;
    }

    if (utf8 && utf8[0] >= '0' && utf8[0] <= '9') {
        onKey(static_cast<uint32_t>(utf8[0]));
    }
    return true;
}

void IRRemoteApp::tick(uint32_t nowMs)
{
    if (_current_vm) {
        _current_vm->tick(nowMs);
    }
    if (_current_view) {
        _current_view->tick(nowMs);
    }
}

ViewModel* IRRemoteApp::viewModelFor(PageId page)
{
    for (auto* vm : _view_models) {
        if (vm && vm->pageId() == page) {
            return vm;
        }
    }
    return nullptr;
}

View* IRRemoteApp::viewFor(PageId page)
{
    const auto index = static_cast<size_t>(page);
    if (index >= _views.size()) {
        return nullptr;
    }
    return _views[index];
}

void IRRemoteApp::setupInputGroup()
{
    if (_input_group) {
        return;
    }

    _input_group = lv_group_create();

    lv_indev_t* indev = lv_indev_get_next(nullptr);
    while (indev) {
        if (lv_indev_get_type(indev) == LV_INDEV_TYPE_KEYPAD) {
            lv_indev_set_group(indev, _input_group);
#if LV_USE_SDL
            lv_indev_add_event_cb(indev, onKeyboardEvent, LV_EVENT_KEY, this);
#endif
        }
        indev = lv_indev_get_next(indev);
    }
}

void IRRemoteApp::setCurrentPage(PageId page)
{
    ViewModel* next = viewModelFor(page);
    View* next_view = viewFor(page);
    if (!next || (next == _current_vm && next_view == _current_view)) {
        return;
    }

    if (_current_view) {
        _current_view->onExit();
    }
    if (_current_vm) {
        _current_vm->onExit();
    }
    _current_vm   = next;
    _current_view = next_view;
    spdlog::info("IR Remote route -> {}", pageIdName(page));
    _current_vm->onEnter();
    if (_current_view) {
        _current_view->onEnter(lv_screen_active());
    }
}

void IRRemoteApp::onRouteChanged(void* context, const PageId& page)
{
    auto* self = static_cast<IRRemoteApp*>(context);
    if (self) {
        self->setCurrentPage(page);
    }
}

void IRRemoteApp::onKeyboardEvent(lv_event_t* event)
{
    auto* self  = static_cast<IRRemoteApp*>(lv_event_get_user_data(event));
    auto* indev = static_cast<lv_indev_t*>(lv_event_get_target(event));
    if (!self || !indev || lv_indev_get_state(indev) != LV_INDEV_STATE_PRESSED) {
        return;
    }

    const uint32_t key = lv_indev_get_key(indev);
    char utf8[2]       = {0, 0};
    if (key >= 0x20 && key < 0x7f) {
        utf8[0] = static_cast<char>(key);
    }
    self->onLvglKey(key, utf8);
}

}  // namespace ir_remote
