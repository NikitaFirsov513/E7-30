#pragma once

#include "../device/E730Device.h"
#include "../protocol/E730Protocol.h"

#include <Windows.h>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class SerialPort {
public:
    SerialPort() = default;
    ~SerialPort();
    SerialPort(const SerialPort&) = delete;
    SerialPort& operator=(const SerialPort&) = delete;

    bool connect(const std::string& portName, int baudRate = 9600);
    void disconnect() noexcept;
    bool isConnected() const noexcept;

    std::vector<std::uint8_t> sendCommand(
        const std::vector<std::uint8_t>& command,
        std::size_t expectedLength,
        std::chrono::steady_clock::time_point deadline);

    e730::ParseResult parseToDeviceResponse(const std::vector<std::uint8_t>& data) const;
    DeviceResponse parceToDeviceResponse(const std::vector<std::uint8_t>& data) const;
    DeviceResponse emulateReadData() const;

private:
    void failConnection(const std::string& message);
    bool writeAll(const std::vector<std::uint8_t>& data,
                  std::chrono::steady_clock::time_point deadline);

    HANDLE handle_ = INVALID_HANDLE_VALUE;
    std::atomic<bool> connected_{false};
    bool emulator_ = false;
    std::uint32_t emulatorFrequency_ = 1000;
    int emulatorDropAfter_ = -1;
    int emulatorStatusCount_ = 0;
};
