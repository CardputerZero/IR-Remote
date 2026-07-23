#include "models/ir_service_model.hpp"
#include "models/lirc_device_discovery.hpp"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <random>
#include <string>
#include <utility>
#include <vector>

#if IR_REMOTE_USE_LIRC_IR
#include <dirent.h>
#include <fcntl.h>
#include <linux/lirc.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace ir_remote {
namespace {

constexpr uint32_t kDefaultCarrierHz    = 38000;
constexpr uint32_t kDefaultDutyCycle    = 33;
constexpr uint32_t kCaptureTimeoutMs    = 6000;
constexpr uint32_t kCaptureSilenceMs    = 220;
constexpr uint32_t kDummyCaptureDelayMs = 700;
constexpr const char* kRcClassPath      = "/sys/class/rc";
constexpr const char* kDevicePath       = "/dev";

std::string envValue(const char* name)
{
    const char* value = std::getenv(name);
    return value && value[0] != '\0' ? value : "";
}

#if IR_REMOTE_USE_LIRC_IR
std::string readTextFile(const std::string& path)
{
    FILE* file = std::fopen(path.c_str(), "r");
    if (!file) {
        return "";
    }

    char buffer[512]   = {};
    const size_t bytes = std::fread(buffer, 1, sizeof(buffer) - 1, file);
    std::fclose(file);
    buffer[bytes] = '\0';
    return buffer;
}

std::string trim(std::string value)
{
    while (!value.empty() && (value.back() == '\n' || value.back() == '\r' || value.back() == ' ')) {
        value.pop_back();
    }
    while (!value.empty() && value.front() == ' ') {
        value.erase(value.begin());
    }
    return value;
}

std::string sysfsKey(const std::string& rcPath, const std::string& key)
{
    const std::string uevent = readTextFile(rcPath + "/uevent");
    size_t pos               = 0;
    while (pos < uevent.size()) {
        const size_t end       = uevent.find('\n', pos);
        const std::string line = uevent.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
        pos                    = end == std::string::npos ? uevent.size() : end + 1;

        const std::string prefix = key + "=";
        if (line.rfind(prefix, 0) == 0) {
            return trim(line.substr(prefix.size()));
        }
    }
    return "";
}

std::string rcDriverName(const std::string& rcPath)
{
    std::string driver = sysfsKey(rcPath, "DRV_NAME");
    if (!driver.empty()) {
        return driver;
    }

    driver = trim(readTextFile(rcPath + "/drv_name"));
    if (!driver.empty()) {
        return driver;
    }

    return trim(readTextFile(rcPath + "/name"));
}

bool isNumberedName(const std::string& name, const char* prefix)
{
    const std::string prefixString = prefix;
    if (name.size() <= prefixString.size() || name.rfind(prefixString, 0) != 0) {
        return false;
    }
    return std::all_of(name.begin() + static_cast<std::ptrdiff_t>(prefixString.size()), name.end(),
                       [](char value) { return value >= '0' && value <= '9'; });
}

std::vector<std::string> numberedEntries(const std::string& directory, const char* prefix)
{
    std::vector<std::string> entries;
    DIR* handle = opendir(directory.c_str());
    if (!handle) {
        return entries;
    }

    while (dirent* entry = readdir(handle)) {
        const std::string name = entry->d_name;
        if (isNumberedName(name, prefix)) {
            entries.push_back(name);
        }
    }
    closedir(handle);
    std::sort(entries.begin(), entries.end());
    return entries;
}

std::vector<std::string> listRcPaths()
{
    std::vector<std::string> paths;
    for (const auto& name : numberedEntries(kRcClassPath, "rc")) {
        paths.push_back(std::string(kRcClassPath) + "/" + name);
    }
    return paths;
}

std::vector<std::string> listLircDevices(const std::string& rcPath)
{
    std::vector<std::string> devices;
    for (const auto& name : numberedEntries(rcPath, "lirc")) {
        devices.push_back(std::string(kDevicePath) + "/" + name);
    }
    return devices;
}

std::string formatFeatures(uint32_t features)
{
    char buffer[16] = {};
    std::snprintf(buffer, sizeof(buffer), "0x%08X", features);
    return buffer;
}

std::string errnoMessage(const std::string& action, int errorNumber)
{
    return action + ": " + std::strerror(errorNumber) + " (errno=" + std::to_string(errorNumber) + ")";
}

int openLircForProbe(const std::string& devicePath)
{
    constexpr std::array<int, 3> kOpenModes{
        O_RDWR | O_NONBLOCK | O_CLOEXEC,
        O_RDONLY | O_NONBLOCK | O_CLOEXEC,
        O_WRONLY | O_NONBLOCK | O_CLOEXEC,
    };
    for (const int flags : kOpenModes) {
        const int fd = ::open(devicePath.c_str(), flags);
        if (fd >= 0) {
            return fd;
        }
    }
    return -1;
}

bool readLircFeatures(int fd, uint32_t& features, std::string& error)
{
    features = 0;
    if (::ioctl(fd, LIRC_GET_FEATURES, &features) == 0) {
        return true;
    }
    const int errorNumber = errno;
    error                 = errnoMessage("LIRC_GET_FEATURES failed", errorNumber);
    return false;
}

bool probeLircFeatures(const std::string& devicePath, uint32_t& features, std::string& error)
{
    const int fd = openLircForProbe(devicePath);
    if (fd < 0) {
        const int errorNumber = errno;
        error                 = errnoMessage("Failed to open " + devicePath, errorNumber);
        return false;
    }

    const bool ok = readLircFeatures(fd, features, error);
    ::close(fd);
    return ok;
}

LircDeviceCandidate candidateMetadata(const std::string& devicePath, const std::vector<std::string>& rcPaths)
{
    for (const auto& rcPath : rcPaths) {
        const auto devices = listLircDevices(rcPath);
        if (std::find(devices.begin(), devices.end(), devicePath) != devices.end()) {
            return {rcPath, devicePath, rcDriverName(rcPath), 0};
        }
    }
    return {"", devicePath, "lirc", 0};
}

void appendCandidate(std::vector<LircDeviceCandidate>& candidates, LircDeviceCandidate candidate)
{
    const auto duplicate = std::find_if(candidates.begin(), candidates.end(), [&candidate](const auto& existing) {
        return existing.devicePath == candidate.devicePath;
    });
    if (duplicate == candidates.end()) {
        candidates.push_back(std::move(candidate));
    }
}

void probeRcPaths(const std::vector<std::string>& rcPaths, std::vector<LircDeviceCandidate>& candidates)
{
    for (const auto& rcPath : rcPaths) {
        for (const auto& devicePath : listLircDevices(rcPath)) {
            LircDeviceCandidate candidate{rcPath, devicePath, rcDriverName(rcPath), 0};
            std::string error;
            if (!probeLircFeatures(devicePath, candidate.features, error)) {
                spdlog::warn("IrServiceModel: LIRC probe skipped device={} rc={} driver={} error={}", devicePath,
                             rcPath, candidate.driver, error);
                continue;
            }
            spdlog::info("IrServiceModel: LIRC candidate device={} rc={} driver={} features={}", devicePath, rcPath,
                         candidate.driver, formatFeatures(candidate.features));
            appendCandidate(candidates, std::move(candidate));
        }
    }
}

void probeUnmappedDevices(const std::vector<std::string>& rcPaths, std::vector<LircDeviceCandidate>& candidates)
{
    for (const auto& name : numberedEntries(kDevicePath, "lirc")) {
        const std::string devicePath = std::string(kDevicePath) + "/" + name;
        if (std::any_of(candidates.begin(), candidates.end(),
                        [&devicePath](const auto& candidate) { return candidate.devicePath == devicePath; })) {
            continue;
        }

        auto candidate = candidateMetadata(devicePath, rcPaths);
        std::string error;
        if (!probeLircFeatures(devicePath, candidate.features, error)) {
            spdlog::warn("IrServiceModel: LIRC probe skipped device={} error={}", devicePath, error);
            continue;
        }
        spdlog::info("IrServiceModel: LIRC candidate device={} rc={} driver={} features={}", devicePath,
                     candidate.rcPath.empty() ? "unmapped" : candidate.rcPath, candidate.driver,
                     formatFeatures(candidate.features));
        appendCandidate(candidates, std::move(candidate));
    }
}

const char* roleName(LircDeviceRole role)
{
    return role == LircDeviceRole::Receiver ? "receiver" : "transmitter";
}

const char* requiredCapabilityName(LircDeviceRole role)
{
    return role == LircDeviceRole::Receiver ? "LIRC_CAN_REC_MODE2" : "LIRC_CAN_SEND_PULSE";
}

bool discoverLircDevice(LircDeviceRole role, const char* deviceEnv, const char* rcEnv, LircDeviceCandidate& selected,
                        std::string& error)
{
    const std::string explicitDevice = envValue(deviceEnv);
    const std::string explicitRc     = envValue(rcEnv);
    const auto allRcPaths            = listRcPaths();

    if (!explicitDevice.empty()) {
        const std::vector<std::string> metadataPaths =
            explicitRc.empty() ? allRcPaths : std::vector<std::string>{explicitRc};
        selected = candidateMetadata(explicitDevice, metadataPaths);
        if (!probeLircFeatures(explicitDevice, selected.features, error)) {
            return false;
        }
        if (!lircDeviceSupportsRole(selected.features, role)) {
            error = std::string(deviceEnv) + "=" + explicitDevice + " has features " +
                    formatFeatures(selected.features) + ", missing " + requiredCapabilityName(role);
            return false;
        }
        return true;
    }

    const std::vector<std::string> searchRcPaths =
        explicitRc.empty() ? allRcPaths : std::vector<std::string>{explicitRc};
    std::vector<LircDeviceCandidate> candidates;
    probeRcPaths(searchRcPaths, candidates);
    if (explicitRc.empty()) {
        probeUnmappedDevices(allRcPaths, candidates);
    }

    const auto match = selectLircDevice(candidates, role);
    if (match) {
        selected = *match;
        return true;
    }

    if (!explicitRc.empty() && candidates.empty()) {
        error = std::string("No LIRC nodes found under ") + explicitRc;
    } else if (candidates.empty()) {
        error = "No usable LIRC nodes found";
    } else {
        error = std::string("No LIRC ") + roleName(role) + " advertises " + requiredCapabilityName(role);
    }
    return false;
}

bool ioctlSet(int fd, unsigned long request, uint32_t value, const char* what, std::string& error)
{
    if (::ioctl(fd, request, &value) == 0) {
        return true;
    }

    const int errorNumber = errno;
    error                 = errnoMessage(std::string(what) + " failed", errorNumber);
    return false;
}

bool isLircPulse(lirc_t value)
{
    return LIRC_IS_PULSE(value);
}

bool isLircSpace(lirc_t value)
{
    return LIRC_IS_SPACE(value);
}
#endif

#if IR_REMOTE_USE_DUMMY_IR
IrSignal makeDummySignal(uint32_t seedValue)
{
    std::mt19937 rng(seedValue == 0 ? 0x49345231u : seedValue);
    std::uniform_int_distribution<int> bit(0, 1);
    std::uniform_int_distribution<int> jitter(-80, 80);

    IrSignal signal;
    signal.carrierHz        = kDefaultCarrierHz;
    signal.dutyCyclePercent = kDefaultDutyCycle;
    signal.sourceDriver     = "sdl-dummy";
    signal.sourceDevice     = "generated";

    auto add = [&signal, &jitter, &rng](bool pulse, uint32_t duration) {
        const int value = static_cast<int>(duration) + jitter(rng);
        signal.pulses.push_back({pulse, static_cast<uint32_t>(std::max(100, value))});
    };

    add(true, 9000);
    add(false, 4500);
    for (int i = 0; i < 32; ++i) {
        add(true, 560);
        add(false, bit(rng) ? 1690 : 560);
    }
    add(true, 560);
    return signal;
}
#endif

}  // namespace

IrServiceModel::IrServiceModel()
{
}

IrServiceModel::~IrServiceModel()
{
#if IR_REMOTE_USE_LIRC_IR
    closeDevices();
#endif
}

void IrServiceModel::tick(uint32_t nowMs)
{
#if IR_REMOTE_USE_LIRC_IR
    if (_state.get() == IrServiceState::Capturing) {
        pollLirc(nowMs);
        if (_state.get() == IrServiceState::Capturing && nowMs - _capture_start_ms >= kCaptureTimeoutMs) {
            finishCapture(false, "Capture timed out");
        } else if (_state.get() == IrServiceState::Capturing && !_capture_buffer.empty() &&
                   nowMs - _last_sample_ms >= kCaptureSilenceMs) {
            finishCapture(true);
        }
    }
#endif

#if IR_REMOTE_USE_DUMMY_IR
    finishDummyCapture(nowMs);
#endif
}

bool IrServiceModel::startCapture(uint32_t nowMs)
{
    if (_state.get() != IrServiceState::Idle) {
        setError("IR service is busy");
        return false;
    }

#if IR_REMOTE_USE_LIRC_IR
    if (!ensureRxOpen()) {
        return false;
    }
#endif

    _captured_signal  = IrSignal{};
    _capture_ready    = false;
    _capture_start_ms = nowMs;
    _last_sample_ms   = nowMs;
#if IR_REMOTE_USE_LIRC_IR
    _capture_buffer.clear();
#endif
    _last_error.set("");
    _state.set(IrServiceState::Capturing);
    spdlog::info("IrServiceModel: capture started");
    return true;
}

bool IrServiceModel::isCapturing() const
{
    return _state.get() == IrServiceState::Capturing;
}

bool IrServiceModel::consumeCapturedSignal(IrSignal& outSignal)
{
    if (!_capture_ready) {
        return false;
    }

    outSignal        = std::move(_captured_signal);
    _captured_signal = IrSignal{};
    _capture_ready   = false;
    return true;
}

bool IrServiceModel::send(const IrSignal& signal)
{
    if (signal.empty()) {
        setError("Cannot send an empty IR signal");
        return false;
    }

    if (_state.get() != IrServiceState::Idle) {
        setError("IR service is busy");
        return false;
    }

    _state.set(IrServiceState::Sending);

#if IR_REMOTE_USE_DUMMY_IR
    spdlog::info("IrServiceModel: dummy send pulses={} carrier={}Hz", signal.pulses.size(), signal.carrierHz);
    _last_error.set("");
    _state.set(IrServiceState::Idle);
    return true;
#endif

#if IR_REMOTE_USE_LIRC_IR
    if (!ensureTxOpen()) {
        _state.set(IrServiceState::Idle);
        return false;
    }

    std::string configurationError;
    if ((_tx_features & LIRC_CAN_SET_SEND_CARRIER) != 0 &&
        !ioctlSet(_tx_fd, LIRC_SET_SEND_CARRIER, signal.carrierHz, "LIRC_SET_SEND_CARRIER", configurationError)) {
        spdlog::warn("IrServiceModel: TX will use the current carrier: {}", configurationError);
    }
    if ((_tx_features & LIRC_CAN_SET_SEND_DUTY_CYCLE) != 0 &&
        !ioctlSet(_tx_fd, LIRC_SET_SEND_DUTY_CYCLE, signal.dutyCyclePercent, "LIRC_SET_SEND_DUTY_CYCLE",
                  configurationError)) {
        spdlog::warn("IrServiceModel: TX will use the current duty cycle: {}", configurationError);
    }

    std::vector<lirc_t> durations;
    durations.reserve(signal.pulses.size());
    bool have_value = false;
    bool last_pulse = true;
    std::string pulseDataError;
    constexpr uint64_t kMaxLircDuration = static_cast<uint64_t>(std::numeric_limits<lirc_t>::max());
    for (const auto& pulse : signal.pulses) {
        if (pulse.durationUs == 0) {
            continue;
        }
        if (!have_value && !pulse.pulse) {
            continue;
        }
        if (pulse.durationUs > kMaxLircDuration) {
            pulseDataError = "IR pulse duration exceeds the LIRC limit";
            break;
        }
        if (have_value && pulse.pulse == last_pulse) {
            const uint64_t mergedDuration = static_cast<uint64_t>(durations.back()) + pulse.durationUs;
            if (mergedDuration > kMaxLircDuration) {
                pulseDataError = "Merged IR pulse duration exceeds the LIRC limit";
                break;
            }
            durations.back() = static_cast<lirc_t>(mergedDuration);
        } else {
            durations.push_back(static_cast<lirc_t>(pulse.durationUs));
            have_value = true;
            last_pulse = pulse.pulse;
        }
    }

    if (!pulseDataError.empty()) {
        closeTx();
        _state.set(IrServiceState::Idle);
        setError(pulseDataError);
        return false;
    }
    if (durations.empty()) {
        closeTx();
        _state.set(IrServiceState::Idle);
        setError("IR signal has no pulse data");
        return false;
    }

    // LIRC pulse writes must begin and end with a pulse.
    if (durations.size() % 2 == 0) {
        durations.pop_back();
    }
    if (durations.empty()) {
        closeTx();
        _state.set(IrServiceState::Idle);
        setError("IR signal has no transmittable pulse data");
        return false;
    }

    const size_t bytes    = durations.size() * sizeof(lirc_t);
    const ssize_t written = ::write(_tx_fd, durations.data(), bytes);
    if (written != static_cast<ssize_t>(bytes)) {
        const int errorNumber = errno;
        const std::string error =
            written < 0 ? errnoMessage("LIRC write failed", errorNumber)
                        : "LIRC short write: " + std::to_string(written) + "/" + std::to_string(bytes) + " bytes";
        closeTx();
        _state.set(IrServiceState::Idle);
        setError(error);
        return false;
    }

    spdlog::info("IrServiceModel: sent pulses={} carrier={}Hz device={}", durations.size(), signal.carrierHz,
                 _tx_device);
    closeTx();
    _last_error.set("");
    _state.set(IrServiceState::Idle);
    return true;
#endif
}

#if IR_REMOTE_USE_LIRC_IR
bool IrServiceModel::ensureRxOpen()
{
    if (_rx_fd >= 0) {
        return true;
    }

    LircDeviceCandidate selected;
    std::string error;
    if (!discoverLircDevice(LircDeviceRole::Receiver, "IR_REMOTE_LIRC_RX_DEVICE", "IR_REMOTE_LIRC_RX_RC", selected,
                            error)) {
        setError(error);
        return false;
    }

    const int fd = ::open(selected.devicePath.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        const int errorNumber = errno;
        setError(errnoMessage("Failed to open IR receiver " + selected.devicePath, errorNumber));
        return false;
    }

    uint32_t features = 0;
    if (!readLircFeatures(fd, features, error)) {
        ::close(fd);
        setError(error);
        return false;
    }
    if (!lircDeviceSupportsRole(features, LircDeviceRole::Receiver)) {
        ::close(fd);
        setError("IR receiver " + selected.devicePath + " lost " + requiredCapabilityName(LircDeviceRole::Receiver) +
                 " capability");
        return false;
    }
    if (!ioctlSet(fd, LIRC_SET_REC_MODE, LIRC_MODE_MODE2, "LIRC_SET_REC_MODE", error)) {
        ::close(fd);
        setError(error);
        return false;
    }

    _rx_fd       = fd;
    _rx_device   = std::move(selected.devicePath);
    _rx_driver   = std::move(selected.driver);
    _rx_features = features;

    if ((_rx_features & LIRC_CAN_SET_REC_TIMEOUT) != 0) {
        std::string timeoutError;
        if (!ioctlSet(_rx_fd, LIRC_SET_REC_TIMEOUT, 100000, "LIRC_SET_REC_TIMEOUT", timeoutError)) {
            spdlog::warn("IrServiceModel: RX will use userspace silence timeout: {}", timeoutError);
        } else if (!ioctlSet(_rx_fd, LIRC_SET_REC_TIMEOUT_REPORTS, 1, "LIRC_SET_REC_TIMEOUT_REPORTS", timeoutError)) {
            spdlog::warn("IrServiceModel: RX timeout reports unavailable: {}", timeoutError);
        }
    }

    spdlog::info("IrServiceModel: RX open device={} rc={} driver={} features={} mode=MODE2", _rx_device,
                 selected.rcPath.empty() ? "unmapped" : selected.rcPath, _rx_driver, formatFeatures(_rx_features));
    return true;
}

bool IrServiceModel::ensureTxOpen()
{
    if (_tx_fd >= 0) {
        return true;
    }

    LircDeviceCandidate selected;
    std::string error;
    if (!discoverLircDevice(LircDeviceRole::Transmitter, "IR_REMOTE_LIRC_TX_DEVICE", "IR_REMOTE_LIRC_TX_RC", selected,
                            error)) {
        setError(error);
        return false;
    }

    const int fd = ::open(selected.devicePath.c_str(), O_WRONLY | O_CLOEXEC);
    if (fd < 0) {
        const int errorNumber = errno;
        setError(errnoMessage("Failed to open IR transmitter " + selected.devicePath, errorNumber));
        return false;
    }

    uint32_t features = 0;
    if (!readLircFeatures(fd, features, error)) {
        ::close(fd);
        setError(error);
        return false;
    }
    if (!lircDeviceSupportsRole(features, LircDeviceRole::Transmitter)) {
        ::close(fd);
        setError("IR transmitter " + selected.devicePath + " lost " +
                 requiredCapabilityName(LircDeviceRole::Transmitter) + " capability");
        return false;
    }
    if (!ioctlSet(fd, LIRC_SET_SEND_MODE, LIRC_MODE_PULSE, "LIRC_SET_SEND_MODE", error)) {
        ::close(fd);
        setError(error);
        return false;
    }

    _tx_fd       = fd;
    _tx_device   = std::move(selected.devicePath);
    _tx_driver   = std::move(selected.driver);
    _tx_features = features;
    spdlog::info("IrServiceModel: TX open device={} rc={} driver={} features={} mode=PULSE", _tx_device,
                 selected.rcPath.empty() ? "unmapped" : selected.rcPath, _tx_driver, formatFeatures(_tx_features));
    return true;
}

void IrServiceModel::closeRx()
{
    if (_rx_fd >= 0) {
        ::close(_rx_fd);
        _rx_fd = -1;
    }
    _rx_features = 0;
}

void IrServiceModel::closeTx()
{
    if (_tx_fd >= 0) {
        ::close(_tx_fd);
        _tx_fd = -1;
    }
    _tx_features = 0;
}

void IrServiceModel::closeDevices()
{
    closeRx();
    closeTx();
}

void IrServiceModel::pollLirc(uint32_t nowMs)
{
    if (_rx_fd < 0) {
        return;
    }

    while (true) {
        lirc_t value        = 0;
        const ssize_t bytes = ::read(_rx_fd, &value, sizeof(value));
        if (bytes == sizeof(value)) {
            if (isLircPulse(value) || isLircSpace(value)) {
                const uint32_t duration = LIRC_VALUE(value);
                if (duration > 0) {
                    _capture_buffer.push_back({isLircPulse(value), duration});
                    _last_sample_ms = nowMs;
                }
            } else if (LIRC_IS_TIMEOUT(value) && !_capture_buffer.empty()) {
                finishCapture(true);
                return;
            } else if (LIRC_IS_OVERFLOW(value)) {
                finishCapture(false, "IR receive overflow");
                return;
            }
            continue;
        }

        if (bytes < 0 && errno == EINTR) {
            continue;
        }
        if (bytes < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return;
        }

        if (bytes < 0) {
            const int errorNumber = errno;
            finishCapture(false, errnoMessage("LIRC read failed", errorNumber));
        } else if (bytes == 0) {
            finishCapture(false, "LIRC receiver reached end of stream");
        } else {
            finishCapture(false,
                          "LIRC short read: " + std::to_string(bytes) + "/" + std::to_string(sizeof(value)) + " bytes");
        }
        return;
    }
}

void IrServiceModel::finishCapture(bool ok, const std::string& error)
{
    if (ok && !_capture_buffer.empty()) {
        _captured_signal.pulses           = std::move(_capture_buffer);
        _captured_signal.carrierHz        = kDefaultCarrierHz;
        _captured_signal.dutyCyclePercent = kDefaultDutyCycle;
        _captured_signal.sourceDriver     = _rx_driver.empty() ? "lirc" : _rx_driver;
        _captured_signal.sourceDevice     = _rx_device;
        _capture_ready                    = true;
        _last_error.set("");
        spdlog::info("IrServiceModel: capture complete pulses={} device={}", _captured_signal.pulses.size(),
                     _rx_device);
    } else {
        _capture_ready = false;
        _capture_buffer.clear();
        setError(error.empty() ? "Capture failed" : error);
    }
    closeRx();
    _state.set(IrServiceState::Idle);
}
#endif

#if IR_REMOTE_USE_DUMMY_IR
void IrServiceModel::finishDummyCapture(uint32_t nowMs)
{
    if (_state.get() != IrServiceState::Capturing || nowMs - _capture_start_ms < kDummyCaptureDelayMs) {
        return;
    }

    const auto seed  = static_cast<uint32_t>(std::chrono::steady_clock::now().time_since_epoch().count() ^
                                             static_cast<uint64_t>(nowMs));
    _captured_signal = makeDummySignal(seed);
    _capture_ready   = true;
    _last_error.set("");
    _state.set(IrServiceState::Idle);
    spdlog::info("IrServiceModel: dummy capture complete pulses={}", _captured_signal.pulses.size());
}
#endif

void IrServiceModel::setError(std::string message)
{
    spdlog::warn("IrServiceModel: {}", message);
    _last_error.set(std::move(message));
}

}  // namespace ir_remote
