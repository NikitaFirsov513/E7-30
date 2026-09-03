#include "SerialPort.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <random>
#include <stdexcept>
#include <thread>

namespace {

float randomFloat(float minimum, float maximum) {
    static thread_local std::mt19937 generator{std::random_device{}()};
    std::uniform_real_distribution<float> distribution(minimum, maximum);
    return distribution(generator);
}

} // namespace

SerialPort::~SerialPort() { disconnect(); }

bool SerialPort::connect(const std::string& portName, int baudRate) {
    disconnect();
    if (portName == "EMULATOR" || portName == "EMULATOR_DROP") {
        emulator_ = true;
        emulatorDropAfter_ = portName == "EMULATOR_DROP" ? 5 : -1;
        emulatorStatusCount_ = 0;
        connected_.store(true);
        return true;
    }

    const std::string fullPortName = "\\\\.\\" + portName;
    handle_ = CreateFileA(fullPortName.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                          OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle_ == INVALID_HANDLE_VALUE) {
        connected_.store(false);
        return false;
    }

    DCB parameters{};
    parameters.DCBlength = sizeof(parameters);
    if (!GetCommState(handle_, &parameters)) {
        disconnect();
        return false;
    }
    parameters.BaudRate = static_cast<DWORD>(baudRate);
    parameters.ByteSize = 8;
    parameters.StopBits = ONESTOPBIT;
    parameters.Parity = NOPARITY;
    if (!SetCommState(handle_, &parameters)) {
        disconnect();
        return false;
    }

    COMMTIMEOUTS timeouts{};
    timeouts.ReadIntervalTimeout = 5;
    timeouts.ReadTotalTimeoutConstant = 10;
    timeouts.ReadTotalTimeoutMultiplier = 0;
    timeouts.WriteTotalTimeoutConstant = 100;
    timeouts.WriteTotalTimeoutMultiplier = 5;
    if (!SetCommTimeouts(handle_, &timeouts)) {
        disconnect();
        return false;
    }
    connected_.store(true);
    return true;
}

void SerialPort::disconnect() noexcept {
    connected_.store(false);
    emulator_ = false;
    emulatorDropAfter_ = -1;
    emulatorStatusCount_ = 0;
    if (handle_ != INVALID_HANDLE_VALUE) {
        CancelIo(handle_);
        CloseHandle(handle_);
        handle_ = INVALID_HANDLE_VALUE;
    }
}

bool SerialPort::isConnected() const noexcept {
    return connected_.load() && (emulator_ || handle_ != INVALID_HANDLE_VALUE);
}

void SerialPort::failConnection(const std::string& message) {
    disconnect();
    throw std::runtime_error(message);
}

bool SerialPort::writeAll(const std::vector<std::uint8_t>& data,
                          std::chrono::steady_clock::time_point deadline) {
    std::size_t offset = 0;
    while (offset < data.size() && std::chrono::steady_clock::now() < deadline) {
        DWORD written = 0;
        const std::size_t remaining = data.size() - offset;
        const DWORD request = static_cast<DWORD>(std::min<std::size_t>(remaining, MAXDWORD));
        if (!WriteFile(handle_, data.data() + offset, request, &written, nullptr)) {
            failConnection("WriteFile failed; serial connection was closed");
        }
        if (written == 0U) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        offset += written;
    }
    return offset == data.size();
}

std::vector<std::uint8_t> SerialPort::sendCommand(
    const std::vector<std::uint8_t>& command,
    std::size_t expectedLength,
    std::chrono::steady_clock::time_point deadline) {
    if (!isConnected()) {
        throw std::runtime_error("serial port is not connected or its handle is invalid");
    }
    if (command.size() < 2U || command[0] != e730::kHeader) {
        throw std::invalid_argument("invalid command envelope");
    }

    if (emulator_) {
        if (command[1] == e730::kSetFrequencyCommand && command.size() == 6U) {
            emulatorFrequency_ = (static_cast<std::uint32_t>(command[2]) << 24U) |
                                 (static_cast<std::uint32_t>(command[3]) << 16U) |
                                 (static_cast<std::uint32_t>(command[4]) << 8U) |
                                 static_cast<std::uint32_t>(command[5]);
        }
        if (command[1] == e730::kStatusCommand) {
            if (emulatorDropAfter_ >= 0 && emulatorStatusCount_++ >= emulatorDropAfter_) {
                disconnect();
                throw std::runtime_error("emulated unexpected serial disconnect");
            }
            return e730::emulateStatusPacket(static_cast<std::int32_t>(emulatorFrequency_),
                                             123.45F + randomFloat(-2.0F, 2.0F),
                                             0.5F + randomFloat(-0.05F, 0.05F));
        }
        return {e730::kHeader, command[1]};
    }

    if (!writeAll(command, deadline)) {
        throw std::runtime_error("serial write deadline expired before all bytes were sent");
    }

    e730::ResponseAccumulator accumulator(command[1], expectedLength);
    std::array<std::uint8_t, 256> buffer{};
    while (!accumulator.complete() && std::chrono::steady_clock::now() < deadline) {
        COMSTAT status{};
        DWORD errors = 0;
        if (!ClearCommError(handle_, &errors, &status)) {
            failConnection("ClearCommError failed; serial connection was closed");
        }
        if ((errors & (CE_FRAME | CE_OVERRUN | CE_RXOVER | CE_RXPARITY)) != 0U) {
            failConnection("serial receive error; connection was closed");
        }
        if (status.cbInQue == 0U) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }
        const DWORD requested = static_cast<DWORD>(std::min<std::size_t>(
            std::min<std::size_t>(status.cbInQue, buffer.size()),
            expectedLength - accumulator.data().size()));
        DWORD bytesRead = 0;
        if (!ReadFile(handle_, buffer.data(), requested, &bytesRead, nullptr)) {
            failConnection("ReadFile failed; serial connection was closed");
        }
        std::string appendError;
        if (!accumulator.append(buffer.data(), bytesRead, appendError)) {
            throw std::runtime_error(appendError);
        }
    }
    if (!accumulator.complete()) {
        if (command[1] == e730::kStatusCommand) {
            return accumulator.data();
        }
        throw std::runtime_error("serial operation deadline expired with a partial response");
    }
    // Status packets are returned to the parser even with a damaged envelope so
    // the raw bytes can be persisted as an invalid diagnostic measurement.
    if (command[1] != e730::kStatusCommand) {
        std::string envelopeError;
        if (!e730::validateEnvelope(accumulator.data(), command[1], expectedLength, envelopeError)) {
            throw std::runtime_error(envelopeError);
        }
    }
    return accumulator.data();
}

e730::ParseResult SerialPort::parseToDeviceResponse(
    const std::vector<std::uint8_t>& data) const {
    return e730::parseStatusResponse(data);
}

DeviceResponse SerialPort::parceToDeviceResponse(
    const std::vector<std::uint8_t>& data) const {
    const auto parsed = parseToDeviceResponse(data);
    if (!parsed.success) {
        throw std::runtime_error(parsed.message);
    }
    return parsed.response;
}

DeviceResponse SerialPort::emulateReadData() const {
    const auto parsed = e730::parseStatusResponse(
        e730::emulateStatusPacket(1000, 123.45F, 0.5F));
    return parsed.response;
}
