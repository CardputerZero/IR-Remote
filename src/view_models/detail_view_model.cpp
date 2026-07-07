#include "view_models/detail_view_model.hpp"
#include "models/ir_protocol_decoder.hpp"
#include <spdlog/spdlog.h>
#include <utility>

namespace ir_remote {

DetailViewModel::DetailViewModel(IRRemoteRouter& router, IrRemoteModel& model) : ViewModel(router), _model(model)
{
}

void DetailViewModel::onEnter()
{
    spdlog::info("DetailViewModel enter");
    loadSelected();
}

void DetailViewModel::onKey(uint32_t key)
{
    switch (key) {
        case '\x1b':
        case '4':
            _router.back();
            break;
        case '7':
        case '\r':
        case '\n':
            play();
            break;
        default:
            break;
    }
}

const IrRecordingFile* DetailViewModel::selectedFile() const
{
    return _model.recordings().selectedFile();
}

void DetailViewModel::loadSelected()
{
    const IrRecordingFile* selected = _model.recordings().selectedFile();
    if (!selected) {
        _file.set(IrRecordingFile{});
        _signal.set(IrSignal{});
        _decode_result.set(IrProtocolDecodeResult{});
        _status.set("No signal selected");
        return;
    }

    IrSignal signal;
    IrRecordingFile info;
    if (!_model.recordings().loadSignal(selected->path, signal, &info)) {
        _file.set(*selected);
        _signal.set(IrSignal{});
        _decode_result.set(IrProtocolDecodeResult{});
        _status.set("Failed to load signal");
        return;
    }

    _file.set(std::move(info));
    _decode_result.set(decodeIrProtocol(signal));
    _signal.set(std::move(signal));
    _status.set("Ready");
}

void DetailViewModel::play()
{
    IrSignal signal = _signal.get();
    if (signal.empty()) {
        loadSelected();
        signal = _signal.get();
    }

    if (signal.empty()) {
        _status.set("No signal to play");
        return;
    }

    if (_model.service().send(signal)) {
        _status.set("Sent " + _file.get().name);
        _play_serial.set(_play_serial.get() + 1);
    } else {
        _status.set(_model.service().lastError().get());
    }
}

}  // namespace ir_remote
