#pragma once

#include "core/ir_remote_router.hpp"
#include "models/ir_remote_model.hpp"
#include "view_models/detail_view_model.hpp"
#include "view_models/remote_view_model.hpp"
#include "views/detail_view.hpp"
#include "views/help_view.hpp"
#include "views/remote_view.hpp"
#include "views/view.hpp"
#include <lvgl.h>
#include <array>

namespace ir_remote {

class IRRemoteApp {
public:
    IRRemoteApp();
    ~IRRemoteApp();

    IRRemoteApp(const IRRemoteApp&)            = delete;
    IRRemoteApp& operator=(const IRRemoteApp&) = delete;

    void start();
    void onKey(uint32_t key);
    void onLvglKey(uint32_t lv_key, const char* utf8);
    bool onLvglKeyState(uint32_t lv_key, const char* utf8, bool pressed);
    void tick(uint32_t nowMs);

    bool quitRequested() const
    {
        return _quit_requested;
    }

private:
    IRRemoteRouter _router;
    IrRemoteModel _model;
    RemoteViewModel _remote_vm;
    DetailViewModel _detail_vm;
    RemoteView _remote_view;
    DetailView _detail_view;
    ViewModel* _current_vm    = nullptr;
    View* _current_view       = nullptr;
    lv_group_t* _input_group  = nullptr;
    std::unique_ptr<HelpView> _help_view;
    size_t _route_observer_id = 0;
    bool _quit_requested      = false;

    std::array<ViewModel*, 2> _view_models;
    std::array<View*, 2> _views;

    ViewModel* viewModelFor(PageId page);
    View* viewFor(PageId page);
    void setupInputGroup();
    void setCurrentPage(PageId page);
    static void onRouteChanged(void* context, const PageId& page);
    static void onKeyboardEvent(lv_event_t* event);
};

}  // namespace ir_remote
