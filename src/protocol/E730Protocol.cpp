#include "E730Protocol.h"

#include <cmath>
#include <cstring>
#include <sstream>
#include <utility>

namespace {

std::uint16_t readBe16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(p[0]) << 8U) |
                                      static_cast<std::uint16_t>(p[1]));
}

std::uint32_t readBe32(const std::uint8_t* p) {
    return (static_cast<std::uint32_t>(p[0]) << 24U) |
           (static_cast<std::uint32_t>(p[1]) << 16U) |
           (static_cast<std::uint32_t>(p[2]) << 8U) |
           static_cast<std::uint32_t>(p[3]);
}

float readBeFloat(const std::uint8_t* p) {
    const std::uint32_t bits = readBe32(p);
    float value = 0.0F;
    static_assert(sizeof(value) == sizeof(bits), "32-bit float required");
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

void writeBe32(std::vector<std::uint8_t>& out, std::size_t offset, std::uint32_t value) {
    out[offset] = static_cast<std::uint8_t>((value >> 24U) & 0xFFU);
    out[offset + 1U] = static_cast<std::uint8_t>((value >> 16U) & 0xFFU);
    out[offset + 2U] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
    out[offset + 3U] = static_cast<std::uint8_t>(value & 0xFFU);
}

void writeBeFloat(std::vector<std::uint8_t>& out, std::size_t offset, float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    writeBe32(out, offset, bits);
}

} // namespace

namespace e730 {

std::vector<std::uint8_t> encodeStatusRequest() { return {kHeader, kStatusCommand, 0x00}; }

std::vector<std::uint8_t> encodeSetFrequency(std::uint32_t frequency) {
    std::vector<std::uint8_t> command(6U);
    command[0] = kHeader;
    command[1] = kSetFrequencyCommand;
    writeBe32(command, 2U, frequency);
    return command;
}

std::vector<std::uint8_t> encodeReset() { return {kHeader, kResetCommand}; }

std::size_t expectedResponseLength(std::uint8_t command, const ProtocolProfile& profile) {
    return command == kStatusCommand ? profile.statusResponseSize : profile.acknowledgementSize;
}

bool validateEnvelope(const std::vector<std::uint8_t>& data,
                      std::uint8_t expectedCommand,
                      std::size_t expectedLength,
                      std::string& error) {
    if (data.size() != expectedLength) {
        std::ostringstream message;
        message << "response length " << data.size() << ", expected " << expectedLength;
        error = message.str();
        return false;
    }
    if (data.size() < 2U) {
        error = "response is shorter than the two-byte envelope";
        return false;
    }
    if (data[0] != kHeader) {
        error = "unexpected response header";
        return false;
    }
    if (data[1] != expectedCommand) {
        error = "response command does not match request";
        return false;
    }
    return true;
}

ParseResult parseStatusResponse(const std::vector<std::uint8_t>& data,
                                const ProtocolProfile& profile) {
    ParseResult result;
    result.response.rawPacket = data;
    result.response.timestamp = std::chrono::system_clock::now();
    result.response.receivedAt = result.response.timestamp;
    if (data.size() < 2U) {
        result.error = ParseError::TooShort;
        result.message = "packet is shorter than the envelope";
        return result;
    }
    std::string envelopeError;
    if (!validateEnvelope(data, kStatusCommand, profile.statusResponseSize, envelopeError)) {
        result.error = data.size() != profile.statusResponseSize
                           ? ParseError::WrongLength
                           : (data[0] != kHeader ? ParseError::WrongHeader : ParseError::WrongCommand);
        result.message = std::move(envelopeError);
        return result;
    }
    if (profile.hasLengthField) {
        result.error = ParseError::WrongLength;
        result.message = "length-field position is not documented";
        return result;
    }
    if (profile.hasChecksum) {
        result.error = ParseError::ChecksumUnavailable;
        result.message = "checksum algorithm and position are not documented";
        return result;
    }
    if (!profile.integersAreBigEndian || !profile.floatsAreBigEndian) {
        result.error = ParseError::UnsupportedByteOrder;
        result.message = "only the isolated legacy big-endian profile is implemented";
        return result;
    }
    const std::uint8_t flags = data[2];
    result.response.avp = (flags & (1U << 0U)) != 0U;
    result.response.overload = (flags & (1U << 2U)) != 0U;
    result.response.autoParameter = (flags & (1U << 3U)) != 0U;
    result.response.scheme = (flags & (1U << 4U)) != 0U;
    result.response.done = (flags & (1U << 7U)) != 0U;
    result.response.mode = data[3];
    result.response.slow = data[4];
    result.response.range = data[5];
    result.response.offset = static_cast<std::int16_t>(readBe16(data.data() + 6U));
    result.response.returnedFrequency = static_cast<std::int32_t>(readBe32(data.data() + 8U));
    result.response.value1 = readBeFloat(data.data() + 12U);
    result.response.value2 = readBeFloat(data.data() + 16U);
    if (!std::isfinite(result.response.value1) || !std::isfinite(result.response.value2)) {
        result.error = ParseError::NonFiniteValue;
        result.message = "numeric field contains NaN or infinity";
        return result;
    }
    result.response.valid = result.response.done && !result.response.overload;
    result.response.diagnostic =
        "legacy 20-byte profile; field meaning, units and checksum remain unverified";
    result.success = true;
    return result;
}

ResponseAccumulator::ResponseAccumulator(std::uint8_t command, std::size_t expectedLength)
    : command_(command), expectedLength_(expectedLength) { data_.reserve(expectedLength); }

bool ResponseAccumulator::append(const std::uint8_t* bytes,
                                 std::size_t size,
                                 std::string& error) {
    if (bytes == nullptr && size != 0U) {
        error = "null response chunk";
        return false;
    }
    if (data_.size() + size > expectedLength_) {
        error = "response exceeds expected length";
        return false;
    }
    if (size != 0U) {
        data_.insert(data_.end(), bytes, bytes + size);
    }
    return true;
}

bool ResponseAccumulator::complete() const noexcept { return data_.size() == expectedLength_; }
const std::vector<std::uint8_t>& ResponseAccumulator::data() const noexcept { return data_; }

std::vector<std::uint8_t> emulateStatusPacket(std::int32_t frequency,
                                              float value1,
                                              float value2,
                                              bool overload,
                                              bool done) {
    std::vector<std::uint8_t> packet(kLegacyStatusResponseSize, 0U);
    packet[0] = kHeader;
    packet[1] = kStatusCommand;
    packet[2] = static_cast<std::uint8_t>((1U << 0U) |
                                         (overload ? (1U << 2U) : 0U) |
                                         (done ? (1U << 7U) : 0U));
    packet[3] = 1U;
    packet[5] = 2U;
    writeBe32(packet, 8U, static_cast<std::uint32_t>(frequency));
    writeBeFloat(packet, 12U, value1);
    writeBeFloat(packet, 16U, value2);
    return packet;
}

} // namespace e730
