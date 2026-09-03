#pragma once

#include "../DeviceCommunicator.h"

#include <chrono>
#include <cstdint>
#include <deque>

class UI {
public:
    explicit UI(DeviceCommunicator& communicator) : deviceCommunicator_(&communicator) {}
    void Render();

private:
    struct PlotPoint {
        std::chrono::system_clock::time_point time;
        float value1;
        float value2;
    };

    void ShowMainDockspace();
    void ShowDeviceControlWindow();
    void ShowExperimentWindow();
    void ShowLaboratoryWindow();
    void updateHistory(const DeviceResponse& response);
    void plotHistory(const char* label, bool secondValue);

    DeviceCommunicator* deviceCommunicator_;
    std::uint64_t lastPlottedSequence_ = 0;
    std::int32_t plottedFrequency_ = 0;
    std::deque<PlotPoint> history_;
    static constexpr std::size_t kMaxHistory = 500;
};
