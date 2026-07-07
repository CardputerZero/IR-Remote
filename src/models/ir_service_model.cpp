#include "models/ir_service_model.hpp"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <utility>

#if IR_REMOTE_USE_LIRC_IR
#include <dirent.h>
#include <fcntl.h>
#include <linux/lirc.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace ir_remote {
namespace {

constexpr uint32_t kDefaultCarrierHz      = 38000;
constexpr uint32_t kDefaultDutyCycle      = 33;
constexpr uint32_t kCaptureTimeoutMs      = 6000;
constexpr uint32_t kCaptureSilenceMs      = 220;
constexpr uint32_t kDummyCaptureDelayMs   = 700;
constexpr const char* kDefaultRxRcPath    = "/sys/class/rc/rc0";
constexpr const char* kDefaultTxRcPath    = "/sys/class/rc/rc1";
constexpr const char* kFallbackRxLircPath = "/dev/lirc0";
constexpr const char* kFallbackTxLircPath = "/dev/lirc1";

std::string envOrDefault(const char* name, const char* fallback)
{
    const char* value = std::getenv(name);
    return value && value[0] != '\0' ? value : fallback;
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

std::string firstLircDeviceForRc(const std::string& rcPath)
{
    DIR* handle = opendir(rcPath.c_str());
    if (!handle) {
        return "";
    }

    std::string device;
    while (dirent* entry = readdir(handle)) {
        const std::string name = entry->d_name;
        if (name.rfind("lirc", 0) == 0) {
            device = "/dev/" + name;
            break;
        }
    }
    closedir(handle);
    return device;
}

std::string resolveLircDevice(const char* deviceEnv, const char* rcEnv, const char* defaultRc, const char* fallback,
                              std::string& outDriver)
{
    const std::string explicit_device = envOrDefault(deviceEnv, "");
    if (!explicit_device.empty()) {
        outDriver = "env";
        return explicit_device;
    }

    const std::string rcPath = envOrDefault(rcEnv, defaultRc);
    outDriver                = rcDriverName(rcPath);
    std::string device       = firstLircDeviceForRc(rcPath);
    if (!device.empty()) {
        return device;
    }

    return fallback;
}

bool ioctlSet(int fd, unsigned long request, uint32_t value, const char* what)
{
    if (::ioctl(fd, request, &value) == 0) {
        return true;
    }

    spdlog::warn("IrServiceModel: ioctl {} failed, errno={}", what, errno);
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

    ioctlSet(_tx_fd, LIRC_SET_SEND_CARRIER, signal.carrierHz, "LIRC_SET_SEND_CARRIER");
    ioctlSet(_tx_fd, LIRC_SET_SEND_DUTY_CYCLE, signal.dutyCyclePercent, "LIRC_SET_SEND_DUTY_CYCLE");

    std::vector<lirc_t> durations;
    durations.reserve(signal.pulses.size());
    bool have_value = false;
    bool last_pulse = true;
    for (const auto& pulse : signal.pulses) {
        if (pulse.durationUs == 0) {
            continue;
        }
        if (!have_value && !pulse.pulse) {
            continue;
        }
        if (have_value && pulse.pulse == last_pulse) {
            durations.back() += static_cast<lirc_t>(pulse.durationUs);
        } else {
            durations.push_back(static_cast<lirc_t>(pulse.durationUs));
            have_value = true;
            last_pulse = pulse.pulse;
        }
    }

    if (durations.empty()) {
        _state.set(IrServiceState::Idle);
        setError("IR signal has no pulse data");
        return false;
    }

    const size_t bytes    = durations.size() * sizeof(lirc_t);
    const ssize_t written = ::write(_tx_fd, durations.data(), bytes);
    if (written != static_cast<ssize_t>(bytes)) {
        const std::string error = "LIRC write failed, errno=" + std::to_string(errno);
        _state.set(IrServiceState::Idle);
        setError(error);
        return false;
    }

    spdlog::info("IrServiceModel: sent pulses={} carrier={}Hz device={}", durations.size(), signal.carrierHz,
                 _tx_device);
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

    _rx_device = resolveLircDevice("IR_REMOTE_LIRC_RX_DEVICE", "IR_REMOTE_LIRC_RX_RC", kDefaultRxRcPath,
                                   kFallbackRxLircPath, _rx_driver);
    _rx_fd     = ::open(_rx_device.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (_rx_fd < 0) {
        setError("Failed to open IR receiver " + _rx_device + ", errno=" + std::to_string(errno));
        return false;
    }

    ioctlSet(_rx_fd, LIRC_SET_REC_MODE, LIRC_MODE_MODE2, "LIRC_SET_REC_MODE");
    ioctlSet(_rx_fd, LIRC_SET_REC_TIMEOUT, 100000, "LIRC_SET_REC_TIMEOUT");
    ioctlSet(_rx_fd, LIRC_SET_REC_TIMEOUT_REPORTS, 1, "LIRC_SET_REC_TIMEOUT_REPORTS");
    spdlog::info("IrServiceModel: RX open device={} driver={}", _rx_device, _rx_driver);
    return true;
}

bool IrServiceModel::ensureTxOpen()
{
    if (_tx_fd >= 0) {
        return true;
    }

    _tx_device = resolveLircDevice("IR_REMOTE_LIRC_TX_DEVICE", "IR_REMOTE_LIRC_TX_RC", kDefaultTxRcPath,
                                   kFallbackTxLircPath, _tx_driver);
    _tx_fd     = ::open(_tx_device.c_str(), O_WRONLY | O_CLOEXEC);
    if (_tx_fd < 0) {
        setError("Failed to open IR transmitter " + _tx_device + ", errno=" + std::to_string(errno));
        return false;
    }

    ioctlSet(_tx_fd, LIRC_SET_SEND_MODE, LIRC_MODE_PULSE, "LIRC_SET_SEND_MODE");
    ioctlSet(_tx_fd, LIRC_SET_SEND_CARRIER, kDefaultCarrierHz, "LIRC_SET_SEND_CARRIER");
    ioctlSet(_tx_fd, LIRC_SET_SEND_DUTY_CYCLE, kDefaultDutyCycle, "LIRC_SET_SEND_DUTY_CYCLE");
    spdlog::info("IrServiceModel: TX open device={} driver={}", _tx_device, _tx_driver);
    return true;
}

void IrServiceModel::closeDevices()
{
    if (_rx_fd >= 0) {
        ::close(_rx_fd);
        _rx_fd = -1;
    }
    if (_tx_fd >= 0) {
        ::close(_tx_fd);
        _tx_fd = -1;
    }
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

        if (bytes < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
            return;
        }

        if (bytes < 0) {
            finishCapture(false, "LIRC read failed, errno=" + std::to_string(errno));
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
