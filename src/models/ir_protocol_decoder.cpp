#include "models/ir_protocol_decoder.hpp"
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace ir_remote {
namespace {

struct NormalizedPulse {
    bool pulse = true;
    uint32_t durationUs = 0;
};

bool closeTo(uint32_t value, uint32_t target, uint32_t tolerancePercent = 28)
{
    const uint32_t tolerance = (target * tolerancePercent) / 100;
    return value >= target - tolerance && value <= target + tolerance;
}

std::vector<NormalizedPulse> normalized(const IrSignal& signal)
{
    std::vector<NormalizedPulse> out;
    out.reserve(signal.pulses.size());
    for (const auto& pulse : signal.pulses) {
        if (pulse.durationUs == 0) {
            continue;
        }
        if (!out.empty() && out.back().pulse == pulse.pulse) {
            out.back().durationUs += pulse.durationUs;
        } else {
            out.push_back({pulse.pulse, pulse.durationUs});
        }
    }
    return out;
}

IrProtocolDecodeResult result(std::string protocol, uint32_t address, uint32_t command, uint32_t bits,
                              uint32_t confidence)
{
    IrProtocolDecodeResult out;
    out.decoded = true;
    out.protocol = std::move(protocol);
    out.address = address;
    out.command = command;
    out.bits = bits;
    out.confidence = confidence;
    return out;
}

bool decodeNecBits(const std::vector<NormalizedPulse>& pulses, size_t start, uint32_t bitCount, uint32_t& outData)
{
    if (pulses.size() < start + bitCount * 2) {
        return false;
    }

    uint32_t data = 0;
    for (uint32_t i = 0; i < bitCount; ++i) {
        const auto& mark = pulses[start + i * 2];
        const auto& space = pulses[start + i * 2 + 1];
        if (!mark.pulse || space.pulse || !closeTo(mark.durationUs, 560)) {
            return false;
        }

        if (closeTo(space.durationUs, 560)) {
            continue;
        }
        if (closeTo(space.durationUs, 1690)) {
            data |= (1U << i);
            continue;
        }
        return false;
    }

    outData = data;
    return true;
}

bool decodeNec(const std::vector<NormalizedPulse>& pulses, IrProtocolDecodeResult& out)
{
    if (pulses.size() < 66 || !pulses[0].pulse || pulses[1].pulse || !closeTo(pulses[0].durationUs, 9000) ||
        !closeTo(pulses[1].durationUs, 4500)) {
        return false;
    }

    uint32_t data = 0;
    if (!decodeNecBits(pulses, 2, 32, data)) {
        return false;
    }

    const uint8_t address = data & 0xFFU;
    const uint8_t address_inv = (data >> 8) & 0xFFU;
    const uint8_t command = (data >> 16) & 0xFFU;
    const uint8_t command_inv = (data >> 24) & 0xFFU;
    const bool command_ok = static_cast<uint8_t>(command ^ command_inv) == 0xFFU;

    if (static_cast<uint8_t>(address ^ address_inv) == 0xFFU && command_ok) {
        out = result("NEC", address, command, 32, 96);
        return true;
    }

    if (command_ok) {
        const uint32_t extended_address = address | (static_cast<uint32_t>(address_inv) << 8);
        out = result("NEC Ext", extended_address, command, 32, 88);
        return true;
    }

    return false;
}

bool decodeSony(const std::vector<NormalizedPulse>& pulses, IrProtocolDecodeResult& out)
{
    if (pulses.size() < 25 || !pulses[0].pulse || pulses[1].pulse || !closeTo(pulses[0].durationUs, 2400) ||
        !closeTo(pulses[1].durationUs, 600)) {
        return false;
    }

    uint32_t data = 0;
    uint32_t bits = 0;
    for (size_t i = 2; i + 1 < pulses.size() && bits < 20; i += 2) {
        const auto& mark = pulses[i];
        const auto& space = pulses[i + 1];
        if (!mark.pulse || space.pulse || !closeTo(space.durationUs, 600)) {
            break;
        }

        if (closeTo(mark.durationUs, 600)) {
            ++bits;
            continue;
        }
        if (closeTo(mark.durationUs, 1200)) {
            data |= (1U << bits);
            ++bits;
            continue;
        }
        return false;
    }

    if (bits != 12 && bits != 15 && bits != 20) {
        return false;
    }

    const uint32_t command = data & 0x7FU;
    const uint32_t address = data >> 7;
    out = result("Sony SIRC", address, command, bits, bits == 12 ? 82 : 88);
    return true;
}

int decodeManchesterHalf(bool firstPulse, uint32_t firstDuration, bool secondPulse, uint32_t secondDuration)
{
    if (firstPulse == secondPulse || !closeTo(firstDuration, 889, 34) || !closeTo(secondDuration, 889, 34)) {
        return -1;
    }
    return firstPulse && !secondPulse ? 1 : 0;
}

bool decodeRc5(const std::vector<NormalizedPulse>& pulses, IrProtocolDecodeResult& out)
{
    if (pulses.size() < 28) {
        return false;
    }

    std::vector<int> bits;
    bits.reserve(14);
    for (size_t i = 0; i + 1 < pulses.size() && bits.size() < 14; i += 2) {
        const int bit = decodeManchesterHalf(pulses[i].pulse, pulses[i].durationUs, pulses[i + 1].pulse,
                                             pulses[i + 1].durationUs);
        if (bit < 0) {
            return false;
        }
        bits.push_back(bit);
    }

    if (bits.size() != 14 || bits[0] != 1) {
        return false;
    }

    uint32_t address = 0;
    uint32_t command = bits[1] ? 0 : 0x40;
    for (size_t i = 3; i < 8; ++i) {
        address = (address << 1) | static_cast<uint32_t>(bits[i]);
    }
    for (size_t i = 8; i < 14; ++i) {
        command = (command << 1) | static_cast<uint32_t>(bits[i]);
    }

    out = result("RC5", address, command, 14, 70);
    return true;
}

bool decodeRc6(const std::vector<NormalizedPulse>& pulses, IrProtocolDecodeResult& out)
{
    if (pulses.size() < 40 || !pulses[0].pulse || pulses[1].pulse || !closeTo(pulses[0].durationUs, 2666, 34) ||
        !closeTo(pulses[1].durationUs, 889, 34)) {
        return false;
    }

    std::vector<int> bits;
    bits.reserve(24);
    for (size_t i = 2; i + 1 < pulses.size() && bits.size() < 24; i += 2) {
        uint32_t first = pulses[i].durationUs;
        uint32_t second = pulses[i + 1].durationUs;
        if (bits.size() == 3) {
            first /= 2;
            second /= 2;
        }
        const int bit = decodeManchesterHalf(!pulses[i].pulse, first, !pulses[i + 1].pulse, second);
        if (bit < 0) {
            break;
        }
        bits.push_back(bit);
    }

    if (bits.size() < 20) {
        return false;
    }

    uint32_t mode = 0;
    for (size_t i = 0; i < 3; ++i) {
        mode = (mode << 1) | static_cast<uint32_t>(bits[i]);
    }
    uint32_t address = 0;
    uint32_t command = 0;
    for (size_t i = 4; i < 12 && i < bits.size(); ++i) {
        address = (address << 1) | static_cast<uint32_t>(bits[i]);
    }
    for (size_t i = 12; i < 20 && i < bits.size(); ++i) {
        command = (command << 1) | static_cast<uint32_t>(bits[i]);
    }

    out = result(mode == 0 ? "RC6" : "RC6 mode " + std::to_string(mode), address, command,
                 static_cast<uint32_t>(bits.size()), 65);
    return true;
}

}  // namespace

IrProtocolDecodeResult decodeIrProtocol(const IrSignal& signal)
{
    const auto pulses = normalized(signal);
    IrProtocolDecodeResult out;
    if (pulses.empty()) {
        return out;
    }

    if (decodeNec(pulses, out) || decodeSony(pulses, out) || decodeRc5(pulses, out) || decodeRc6(pulses, out)) {
        return out;
    }
    return out;
}

}  // namespace ir_remote
