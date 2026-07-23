#include "models/lirc_device_discovery.hpp"

#include <cstdlib>
#include <iostream>
#include <linux/lirc.h>
#include <string>
#include <vector>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

void testCardputerZeroEnumerationOrder()
{
    const std::vector<ir_remote::LircDeviceCandidate> candidates{
        {"/sys/class/rc/rc2", "/dev/lirc0", "gpio-ir-tx", 0x00000302},
        {"/sys/class/rc/rc1", "/dev/lirc1", "gpio_ir_recv", 0x10040000},
    };

    const auto receiver    = ir_remote::selectLircDevice(candidates, ir_remote::LircDeviceRole::Receiver);
    const auto transmitter = ir_remote::selectLircDevice(candidates, ir_remote::LircDeviceRole::Transmitter);

    require(receiver && receiver->devicePath == "/dev/lirc1", "receiver must be selected by MODE2 capability");
    require(transmitter && transmitter->devicePath == "/dev/lirc0", "transmitter must be selected by PULSE capability");
}

void testNumberingDoesNotDefineRole()
{
    const std::vector<ir_remote::LircDeviceCandidate> candidates{
        {"/sys/class/rc/rc8", "/dev/lirc8", "receiver", LIRC_CAN_REC_MODE2},
        {"/sys/class/rc/rc3", "/dev/lirc3", "transmitter", LIRC_CAN_SEND_PULSE},
    };

    const auto receiver    = ir_remote::selectLircDevice(candidates, ir_remote::LircDeviceRole::Receiver);
    const auto transmitter = ir_remote::selectLircDevice(candidates, ir_remote::LircDeviceRole::Transmitter);

    require(receiver && receiver->devicePath == "/dev/lirc8", "receiver selection must ignore node number");
    require(transmitter && transmitter->devicePath == "/dev/lirc3", "transmitter selection must ignore node number");
}

void testUnsupportedCandidatesAreRejected()
{
    const std::vector<ir_remote::LircDeviceCandidate> candidates{
        {"/sys/class/rc/rc0", "/dev/lirc0", "cec", LIRC_CAN_REC_SCANCODE},
        {"/sys/class/rc/rc1", "/dev/lirc1", "unknown", 0},
    };

    require(!ir_remote::selectLircDevice(candidates, ir_remote::LircDeviceRole::Receiver),
            "SCANCODE-only receiver cannot capture raw pulses");
    require(!ir_remote::selectLircDevice(candidates, ir_remote::LircDeviceRole::Transmitter),
            "candidate without PULSE capability cannot transmit raw pulses");
}

void testStableFirstMatch()
{
    const std::vector<ir_remote::LircDeviceCandidate> candidates{
        {"/sys/class/rc/rc1", "/dev/lirc1", "first", LIRC_CAN_REC_MODE2 | LIRC_CAN_SEND_PULSE},
        {"/sys/class/rc/rc2", "/dev/lirc2", "second", LIRC_CAN_REC_MODE2 | LIRC_CAN_SEND_PULSE},
    };

    const auto receiver    = ir_remote::selectLircDevice(candidates, ir_remote::LircDeviceRole::Receiver);
    const auto transmitter = ir_remote::selectLircDevice(candidates, ir_remote::LircDeviceRole::Transmitter);

    require(receiver && receiver->devicePath == "/dev/lirc1", "receiver selection must be stable");
    require(transmitter && transmitter->devicePath == "/dev/lirc1", "transmitter selection must be stable");
}

}  // namespace

int main()
{
    testCardputerZeroEnumerationOrder();
    testNumberingDoesNotDefineRole();
    testUnsupportedCandidatesAreRejected();
    testStableFirstMatch();
    std::cout << "LIRC device discovery tests passed\n";
    return 0;
}
