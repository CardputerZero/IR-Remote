#include "view_models/remote_view_model.hpp"
#include <lvgl.h>
#include <spdlog/spdlog.h>
#include <cstdio>

namespace ir_remote {

RemoteViewModel::RemoteViewModel(IRRemoteRouter& router, IrRemoteModel& model) : ViewModel(router), _model(model)
{
}

void RemoteViewModel::onEnter()
{
    spdlog::info("RemoteViewModel enter");
    _model.refresh(true);
    _status.set(_model.recordings().files().get().empty() ? "No signals" : "Ready");
    _magic_count = 0;
}

void RemoteViewModel::onKey(uint32_t key)
{
    if (_capture_active.get()) {
        return;
    }

    if (_pending_delete_recording.get().active) {
        switch (key) {
            case '\x1b':
                cancelDeleteRecording();
                break;
            case '\r':
            case '\n':
                confirmDeleteRecording();
                break;
            default:
                break;
        }
        return;
    }

    if (_pending_save_signal.get().active) {
        switch (key) {
            case '\x1b':
                cancelSaveSignal();
                break;
            case '\r':
            case '\n':
                confirmSaveSignal();
                break;
            default:
                break;
        }
        return;
    }

    switch (key) {
        case ' ':
            if (canGenerateMagic()) {
                ++_magic_count;
                if (_magic_count >= 3) {
                    _magic_count = 0;
                    generateMagic();
                }
            } else {
                _magic_count = 0;
            }
            break;
        case '4':
            requestCapture();
            break;
        case ir_remote_key::Up:
        case '5':
            _model.selectPrevious();
            _status.set("Up");
            break;
        case ir_remote_key::Down:
        case '6':
            _model.selectNext();
            _status.set("Down");
            break;
        case '\r':
        case '\n':
        case '7':
        case ir_remote_key::Right:
            openSelectedDetail();
            break;
        case '8':
            requestDeleteSelected();
            break;
        default:
            break;
    }
}

void RemoteViewModel::tick(uint32_t nowMs)
{
    _now_ms = nowMs;
    _model.service().tick(nowMs);

    if (!_capture_active.get() || _capture_start_pending) {
        return;
    }

    IrSignal signal;
    if (_model.service().consumeCapturedSignal(signal)) {
        _capture_active.set(false);
        _save_close_action     = SaveCloseAction::None;
        const std::string name = nextCaptureName();
        _pending_save_signal_name.set(name);
        _pending_save_signal.set(PendingSaveIrSignal{true, name, std::move(signal)});
        _status.set("Capture ready");
        return;
    }

    if (!_model.service().isCapturing()) {
        _capture_active.set(false);
        const std::string error = _model.service().lastError().get();
        _status.set(error.empty() ? "Capture failed" : error);
    }
}

void RemoteViewModel::cancelDeleteRecording()
{
    spdlog::info("RemoteViewModel: cancel delete");
    _delete_close_action = DeleteCloseAction::Cancel;
    _pending_delete_recording.set(PendingDeleteIrRecordingFile{});
}

void RemoteViewModel::confirmDeleteRecording()
{
    spdlog::info("RemoteViewModel: confirm delete");
    _delete_close_action = DeleteCloseAction::Confirm;
    const auto pending   = _pending_delete_recording.get();
    _pending_delete_recording.set(PendingDeleteIrRecordingFile{});
    if (pending.active && _model.recordings().deleteFile(pending.file.path)) {
        _model.refresh(false);
        _status.set("Deleted " + pending.file.name);
    } else {
        _status.set("Delete failed");
    }
}

void RemoteViewModel::cancelSaveSignal()
{
    spdlog::info("RemoteViewModel: cancel save");
    _save_close_action = SaveCloseAction::Cancel;
    _pending_save_signal.set(PendingSaveIrSignal{});
    _pending_save_signal_name.set("");
    _status.set("Discarded");
}

void RemoteViewModel::setPendingSaveSignalName(std::string name)
{
    _pending_save_signal_name.set(std::move(name));
}

void RemoteViewModel::confirmSaveSignal()
{
    spdlog::info("RemoteViewModel: confirm save");
    _save_close_action = SaveCloseAction::Confirm;
    const auto pending = _pending_save_signal.get();

    IrRecordingFile saved;
    if (pending.active && _model.recordings().saveSignal(pending.signal, _pending_save_signal_name.get(), saved)) {
        _pending_save_signal.set(PendingSaveIrSignal{});
        _pending_save_signal_name.set("");
        _status.set("Saved " + saved.name);
    } else {
        _save_close_action = SaveCloseAction::None;
        _status.set("Save failed");
    }
}

void RemoteViewModel::onCaptureWindowOpened()
{
    if (!_capture_start_pending || !_capture_active.get()) {
        return;
    }

    _capture_start_pending = false;
    startCapture(_now_ms);
}

bool RemoteViewModel::canGenerateMagic() const
{
    return true;
}

void RemoteViewModel::generateMagic()
{
    _magic.set(_magic.get() + 1);
    spdlog::info("RemoteViewModel: magic trigger serial={}", _magic.get());
}

void RemoteViewModel::requestCapture()
{
    ++_capture_serial;
    _capture_start_pending = true;
    _capture_active.set(true);
    _status.set("Preparing capture");
}

void RemoteViewModel::startCapture(uint32_t nowMs)
{
    if (nowMs == 0) {
        nowMs = lv_tick_get();
    }
    if (_model.service().startCapture(nowMs)) {
        _status.set("Capturing");
    } else {
        _capture_start_pending = false;
        _capture_active.set(false);
        _status.set(_model.service().lastError().get());
    }
}

void RemoteViewModel::requestDeleteSelected()
{
    const IrRecordingFile* selected = _model.recordings().selectedFile();
    if (!selected) {
        _status.set("No signal selected");
        return;
    }

    _delete_close_action = DeleteCloseAction::None;
    _pending_delete_recording.set(PendingDeleteIrRecordingFile{true, *selected});
}

void RemoteViewModel::openSelectedDetail()
{
    const IrRecordingFile* selected = _model.recordings().selectedFile();
    if (!selected) {
        _status.set("No signal selected");
        return;
    }
    _router.push(PageId::Detail);
}

std::string RemoteViewModel::nextCaptureName() const
{
    char buffer[32] = {};
    std::snprintf(buffer, sizeof(buffer), "IR Signal %u", static_cast<unsigned>(_capture_serial));
    return buffer;
}

}  // namespace ir_remote
