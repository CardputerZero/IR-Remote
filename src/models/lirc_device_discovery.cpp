#include "models/lirc_device_discovery.hpp"

#if IR_REMOTE_USE_LIRC_IR
#include <linux/lirc.h>
#endif

namespace ir_remote {

bool lircDeviceSupportsRole(uint32_t features, LircDeviceRole role)
{
#if IR_REMOTE_USE_LIRC_IR
    if (role == LircDeviceRole::Receiver) {
        return (features & LIRC_CAN_REC_MODE2) != 0;
    }
    return (features & LIRC_CAN_SEND_PULSE) != 0;
#else
    (void)features;
    (void)role;
    return false;
#endif
}

std::optional<LircDeviceCandidate> selectLircDevice(const std::vector<LircDeviceCandidate>& candidates,
                                                    LircDeviceRole role)
{
    for (const auto& candidate : candidates) {
        if (lircDeviceSupportsRole(candidate.features, role)) {
            return candidate;
        }
    }
    return std::nullopt;
}

}  // namespace ir_remote
