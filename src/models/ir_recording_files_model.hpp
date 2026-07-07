#pragma once

#include "core/ir_remote_types.hpp"
#include <tools/observable/single_observable.hpp>
#include <string>

namespace ir_remote {

class IrRecordingFilesModel {
public:
    IrRecordingFilesModel();
    explicit IrRecordingFilesModel(std::string recordings_dir);

    smooth_ui_toolkit::SingleObservable<std::vector<IrRecordingFile>>& files()
    {
        return _files;
    }

    smooth_ui_toolkit::SingleObservable<int>& selectedIndex()
    {
        return _selected_index;
    }

    const std::string& recordingsDirectory() const
    {
        return _recordings_dir;
    }

    const IrRecordingFile* selectedFile() const;
    void refresh(bool preserveSelected = false);
    void selectPrevious();
    void selectNext();
    bool selectPath(const std::string& path);
    bool deleteFile(const std::string& path);
    bool loadSignal(const std::string& path, IrSignal& outSignal, IrRecordingFile* outInfo = nullptr) const;
    bool saveSignal(const IrSignal& signal, const std::string& name, IrRecordingFile& outFile);

private:
    std::string _recordings_dir;
    smooth_ui_toolkit::SingleObservable<std::vector<IrRecordingFile>> _files{std::vector<IrRecordingFile>{}};
    smooth_ui_toolkit::SingleObservable<int> _selected_index{-1};
};

}  // namespace ir_remote
