#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ir_remote {

enum class LircDeviceRole {
    Receiver,
    Transmitter,
};

struct LircDeviceCandidate {
    std::string rcPath;
    std::string devicePath;
    std::string driver;
    uint32_t features = 0;
};

bool lircDeviceSupportsRole(uint32_t features, LircDeviceRole role);
std::optional<LircDeviceCandidate> selectLircDevice(const std::vector<LircDeviceCandidate>& candidates,
                                                    LircDeviceRole role);

}  // namespace ir_remote
