#pragma once

#include "view_models/remote_view_model.hpp"
#include "views/bottom_key_bar.hpp"
#include "views/view.hpp"
#include <core/animation/animate_value/animate_value.hpp>
#include <lvgl.h>
#include <lvgl/lvgl_cpp/obj.hpp>
#include <memory>
#include <vector>

namespace ir_remote {

class IrFilesMenu;
class MagicView;

class RemoteView : public View {
public:
    explicit RemoteView(RemoteViewModel& vm);
    ~RemoteView() override;

    void onEnter(lv_obj_t* parent) override;
    void onExit() override;
    void tick(uint32_t nowMs) override;

private:
    class CaptureWindow;
    class SaveConfirmDialog;
    class DeleteConfirmDialog;

    RemoteViewModel& _vm;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Container> _root;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Container> _fade_mask;
    smooth_ui_toolkit::AnimateValue _fade_mask_opacity;
    std::unique_ptr<IrFilesMenu> _menu;
    std::unique_ptr<CaptureWindow> _capture_window;
    std::unique_ptr<SaveConfirmDialog> _save_confirm_dialog;
    std::unique_ptr<DeleteConfirmDialog> _delete_confirm_dialog;
    std::unique_ptr<BottomKeyBar> _key_bar;
    std::unique_ptr<MagicView> _magic_view;
    uint32_t _magic_serial_seen = 0;

    void build(lv_obj_t* parent);
    void destroy();
    void renderFiles(const std::vector<IrRecordingFile>& files);
    void renderSelectedIndex(int index);
    void renderCaptureActive(const bool& active);
    void renderPendingSave(const PendingSaveIrSignal& pending);
    void renderPendingSaveName(const std::string& name);
    void renderPendingDelete(const PendingDeleteIrRecordingFile& pending);
    void renderMagic(uint32_t magicSerial);
    static void onFilesChanged(void* context, const std::vector<IrRecordingFile>& files);
    static void onSelectedIndexChanged(void* context, const int& index);
    static void onCaptureActiveChanged(void* context, const bool& active);
    static void onPendingSaveChanged(void* context, const PendingSaveIrSignal& pending);
    static void onPendingSaveNameChanged(void* context, const std::string& name);
    static void onPendingDeleteChanged(void* context, const PendingDeleteIrRecordingFile& pending);
    static void onMagicChanged(void* context, const uint32_t& magicSerial);
};

}  // namespace ir_remote
