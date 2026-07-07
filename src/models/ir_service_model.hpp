#pragma once

#include "core/ir_remote_types.hpp"
#include <tools/observable/single_observable.hpp>
#include <string>

namespace ir_remote {

class IrServiceModel {
public:
    IrServiceModel();
    ~IrServiceModel();

    IrServiceModel(const IrServiceModel&)            = delete;
    IrServiceModel& operator=(const IrServiceModel&) = delete;

    smooth_ui_toolkit::SingleObservable<IrServiceState>& state()
    {
        return _state;
    }

    smooth_ui_toolkit::SingleObservable<std::string>& lastError()
    {
        return _last_error;
    }

    void tick(uint32_t nowMs);
    bool startCapture(uint32_t nowMs);
    bool isCapturing() const;
    bool consumeCapturedSignal(IrSignal& outSignal);
    bool send(const IrSignal& signal);

private:
    smooth_ui_toolkit::SingleObservable<IrServiceState> _state{IrServiceState::Idle};
    smooth_ui_toolkit::SingleObservable<std::string> _last_error{""};
    IrSignal _captured_signal;
    bool _capture_ready        = false;
    uint32_t _capture_start_ms = 0;
    uint32_t _last_sample_ms   = 0;

#if IR_REMOTE_USE_LIRC_IR
    int _rx_fd = -1;
    int _tx_fd = -1;
    std::string _rx_device;
    std::string _tx_device;
    std::string _rx_driver;
    std::string _tx_driver;
    std::vector<IrPulse> _capture_buffer;

    bool ensureRxOpen();
    bool ensureTxOpen();
    void closeDevices();
    void pollLirc(uint32_t nowMs);
    void finishCapture(bool ok, const std::string& error = "");
#endif

#if IR_REMOTE_USE_DUMMY_IR
    void finishDummyCapture(uint32_t nowMs);
#endif

    void setError(std::string message);
};

}  // namespace ir_remote
