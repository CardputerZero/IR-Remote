#include "models/ir_remote_model.hpp"
#include <utility>

namespace ir_remote {

IrRemoteModel::IrRemoteModel() = default;

IrRemoteModel::IrRemoteModel(std::string recordings_dir) : _recordings(std::move(recordings_dir))
{
}

void IrRemoteModel::refresh(bool preserveSelected)
{
    _recordings.refresh(preserveSelected);
}

void IrRemoteModel::selectPrevious()
{
    _recordings.selectPrevious();
}

void IrRemoteModel::selectNext()
{
    _recordings.selectNext();
}

}  // namespace ir_remote
