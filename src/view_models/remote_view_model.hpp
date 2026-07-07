#pragma once

#include "models/ir_remote_model.hpp"
#include "view_models/view_model.hpp"
#include <tools/observable/single_observable.hpp>
#include <string>

namespace ir_remote {

class RemoteViewModel : public ViewModel {
public:
    enum class DeleteCloseAction {
        None,
        Cancel,
        Confirm,
    };

    enum class SaveCloseAction {
        None,
        Cancel,
        Confirm,
    };

    RemoteViewModel(IRRemoteRouter& router, IrRemoteModel& model);

    PageId pageId() const override
    {
        return PageId::Remote;
    }

    void onEnter() override;
    void onKey(uint32_t key) override;
    void tick(uint32_t nowMs) override;

    smooth_ui_toolkit::SingleObservable<std::vector<IrRecordingFile>>& files()
    {
        return _model.recordings().files();
    }

    smooth_ui_toolkit::SingleObservable<int>& selectedIndex()
    {
        return _model.recordings().selectedIndex();
    }

    smooth_ui_toolkit::SingleObservable<std::string>& status()
    {
        return _status;
    }

    smooth_ui_toolkit::SingleObservable<bool>& captureActive()
    {
        return _capture_active;
    }

    smooth_ui_toolkit::SingleObservable<PendingDeleteIrRecordingFile>& pendingDeleteRecording()
    {
        return _pending_delete_recording;
    }

    smooth_ui_toolkit::SingleObservable<PendingSaveIrSignal>& pendingSaveSignal()
    {
        return _pending_save_signal;
    }

    smooth_ui_toolkit::SingleObservable<std::string>& pendingSaveSignalName()
    {
        return _pending_save_signal_name;
    }

    smooth_ui_toolkit::SingleObservable<uint32_t>& magic()
    {
        return _magic;
    }

    DeleteCloseAction deleteCloseAction() const
    {
        return _delete_close_action;
    }

    SaveCloseAction saveCloseAction() const
    {
        return _save_close_action;
    }

    bool modalActive() const
    {
        return _capture_active.get() || _pending_delete_recording.get().active || _pending_save_signal.get().active;
    }

    void cancelDeleteRecording();
    void confirmDeleteRecording();
    void setPendingSaveSignalName(std::string name);
    void cancelSaveSignal();
    void confirmSaveSignal();
    void onCaptureWindowOpened();

private:
    IrRemoteModel& _model;
    smooth_ui_toolkit::SingleObservable<std::string> _status{"Ready"};
    smooth_ui_toolkit::SingleObservable<bool> _capture_active{false};
    smooth_ui_toolkit::SingleObservable<PendingDeleteIrRecordingFile> _pending_delete_recording{
        PendingDeleteIrRecordingFile{}};
    smooth_ui_toolkit::SingleObservable<PendingSaveIrSignal> _pending_save_signal{PendingSaveIrSignal{}};
    smooth_ui_toolkit::SingleObservable<std::string> _pending_save_signal_name{""};
    smooth_ui_toolkit::SingleObservable<uint32_t> _magic{0};
    DeleteCloseAction _delete_close_action = DeleteCloseAction::None;
    SaveCloseAction _save_close_action     = SaveCloseAction::None;
    uint32_t _capture_serial               = 0;
    uint32_t _magic_count                   = 0;
    uint32_t _now_ms                       = 0;
    bool _capture_start_pending            = false;

    bool canGenerateMagic() const;
    void generateMagic();
    void requestCapture();
    void startCapture(uint32_t nowMs);
    void requestDeleteSelected();
    void openSelectedDetail();
    std::string nextCaptureName() const;
};

}  // namespace ir_remote
