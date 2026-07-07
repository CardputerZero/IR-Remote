#pragma once

#include "core/ir_remote_types.hpp"

namespace ir_remote {

IrProtocolDecodeResult decodeIrProtocol(const IrSignal& signal);

}  // namespace ir_remote
