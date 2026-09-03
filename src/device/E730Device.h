#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

enum class DeviceState { CONNECTED, RECORDING, ERR, DISCONNECTED };

// Field meaning and units are mode-dependent and intentionally stay neutral
// until the manufacturer's E7-30 protocol documentation is available.
struct DeviceResponse {
    std::uint64_t sequenceId = 0;
    std::chrono::system_clock::time_point timestamp{};
    std::chrono::system_clock::time_point receivedAt{};
    std::uint32_t requestedFrequency = 0;
    bool avp = false;
    bool overload = false;
    bool autoParameter = false;
    bool scheme = false;
    bool done = false;
    bool valid = false;
    std::uint8_t mode = 0;
    std::uint8_t slow = 0;
    std::uint8_t range = 0;
    std::int16_t offset = 0;
    std::int32_t returnedFrequency = 0;
    float value1 = 0.0F;
    float value2 = 0.0F;
    std::vector<std::uint8_t> rawPacket;
    std::string diagnostic;
};

struct DeviceStatus {
    DeviceState state = DeviceState::DISCONNECTED;
    DeviceResponse lastDeviceResponse{};
    std::chrono::system_clock::time_point lastSuccessfulResponse{};
    std::string lastError;
};

class E730Device {
public:
    DeviceStatus getStatus() const;
    void setState(DeviceState newState);
    void setError(std::string error);
    void clearError();
    void setLastDeviceResponse(DeviceResponse response);

    // Compatibility with the original misspelled API.
    void setLastDeviceResponce(DeviceResponse response) {
        setLastDeviceResponse(std::move(response));
    }

private:
    mutable std::mutex statusMutex_;
    DeviceStatus status_{};
};
