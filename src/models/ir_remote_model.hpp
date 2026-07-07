#pragma once

#include "models/ir_recording_files_model.hpp"
#include "models/ir_service_model.hpp"
#include <string>

namespace ir_remote {

class IrRemoteModel {
public:
    IrRemoteModel();
    explicit IrRemoteModel(std::string recordings_dir);

    IrRecordingFilesModel& recordings()
    {
        return _recordings;
    }

    IrServiceModel& service()
    {
        return _service;
    }

    void refresh(bool preserveSelected = false);
    void selectPrevious();
    void selectNext();

private:
    IrRecordingFilesModel _recordings;
    IrServiceModel _service;
};

}  // namespace ir_remote
