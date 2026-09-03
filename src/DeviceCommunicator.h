#pragma once

#include "com/SerialPort.h"
#include "device/E730Device.h"
#include "storage/DataStore.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

class DeviceCommunicator {
public:
    explicit DeviceCommunicator(E730Device& device);
    ~DeviceCommunicator();
    DeviceCommunicator(const DeviceCommunicator&) = delete;
    DeviceCommunicator& operator=(const DeviceCommunicator&) = delete;

    bool tryConnect(std::string portName);
    void disconnect();
    bool start();
    void stop();
    bool isRunning() const noexcept;
    bool isConnected() const;
    void sendCommand(std::vector<std::uint8_t> command);
    void setFrequency(std::uint32_t frequency);
    void resetDevice();
    void setStatusInterval(int seconds);
    void setStatusInterval(std::chrono::milliseconds interval);
    DeviceStatus getLastData() const;
    DeviceState getDeviceState() const;
    DeviceState getDeviseState() const { return getDeviceState(); }

    bool openDB(std::string name = "E730_experiments.db");
    std::int64_t beginExperiment(const ExperimentMetadata& metadata);
    bool finishExperiment();
    std::int64_t activeExperimentId() const noexcept;
    std::int64_t collectLaboratorySample();
    bool enterLaboratoryResult(std::int64_t sampleId, const LaboratoryResult& result);
    bool setLaboratorySampleValidity(std::int64_t sampleId, bool valid);
    std::vector<LaboratorySample> laboratorySamples() const;
    bool addExperimentEvent(const std::string& type, const std::string& comment);
    std::string storageError() const;
    std::size_t bufferedMeasurementCount() const;
    std::int64_t persistedMeasurementCount() const;

private:
    enum class QueueType { Connect, Disconnect, Command, StartRecording, StopRecording, Reschedule };
    struct QueueItem {
        QueueType type = QueueType::Command;
        std::vector<std::uint8_t> bytes;
        std::string portName;
        std::size_t expectedLength = 0;
        std::shared_ptr<std::promise<bool>> completion;
    };

    void ensureWorker();
    bool enqueueAndWait(QueueItem item, std::chrono::milliseconds timeout);
    void enqueue(QueueItem item);
    void communicationLoop() noexcept;
    void processQueueItem(QueueItem item);
    void requestStatus();
    void shutdownWorker() noexcept;

    E730Device* device_;
    SerialPort serialPort_; // Accessed only by communicationThread_.
    std::thread communicationThread_;
    std::atomic<bool> workerRunning_{false};
    std::atomic<bool> recording_{false};
    mutable std::mutex lifecycleMutex_;
    mutable std::mutex queueMutex_;
    std::condition_variable queueCondition_;
    std::queue<QueueItem> commandQueue_;
    std::atomic<std::int64_t> statusIntervalMs_{1000};
    std::chrono::steady_clock::time_point nextStatusRequest_{};
    mutable std::mutex historyMutex_;
    std::deque<DeviceResponse> dataList_;
    static constexpr std::size_t kMaxBufferedMeasurements = 1000;
    DataStore dataStore_;
    std::string databaseName_ = "E730_experiments.db";
    std::atomic<std::int64_t> activeExperimentId_{0};
    std::atomic<std::uint64_t> nextSequenceId_{1};
    std::atomic<std::uint32_t> requestedFrequency_{0};
};
