#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ir_remote {

namespace ir_remote_key {

constexpr uint32_t Up    = 0x10001;
constexpr uint32_t Down  = 0x10002;
constexpr uint32_t Left  = 0x10003;
constexpr uint32_t Right = 0x10004;
constexpr uint32_t Help  = 0x10005;

}  // namespace ir_remote_key

enum class PageId {
    Remote = 0,
    Detail,
};

enum class IrServiceState {
    Idle,
    Capturing,
    Sending,
};

struct IrPulse {
    bool pulse          = true;
    uint32_t durationUs = 0;
};

struct IrSignal {
    std::vector<IrPulse> pulses;
    uint32_t carrierHz        = 38000;
    uint32_t dutyCyclePercent = 33;
    std::string sourceDriver;
    std::string sourceDevice;

    bool empty() const
    {
        return pulses.empty();
    }
};

struct IrProtocolDecodeResult {
    bool decoded = false;
    std::string protocol = "Unknown";
    uint32_t address = 0;
    uint32_t command = 0;
    uint32_t bits = 0;
    uint32_t confidence = 0;
};

struct IrRecordingFile {
    std::string path;
    std::string name;
    uint32_t carrierHz        = 38000;
    uint32_t dutyCyclePercent = 33;
    uint32_t pulseCount       = 0;
    uint64_t totalDurationUs  = 0;
    uint64_t createdUnixSec   = 0;
    std::string sourceDriver;
    std::string sourceDevice;
};

struct PendingDeleteIrRecordingFile {
    bool active = false;
    IrRecordingFile file;
};

struct PendingSaveIrSignal {
    bool active = false;
    std::string name;
    IrSignal signal;
};

const char* pageIdName(PageId page);
const char* irServiceStateName(IrServiceState state);

}  // namespace ir_remote
