#pragma once

#include "../device/E730Device.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace e730 {

constexpr std::uint8_t kHeader = 0xAA;
constexpr std::uint8_t kSetFrequencyCommand = 67;
constexpr std::uint8_t kResetCommand = 71;
constexpr std::uint8_t kStatusCommand = 72;
constexpr std::size_t kLegacyStatusResponseSize = 20;
constexpr std::uint32_t kMinimumFrequencyHz = 25;
constexpr std::uint32_t kMaximumFrequencyHz = 3000000;

// These values are isolated legacy assumptions recovered from the original code.
// They must be verified against the manufacturer's protocol before experiments.
struct ProtocolProfile {
    std::size_t statusResponseSize = kLegacyStatusResponseSize;
    std::size_t acknowledgementSize = 2;
    bool hasLengthField = false;
    bool hasChecksum = false;
    bool integersAreBigEndian = true;
    bool floatsAreBigEndian = true;
};

enum class ParseError {
    None,
    TooShort,
    WrongLength,
    WrongHeader,
    WrongCommand,
    ChecksumUnavailable,
    UnsupportedByteOrder,
    NonFiniteValue
};

struct ParseResult {
    bool success = false;
    ParseError error = ParseError::None;
    std::string message;
    DeviceResponse response{};
};

std::vector<std::uint8_t> encodeStatusRequest();
std::vector<std::uint8_t> encodeSetFrequency(std::uint32_t frequency);
std::vector<std::uint8_t> encodeReset();
std::size_t expectedResponseLength(std::uint8_t command,
                                   const ProtocolProfile& profile = {});
ParseResult parseStatusResponse(const std::vector<std::uint8_t>& data,
                                const ProtocolProfile& profile = {});
bool validateEnvelope(const std::vector<std::uint8_t>& data,
                      std::uint8_t expectedCommand,
                      std::size_t expectedLength,
                      std::string& error);

class ResponseAccumulator {
public:
    ResponseAccumulator(std::uint8_t command, std::size_t expectedLength);
    bool append(const std::uint8_t* data, std::size_t size, std::string& error);
    bool complete() const noexcept;
    const std::vector<std::uint8_t>& data() const noexcept;

private:
    std::uint8_t command_;
    std::size_t expectedLength_;
    std::vector<std::uint8_t> data_;
};

std::vector<std::uint8_t> emulateStatusPacket(std::int32_t frequency,
                                              float value1,
                                              float value2,
                                              bool overload = false,
                                              bool done = true);

} // namespace e730
