#pragma once

#include "models/ir_remote_model.hpp"
#include "view_models/view_model.hpp"
#include <tools/observable/single_observable.hpp>

namespace ir_remote {

class DetailViewModel : public ViewModel {
public:
    DetailViewModel(IRRemoteRouter& router, IrRemoteModel& model);

    PageId pageId() const override
    {
        return PageId::Detail;
    }

    void onEnter() override;
    void onKey(uint32_t key) override;

    smooth_ui_toolkit::SingleObservable<IrRecordingFile>& file()
    {
        return _file;
    }

    smooth_ui_toolkit::SingleObservable<IrSignal>& signal()
    {
        return _signal;
    }

    smooth_ui_toolkit::SingleObservable<IrProtocolDecodeResult>& decodeResult()
    {
        return _decode_result;
    }

    smooth_ui_toolkit::SingleObservable<std::string>& status()
    {
        return _status;
    }

    smooth_ui_toolkit::SingleObservable<uint32_t>& playSerial()
    {
        return _play_serial;
    }

    const IrRecordingFile* selectedFile() const;

private:
    IrRemoteModel& _model;
    smooth_ui_toolkit::SingleObservable<IrRecordingFile> _file{IrRecordingFile{}};
    smooth_ui_toolkit::SingleObservable<IrSignal> _signal{IrSignal{}};
    smooth_ui_toolkit::SingleObservable<IrProtocolDecodeResult> _decode_result{IrProtocolDecodeResult{}};
    smooth_ui_toolkit::SingleObservable<std::string> _status{""};
    smooth_ui_toolkit::SingleObservable<uint32_t> _play_serial{0};

    void loadSelected();
    void play();
};

}  // namespace ir_remote
