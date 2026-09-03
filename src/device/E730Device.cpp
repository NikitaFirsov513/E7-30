#include "E730Device.h"

#include <utility>

DeviceStatus E730Device::getStatus() const {
    std::lock_guard<std::mutex> lock(statusMutex_);
    return status_;
}

void E730Device::setState(DeviceState newState) {
    std::lock_guard<std::mutex> lock(statusMutex_);
    status_.state = newState;
}

void E730Device::setError(std::string error) {
    std::lock_guard<std::mutex> lock(statusMutex_);
    status_.lastError = std::move(error);
    status_.state = DeviceState::ERR;
}

void E730Device::clearError() {
    std::lock_guard<std::mutex> lock(statusMutex_);
    status_.lastError.clear();
}

void E730Device::setLastDeviceResponse(DeviceResponse response) {
    std::lock_guard<std::mutex> lock(statusMutex_);
    status_.lastSuccessfulResponse = response.receivedAt;
    status_.lastDeviceResponse = std::move(response);
}
