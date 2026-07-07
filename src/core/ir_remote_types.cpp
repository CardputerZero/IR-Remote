#include "core/ir_remote_types.hpp"

namespace ir_remote {

const char* pageIdName(PageId page)
{
    switch (page) {
        case PageId::Remote:
            return "Remote";
        case PageId::Detail:
            return "Detail";
        default:
            return "Unknown";
    }
}

const char* irServiceStateName(IrServiceState state)
{
    switch (state) {
        case IrServiceState::Idle:
            return "Idle";
        case IrServiceState::Capturing:
            return "Capturing";
        case IrServiceState::Sending:
            return "Sending";
        default:
            return "Unknown";
    }
}

}  // namespace ir_remote
